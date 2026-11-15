#include "Processor.h"
#include "Editor.h"
#include "EditorStateSchema.h"
#include "EditorPreferences.h"
#include "ABCompareSerialization.h"
#include "StateValidation.h"
#include "../modules/shared/StateValidation.h"

#include "../crossover/UiStateSchema.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../routing/Runtime.h"

#include <optional>
#include <cmath>

namespace
{
size_t getRestoredSelectedCrossoverRange(const juce::ValueTree& state)
{
    const auto key = crossover_ui::makeStatePropertyId("crossover", "visible_band_index");
    return static_cast<size_t>(static_cast<int>(state.getProperty(key, 0)));
}

template <typename Processor>
bool restoreBinaryModuleState(Processor* processor, const juce::String& stateBase64)
{
    if (processor == nullptr)
        return false;

    juce::MemoryBlock stateData;

    if (! stateData.fromBase64Encoding(stateBase64))
        return false;

    return processor->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
}


}

void AvaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    restoreStateInformation(data, sizeInBytes, true, true);
}

bool AvaAudioProcessor::applyClipboardStateInformation(const void* data, const int sizeInBytes)
{
    return restoreStateInformation(data, sizeInBytes, true, false);
}

bool AvaAudioProcessor::applyHistoryStateInformation(const void* data, const int sizeInBytes)
{
    return restoreStateInformation(data, sizeInBytes, false, false);
}

bool AvaAudioProcessor::restoreStateInformation(const void* data,
                                                const int sizeInBytes,
                                                const bool includeABCompareState,
                                                const bool preserveHostEditorView)
{
    // Offline freeze can restore a headless plugin from a render worker.
    // There are no GUI components to protect when no editor is attached, and
    // requiring the message-thread lock there can reject the host's chunk.
    std::unique_ptr<juce::MessageManagerLock> uiLock;
    if (getRoutingOwner().getContentEditor() != nullptr)
    {
        uiLock = std::make_unique<juce::MessageManagerLock>();
        if (!uiLock->lockWasGained()) return false;
    }

    const auto stateXml = getXmlFromBinary(data, sizeInBytes);
    if (stateXml == nullptr || ! stateXml->hasTagName(parameters.state.getType()))
        return false;

    auto restoredState = ava::modules::state::completeMissingParameterDefaults(
        juce::ValueTree::fromXml(*stateXml), parameters);
    // Older sessions and transient editor switches may have saved module blobs
    // without an active tab. Infer the UI tab; never discard saved audio data.
    if (!restoredState.hasProperty(activeModuleStateKey))
    {
        const auto inferred = restoredState.hasProperty(blockChainModuleStateKey) ? ActiveModule::chain
            : restoredState.hasProperty(dynModuleStateKey) ? ActiveModule::dyn
            : restoredState.hasProperty(eqlModuleStateKey) ? ActiveModule::eql
            : ActiveModule::none;
        if (inferred != ActiveModule::none)
            restoredState.setProperty(activeModuleStateKey, stateIdForModule(inferred), nullptr);
    }
    if (!shell_state_validation::hasCurrentState(restoredState, *this, includeABCompareState))
    {
        juce::Logger::writeToLog("AVA: host project state rejected: incompatible outer state schema.");
        return false;
    }

    // Envelope undo often resends the unchanged plugin chunk. Its host-slot
    // values are derived mirrors of module parameters, and view coordinates
    // are not audio edits. Keep live processors, attachments and editors when
    // the processing state is unchanged instead of rebuilding the whole graph.
    // During a DAW Undo/Redo, the host restores an old *audio* snapshot.
    // If the editor is currently open, the current scroll positions, collapsed
    // sections, dock layout and selected view must remain unchanged.  The
    // snapshot is still allowed to restore its UI when the project is first
    // loaded (there is no live editor in that case).
    const bool preserveLiveView = preserveHostEditorView && getContentEditor() != nullptr;
    // The APVTS scroll property is not necessarily the currently visible
    // pixel position. Flush both root and active INSTANCE before merging
    // the host snapshot, otherwise Undo can restore scroll.eql = 0.
    if (preserveLiveView)
        getContentEditor()->captureLiveViewBeforeHostRestore();
    if (includeABCompareState && !routingInstance
        && (getBlockChainProcessor() != nullptr || preserveLiveView))
    {
        std::unique_ptr<juce::XmlElement> liveXml;
        uint64_t liveRevision = 0, liveViewRevision = 0;
        {
            const juce::ScopedLock lock(processingLock);
            liveRevision = getStateRevision();
            liveViewRevision = viewStateRevision.load(std::memory_order_relaxed);
            if (cachedHostStateXml != nullptr && cachedHostAudioRevision == liveRevision
                && cachedHostViewRevision == liveViewRevision)
                liveXml = std::make_unique<juce::XmlElement>(*cachedHostStateXml);
        }
        if (liveXml == nullptr)
        {
            juce::MemoryBlock liveData;
            writeStateInformation(liveData, true);
            liveXml = getXmlFromBinary(liveData.getData(), int(liveData.getSize()));
            liveRevision = getStateRevision();
            liveViewRevision = viewStateRevision.load(std::memory_order_relaxed);
        }
        auto incomingXml = restoredState.createXml();
        if (liveXml != nullptr && incomingXml != nullptr)
        {
            if (preserveLiveView)
            {
                preserveEditorViewState(*incomingXml, *liveXml);
                // Keep the merged view in the state that will actually be
                // loaded below. Previously the merge affected only this
                // temporary XML used for comparison, so external DAW Undo
                // restored stale collapse and scroll coordinates anyway.
            }
            // During ordinary envelope Undo, REAPER may restore an older
            // chunk while separately driving the current host parameters.
            // During offline render, however, the incoming chunk is the
            // authoritative initial state of the render processor. Copying
            // live slot values over it breaks freeze automation.
            if (!isNonRealtime())
                for (auto* child : incomingXml->getChildIterator())
                    if (child->hasTagName("PARAM")
                        && child->getStringAttribute("id").startsWith(paramHostSlotPrefix))
                        for (const auto* live : liveXml->getChildIterator())
                            if (live->hasTagName("PARAM") && live->getStringAttribute("id") == child->getStringAttribute("id"))
                            { child->setAttribute("value", live->getStringAttribute("value")); break; }
            if (preserveLiveView)
                restoredState = juce::ValueTree::fromXml(*incomingXml);
            if (getStateRevision() == liveRevision && viewStateRevision.load(std::memory_order_relaxed) == liveViewRevision
                && incomingXml->isEquivalentTo(liveXml.get(), true))
            {
                // A byte-identical host chunk does not imply that cached
                // target pointers survived REAPER's offline render lifecycle.
                // Restore the slot->DSP connection even on this fast path.
                if (!routingInstance)
                {
                    queueCurrentHostSlotValues();
                    refreshHostSlotTargets();
                }
                if (preserveLiveView) getContentEditor()->cancelLiveViewHostRestore();
                return true;
            }
        }
    }

    const auto restoredActiveModule = moduleFromStateId(restoredState.getProperty(activeModuleStateKey).toString());
    auto restoredABCompareState = includeABCompareState
        ? std::optional<shell_state_serialization::RestoredABCompareState> { shell_state_serialization::RestoredABCompareState(restoredState) }
        : std::nullopt;
    if (restoredABCompareState.has_value())
    {
        // An obsolete A/B snapshot is not evidence that the *live* project
        // state is corrupt. Discard only that snapshot, not all host settings.
        const auto validateSnapshot = [this](juce::String& encoded, const char* label)
        {
            if (encoded.isEmpty()) return;
            juce::MemoryBlock bytes;
            const auto decoded = bytes.fromBase64Encoding(encoded)
                ? juce::AudioProcessor::getXmlFromBinary(bytes.getData(), static_cast<int>(bytes.getSize()))
                : nullptr;
            if (decoded != nullptr
                && shell_state_validation::hasCurrentState(juce::ValueTree::fromXml(*decoded), *this, false))
                return;
            juce::Logger::writeToLog(juce::String("AVA: ignoring incompatible A/B snapshot ") + label);
            encoded.clear();
        };
        validateSnapshot(restoredABCompareState->snapshotA, "A");
        validateSnapshot(restoredABCompareState->snapshotB, "B");
    }
    const auto restoredSelectedRange = getRestoredSelectedCrossoverRange(restoredState);
    shell_state_serialization::removeABCompareStateProperties(restoredState);

    // One restoration path for project load, clipboard, A/B and DAW Undo.
    // The incoming chunk is a complete snapshot, but existing processors and
    // APVTS attachments must not be discarded. Structural changes reconcile
    // only added/removed module UIDs and routing nodes.
    juce::MemoryBlock liveData;
    writeStateInformation(liveData, includeABCompareState);
    const auto liveXml = getXmlFromBinary(liveData.getData(), int(liveData.getSize()));
    if (liveXml == nullptr)
    {
        if (preserveLiveView && getContentEditor() != nullptr)
            getContentEditor()->cancelLiveViewHostRestore();
        return false;
    }
    const auto currentState = juce::ValueTree::fromXml(*liveXml);

    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    const auto previousSuppression = suppressHostStateNotifications.exchange(true, std::memory_order_acq_rel);
    const auto previousRestore = restoringModel.exchange(true, std::memory_order_acq_rel);
    const juce::ScopeGuard restoreProcessingFlags {[this, previousSuppression, previousRestore] {
        restoringModel.store(previousRestore, std::memory_order_release);
        suppressHostStateNotifications.store(previousSuppression, std::memory_order_release);
    }};
    const juce::ScopeGuard finishEditorRestore {[this, preserveLiveView] {
        if (preserveLiveView && getContentEditor() != nullptr)
            getContentEditor()->cancelLiveViewHostRestore();
    }};
    cachedHostStateXml.reset();
    cachedHostStateData.reset();

    bool success = true;
    bool graphChanged = false;
    bool modulesChanged = false;
    bool structuralChanged = false;
    // Changing the crossover band count alters buffer allocation even though
    // it is represented as a normal APVTS parameter.
    const auto previousSplits = ava::modules::state::readParameterPlainValue(
        currentState, paramCrossoverActiveSplitCountId);
    const auto restoredSplits = ava::modules::state::readParameterPlainValue(
        restoredState, paramCrossoverActiveSplitCountId);
    if (previousSplits != restoredSplits)
        structuralChanged = graphChanged = true;

    // APVTS parameters have two linked representations: parameter value and
    // ValueTree PARAM.value. Calling setValue() alone (the old fast path) does
    // NOT write the ValueTree; this caused e.g. global BYPASS to revert or fail
    // a subsequent Undo. Set the parameter first, then synchronise its tree.
    // Updating in that order avoids a duplicate host notification from JUCE's
    // ParameterAdapter, which already knows the new value.
    for (const auto child : restoredState)
    {
        if (!child.hasType("PARAM")) continue;
        const auto id = child.getProperty("id").toString();
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(parameters.getParameter(id));
        const auto value = ava::modules::state::readParameterPlainValue(restoredState, id);
        if (parameter == nullptr || !value.has_value())
        {
            juce::Logger::writeToLog("AVA: missing or invalid parameter in snapshot: " + id);
            success = false;
            continue;
        }
        const auto normalised = parameter->convertTo0to1(*value);
        if (!juce::approximatelyEqual(parameter->getValue(), normalised))
            parameter->setValue(normalised);
        auto liveParam = parameters.state.getChildWithProperty("id", id);
        if (!liveParam.isValid())
        {
            juce::Logger::writeToLog("AVA: no live APVTS tree for " + id);
            success = false;
            continue;
        }
        if (liveParam.getProperty("value") != child.getProperty("value"))
            liveParam.setProperty("value", child.getProperty("value"), nullptr);
        if (std::abs(parameter->getValue() - normalised) > 1.0e-4f)
        {
            juce::Logger::writeToLog("AVA: failed to apply parameter: " + id);
            success = false;
        }
    }

    // Preserve the ValueTree's identity and all its child PARAM handles.
    // Remove obsolete metadata too, not just overwrite existing properties.
    for (int i = parameters.state.getNumProperties(); --i >= 0;)
    {
        const auto key = parameters.state.getPropertyName(i);
        if (!restoredState.hasProperty(key))
            parameters.state.removeProperty(key, nullptr);
    }
    for (int i = 0; i < restoredState.getNumProperties(); ++i)
    {
        const auto key = restoredState.getPropertyName(i);
        if (parameters.state.getProperty(key) != restoredState.getProperty(key))
            parameters.state.setProperty(key, restoredState.getProperty(key), nullptr);
    }

    const auto rangeCount = getCrossoverSettings().activeSplitCount + 1;
    const auto previousRange = selectedCrossoverRange.load(std::memory_order_relaxed);
    const auto nextRange = juce::jmin(restoredSelectedRange, rangeCount - 1);
    selectedCrossoverRange.store(nextRange, std::memory_order_relaxed);
    ensureActiveCrossoverRangeCount(rangeCount);
    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->setSelectedRange(nextRange);
    graphChanged |= previousRange != nextRange;

    // Update cached band order/visibility metadata independently of the
    // process graph; these are not APVTS parameters.
    for (size_t band = 0; band < bandModuleOrders.size(); ++band)
    {
        auto order = module_order::defaultOrder;
        const auto key = module_order::bandStateKey(band);
        if (restoredState.hasProperty(key))
            module_order::parse(restoredState.getProperty(key).toString(), order);
        const auto packed = module_order::pack(order);
        graphChanged |= bandModuleOrders[band].exchange(packed, std::memory_order_acq_rel) != packed;
        unsigned mask = 0;
        for (int index = 0; index < 2; ++index)
        {
            const auto type = index == 0 ? module_order::eql : module_order::dyn;
            const bool defaultHidden = (module_order::defaultHiddenMask & (1u << index)) != 0;
            if (bool(restoredState.getProperty(module_order::bandHiddenKey(band, type), defaultHidden)))
                mask |= 1u << index;
        }
        graphChanged |= bandHiddenMasks[band].exchange(mask, std::memory_order_acq_rel) != mask;
    }

    const auto applyModule = [&] (const char* key, auto* module, const char* name)
    {
        const auto encoded = restoredState.getProperty(key).toString();
        if (encoded.isEmpty() || encoded == currentState.getProperty(key).toString()) return;
        modulesChanged = graphChanged = true;
        if (!restoreBinaryModuleState(module, encoded))
        {
            juce::Logger::writeToLog(juce::String("AVA: failed restoring ") + name);
            success = false;
        }
    };
    applyModule(blockChainModuleStateKey, getBlockChainProcessor(), "CHAIN");
    applyModule(dynModuleStateKey, getDynModuleProcessor(), "DYN");
    applyModule(eqlModuleStateKey, getEqlProcessorBank(), "EQL");

    if (restoredState.getProperty(moduleStackStateKey) != currentState.getProperty(moduleStackStateKey))
    {
        graphChanged = modulesChanged = true;
        const auto beforeStacks = moduleStacks;
        const auto beforeCopies = moduleCopies;
        if (!restoreModuleCopies(restoredState))
        {
            juce::Logger::writeToLog("AVA: failed to reconcile module copies without resetting DSP");
            success = false;
        }
        else
        {
            for (size_t band = 0; band < moduleStacks.size(); ++band)
            {
                const auto& oldOrder = beforeStacks[band];
                const auto& newOrder = moduleStacks[band];
                if (oldOrder.size() != newOrder.size())
                {
                    structuralChanged = true;
                    break;
                }
                for (size_t index = 0; index < oldOrder.size(); ++index)
                    if (oldOrder[index].uid != newOrder[index].uid
                        || oldOrder[index].type != newOrder[index].type
                        || oldOrder[index].created != newOrder[index].created)
                    {
                        structuralChanged = true;
                        break;
                    }
            }
            if (beforeCopies.size() != moduleCopies.size())
                structuralChanged = true;
            else
                for (const auto& [uid, copy] : moduleCopies)
                    if (auto it = beforeCopies.find(uid);
                        it == beforeCopies.end() || it->second != copy)
                        structuralChanged = true;
        }
    }
    setActiveModule(restoredActiveModule);
    refreshModuleBypasses();

    if (routingRuntime != nullptr)
    {
        // Reuse processor handles for all unchanged ROUTING INSTANCE IDs.
        // Synchronization only creates newly inserted nodes or drops removed
        // nodes; it never tears down and reconstructs the whole UNIT.
        if (ava::routing::readState(currentState) != ava::routing::readState(restoredState))
        {
            auto nextRuntime = std::make_shared<ava::routing::Runtime>(*this, routingRuntime.get());
            nextRuntime->synchronize(parameters.state);
            if (currentSampleRate > 0.0)
                nextRuntime->prepare(currentSampleRate, preparedBlockSize, routingChannelCount);
            routingRuntime = std::move(nextRuntime);
            structuralChanged = graphChanged = true;
        }
        if (restoredState.getProperty(ava::routing::processorStatesKey)
            != currentState.getProperty(ava::routing::processorStatesKey))
        {
            graphChanged = true;
            if (!routingRuntime->restoreProcessorStates(restoredState))
            {
                juce::Logger::writeToLog("AVA: failed to restore ROUTING INSTANCE values");
                success = false;
            }
        }
    }

    if (modulesChanged)
        registerLoadedModuleStateListeners();
    if (graphChanged)
        publishRenderGraph(structuralChanged);
    if (structuralChanged)
        if (auto* editor = getContentEditor())
            editor->requestStructureRestoreResync();
    refreshHostSlotTargets();
    // setValue() during state restoration runs with notifications suppressed.
    // REAPER is not required to resend unchanged envelope values afterwards;
    // apply the restored exposed slot values on the next safe sync instead.
    if (!routingInstance) queueCurrentHostSlotValues();
    if (restoredABCompareState.has_value())
        shell_state_serialization::restoreABCompareState(*this, *restoredABCompareState);
    updateShellLatency();
    if (!preserveLiveView)
        setLastEditorSize(static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0)),
                          static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0)));
    refreshOscConfiguration();
    triggerAsyncUpdate();
    if (!success)
        juce::Logger::writeToLog("AVA: incremental snapshot restore incomplete; no destructive fallback was performed.");
    return success;

}

bool AvaAudioProcessor::applyStateInformationForABCompare(const void* data, const int sizeInBytes)
{
    const auto previousLatencyLocked = abCompareLatencyLocked.load(std::memory_order_acquire);
    const auto previousLatencyFloor = abCompareLatencyFloorSamples.load(std::memory_order_acquire);
    const auto reportedLatencySamples = getLatencySamples();

    abCompareLatencyFloorSamples.store(juce::jmax(previousLatencyFloor, reportedLatencySamples),
                                       std::memory_order_release);
    abCompareLatencyLocked.store(true, std::memory_order_release);

    const auto restored = restoreStateInformation(data, sizeInBytes, false, false);

    if (! restored)
    {
        abCompareLatencyFloorSamples.store(previousLatencyFloor, std::memory_order_release);
        abCompareLatencyLocked.store(previousLatencyLocked, std::memory_order_release);
        updateShellLatency();
    }

    return restored;
}
