#include "Processor.h"
#include "Editor.h"
#include "HostChangeDetails.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../routing/Runtime.h"
#include "EditorPreferences.h"
#include "EditorStateSchema.h"
#include "ModuleCopyTargets.h"
#include "../crossover/UiStateSchema.h"
#include "../crossover/ParameterAccess.h"
#include "../modules/eql/Processor.h"

#include <algorithm>

const char* AvaAudioProcessor::stateIdForModule(const ActiveModule module) noexcept
{
    switch (module)
    {
        case ActiveModule::chain: return blockChainModuleId;
        case ActiveModule::eql: return eqlModuleId;
        case ActiveModule::dyn: return dynModuleId;
        case ActiveModule::none: break;
    }

    return "";
}

AvaAudioProcessor::ActiveModule AvaAudioProcessor::moduleFromStateId(const juce::String& moduleId)
{
    if (moduleId == eqlModuleId)
        return ActiveModule::eql;

    if (moduleId == blockChainModuleId)
        return ActiveModule::chain;

    if (moduleId == dynModuleId)
        return ActiveModule::dyn;

    return ActiveModule::none;
}

juce::AudioProcessorValueTreeState& AvaAudioProcessor::getValueTreeState() noexcept
{
    return parameters;
}

const juce::AudioProcessorValueTreeState& AvaAudioProcessor::getValueTreeState() const noexcept
{
    return parameters;
}

AvaAudioProcessor::ActiveModule AvaAudioProcessor::getActiveModule() const noexcept
{
    return activeModule.load(std::memory_order_acquire);
}

void AvaAudioProcessor::setActiveModule(const ActiveModule module)
{
    activeModule.store(module, std::memory_order_release);

    const auto activeId = juce::String(stateIdForModule(module));

    if (activeId.isNotEmpty())
        parameters.state.setProperty(activeModuleStateKey, activeId, nullptr);
    else
        parameters.state.removeProperty(activeModuleStateKey, nullptr);
}

bool AvaAudioProcessor::loadModule(const ActiveModule module)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);

    if (module == ActiveModule::none)
        return false;

    const auto alreadyLoaded = module == ActiveModule::chain ? blockChainModuleProcessor != nullptr
        : module == ActiveModule::dyn ? dynModuleProcessor != nullptr : eqlProcessorBank != nullptr;
    if (alreadyLoaded)
    {
        setActiveModule(module);
        notifyHostOfStateChange();
        return true;
    }


    clearLoadedModuleStateListeners();
    const auto wasProcessingPrepared = processingPrepared.load(std::memory_order_acquire);
    const auto restoreProcessingPrepared = [this, wasProcessingPrepared]
    {
        if (wasProcessingPrepared && currentSampleRate > 0.0)
            processingPrepared.store(true, std::memory_order_release);
    };

    if (! createModuleInstance(module))
    {
        restoreProcessingPrepared();
        return false;
    }

    if (module == ActiveModule::eql)
        if (auto* eql = getEqlProcessorBank())
            eql->loadInitialFilterPreset();

    setActiveModule(module);
    publishRenderGraph();
    registerLoadedModuleStateListeners();
    refreshHostSlotTargets();
    updateShellLatency();
    notifyHostOfStateChange();
    restoreProcessingPrepared();
    return true;
}

void AvaAudioProcessor::registerLoadedModuleStateListeners()
{
    clearLoadedModuleStateListeners();
    const auto observeModule = [this](auto* processor)
    {
        if (processor == nullptr) return;
        ObservedModuleState observed;
        observed.parameters = &processor->getValueTreeState();
        observed.state = observed.parameters->state;
        for (auto* parameter : observed.parameters->processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            {
                const auto id = ranged->getParameterID();
                observed.parameters->addParameterListener(id, this);
                observed.parameterIds.push_back(id);
            }
        observed.state.addListener(this);
        observedModuleStates.push_back(std::move(observed));
    };
    observeModule(getBlockChainProcessor());
    observeModule(getDynModuleProcessor());
    for (const auto& [uid, copy] : moduleCopies) {
        observeModule(copy->blocks.get());
        observeModule(copy->dyn.get());
        if (copy->eql)
            for (size_t band = 0; band < copy->eql->getCreatedRangeCount(); ++band)
            {
                auto* processor = copy->eql->getProcessor(band);
                if (processor == nullptr) continue;
                // Every copied EQL has its own filter count. It must publish
                // an updated render graph just like the original EQL when
                // filters are added, removed, pasted or restored. Otherwise
                // the host can continue rendering the old filter limit.
                processor->onLayoutChanged = [this]
                {
                    publishRenderGraph();
                    notifyHostOfStateChange();
                };
                processor->onFilterRemapped = [this, uid, band](const auto& mapping)
                {
                    remapEqlFilterTargets(band, mapping, uid);
                };
                observeModule(processor);
            }
    }
    if (eqlProcessorBank != nullptr)
        for (size_t range = 0; range < eqlProcessorBank->getCreatedRangeCount(); ++range)
        {
            auto* processor = eqlProcessorBank->getProcessor(range);
            processor->onLayoutChanged = [this] { publishRenderGraph(); notifyHostOfStateChange(); };
            processor->onFilterRemapped = [this, range](const auto& mapping) { remapEqlFilterTargets(range, mapping); };
            observeModule(processor);
        }
}

void AvaAudioProcessor::clearLoadedModuleStateListeners()
{
    for (auto& observed : observedModuleStates)
    {
        for (const auto& id : observed.parameterIds)
            observed.parameters->removeParameterListener(id, this);
        observed.state.removeListener(this);
    }
    observedModuleStates.clear();
}

void AvaAudioProcessor::parameterChanged(const juce::String& parameterID, const float newValue)
{
    for (int slotIndex = 0; slotIndex < assignableHostSlotCount; ++slotIndex)
    {
        if (parameterID == hostSlotParameterIds[static_cast<size_t>(slotIndex)])
        {
            if (!routingInstance && !suppressHostStateNotifications.load(std::memory_order_relaxed))
                applyHostSlotValue(slotIndex, newValue);
            // The exposed host slot is already a JUCE parameter. Informing
            // the host about non-parameter state here produces an additional
            // Undo event on top of the REAPER envelope-point edit.
            getRoutingOwner().stateRevision.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    const auto activeSplits = getCrossoverSettings().activeSplitCount;
    for (size_t index = 0; index < activeSplits; ++index)
    {
        const auto id = getCrossoverParameterId(("split-" + juce::String(int(index + 1))).toRawUTF8());
        if (parameterID != id && parameterID != paramCrossoverActiveSplitCountId) continue;
        if (auto* split = parameters.getParameter(id))
        {
            const auto bounds = ava::crossover::parameters::splitFrequencyBounds(parameters, index,
                activeSplits, [] (const char* suffix) { return getCrossoverParameterId(suffix); });
            const auto current = split->convertFrom0to1(split->getValue());
            const auto constrained = bounds.clipValue(current);
            if (std::abs(current - constrained) > 1.0e-6f)
                split->setValueNotifyingHost(split->convertTo0to1(constrained));
        }
    }
    const auto autoSolo = bool(parameters.state.getProperty(
        crossover_ui::makeStatePropertyId("crossover", "auto_solo_enabled"), false));
    for (size_t band = 0; band < crossoverSoloParams.size(); ++band)
        if (parameterID == getCrossoverSoloParameterId(band) && !autoSolo
            && !suppressHostStateNotifications.load(std::memory_order_relaxed))
        {
            const auto inclusive = bool(parameters.state.getProperty(
                crossover_ui::makeStatePropertyId("crossover", "manual_solo_inclusive"), false));
            if (newValue >= 0.5f && !inclusive)
                for (size_t other = 0; other < crossoverSoloParams.size(); ++other)
                    if (other != band)
                        if (auto* peer = parameters.getParameter(getCrossoverSoloParameterId(other));
                            peer != nullptr && peer->getValue() >= 0.5f)
                            peer->setValueNotifyingHost(0.0f);
            if (!manualSoloStatePending.exchange(true, std::memory_order_acq_rel))
                triggerAsyncUpdate();
            break;
        }

    // Child BP controls an instance; the owner's BP is the global plugin bypass
    // and must remain available while another instance is soloed.
    if (parameterID == paramGlobalBypassId && routingInstance && newValue < 0.5f
        && acceptedInstanceBypassed.load(std::memory_order_relaxed)
        && !suppressHostStateNotifications.load(std::memory_order_relaxed) && isInstanceSoloBypassed())
    {
        if (auto* bypass = parameters.getParameter(paramGlobalBypassId))
            bypass->setValueNotifyingHost(1.0f);
        return;
    }

    if (parameterID == paramGlobalBypassId)
        acceptedInstanceBypassed.store(newValue >= 0.5f, std::memory_order_relaxed);

    if (parameterID == paramSoloInstanceId || parameterID == paramSoloChannelId)
    {
        auto& owner = getRoutingOwner();
        const juce::ScopedLock lock(owner.processingLock);
        if (newValue >= 0.5f && owner.routingRuntime != nullptr)
            owner.routingRuntime->clearOtherSolo(*this, parameterID == paramSoloChannelId);
        owner.triggerAsyncUpdate();
    }

    if (parameterID == paramCrossoverActiveSplitCountId)
    {
        const auto splitCount = juce::jlimit(0,
                                             static_cast<int>(ava::crossover::BufferRouter::numSplits),
                                             juce::roundToInt(newValue));
        requestedCrossoverRangeCount.store(static_cast<size_t>(splitCount + 1), std::memory_order_release);
        triggerAsyncUpdate();
    }

    // Shell APVTS parameters are host-facing: normal JUCE automation already
    // records their edits. Only a child module's parameters are stored as
    // non-parameter chunk state, and need explicit host notification.
    if (!routingInstance && parameters.getParameter(parameterID) != nullptr)
        stateRevision.fetch_add(1, std::memory_order_relaxed);
    else
        notifyHostOfStateChange();
}

void AvaAudioProcessor::handleAsyncUpdate()
{
    { const juce::ScopedLock lock(processingLock); collectRetiredRenderGraphs(); }
    if (manualSoloStatePending.exchange(false, std::memory_order_acq_rel)
        && !bool(parameters.state.getProperty(crossover_ui::makeStatePropertyId("crossover", "auto_solo_enabled"), false)))
        for (size_t band = 0; band < crossoverSoloParams.size(); ++band)
            parameters.state.setProperty(crossover_ui::makeStatePropertyId(
                "crossover", "manual_solo." + juce::String(int(band))),
                crossoverSoloParams[band] != nullptr && crossoverSoloParams[band]->load(std::memory_order_relaxed) >= 0.5f, nullptr);
    ensureActiveCrossoverRangeCount(requestedCrossoverRangeCount.load(std::memory_order_acquire));
    if (routingRuntime != nullptr)
    {
        const juce::ScopedLock lock(processingLock);
        if (routingRuntime->needsDelayCapacity()) {
            auto next = std::make_shared<ava::routing::Runtime>(*this, routingRuntime.get());
            next->synchronize(parameters.state);
            if (currentSampleRate > 0.0) next->prepare(currentSampleRate, preparedBlockSize, routingChannelCount);
            routingRuntime = std::move(next);
            publishRenderGraph();
        }
        refreshHostSlotTargets();
        updateShellLatency();
    }
    syncHostSlotValues();
    applyPendingShellUpdates();
}

void AvaAudioProcessor::applyPendingShellUpdates()
{
    const auto requestedLatency = requestedLatencySamples.load(std::memory_order_acquire);

    if (getLatencySamples() != requestedLatency)
        setLatencySamples(requestedLatency);

    flushHostStateNotification();
}

void AvaAudioProcessor::flushHostStateNotification()
{
    // Keep one host-facing dispatch point for both asynchronous edits and
    // completed mouse gestures. Host Undo grouping is still DAW-dependent.
    if (pendingHostStateNotification.exchange(false, std::memory_order_acq_rel)
        && ! suppressHostStateNotifications.load(std::memory_order_relaxed))
        updateHostDisplay(makeHostStateChangeDetails());
}

void AvaAudioProcessor::ensureActiveCrossoverRangeCount(const size_t rangeCount)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const auto constrainedRangeCount = std::clamp(rangeCount, size_t { 1 }, EqlProcessorBank::numRanges);
    const juce::ScopedLock lock(processingLock);
    const auto selected = juce::jmin(selectedCrossoverRange.load(std::memory_order_relaxed), constrainedRangeCount - 1);
    if (selectedCrossoverRange.exchange(selected, std::memory_order_relaxed) != selected)
    {
        if (eqlProcessorBank != nullptr) eqlProcessorBank->setSelectedRange(selected);
        refreshHostSlotTargets();
    }
    const auto visibleKey = crossover_ui::makeStatePropertyId("crossover", "visible_band_index");
    if (parameters.state.hasProperty(visibleKey))
        parameters.state.setProperty(visibleKey, static_cast<int>(selected), nullptr);
    const auto eqlNeedsRange = eqlProcessorBank != nullptr
        && eqlProcessorBank->getCreatedRangeCount() < constrainedRangeCount;
    const auto blockChainNeedsRange = blockChainModuleProcessor != nullptr
        && blockChainModuleProcessor->getCreatedRangeCount() < constrainedRangeCount;
    const auto dynNeedsRange = dynModuleProcessor != nullptr
        && dynModuleProcessor->getCreatedRangeCount() < constrainedRangeCount;

    if (! eqlNeedsRange && ! blockChainNeedsRange && ! dynNeedsRange)
        return;


    const auto wasProcessingPrepared = processingPrepared.load(std::memory_order_acquire);

    if (eqlNeedsRange)
        eqlProcessorBank->ensureRangeCount(constrainedRangeCount);

    if (blockChainNeedsRange)
        blockChainModuleProcessor->ensureRangeCount(constrainedRangeCount);

    if (dynNeedsRange)
        dynModuleProcessor->ensureRangeCount(constrainedRangeCount);

    registerLoadedModuleStateListeners();
    publishRenderGraph();
    updateShellLatency();

    if (wasProcessingPrepared && currentSampleRate > 0.0)
        processingPrepared.store(true, std::memory_order_release);
}

void AvaAudioProcessor::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree == parameters.state)
        for (const auto& module : module_bypass::descriptions)
            if (property == juce::Identifier(module.bypassStateKey)) { refreshModuleBypasses(); break; }
    // UI metadata is serialized alongside audio parameters. Inform the host
    // about those changes too, or reopening the plugin can restore an older
    // snapshot even though the APVTS currently contains the newer UI state.
    // isEditorViewProperty still excludes navigation/collapse from audio undo.
    if (!tree.hasType("PARAM"))
    {
        if (isEditorViewProperty(property)) notifyHostOfViewChange();
        else notifyHostOfStateChange();
    }
}

void AvaAudioProcessor::valueTreeRedirected(juce::ValueTree&)
{
    refreshModuleBypasses();
    notifyHostOfStateChange();
}

BlockChainProcessor* AvaAudioProcessor::getBlockChainProcessor() noexcept
{
    return renderingOwner == this && renderingGraph != nullptr ? renderingGraph->blocks.get() : blockChainModuleProcessor.get();
}

const BlockChainProcessor* AvaAudioProcessor::getBlockChainProcessor() const noexcept
{
    return renderingOwner == this && renderingGraph != nullptr ? renderingGraph->blocks.get() : blockChainModuleProcessor.get();
}

DynModuleProcessor* AvaAudioProcessor::getDynModuleProcessor() noexcept
{
    return renderingOwner == this && renderingGraph != nullptr ? renderingGraph->dyn.get() : dynModuleProcessor.get();
}

const DynModuleProcessor* AvaAudioProcessor::getDynModuleProcessor() const noexcept
{
    return renderingOwner == this && renderingGraph != nullptr ? renderingGraph->dyn.get() : dynModuleProcessor.get();
}

bool AvaAudioProcessor::createModuleInstance(const ActiveModule module)
{
    const auto requiredRangeCount = getCrossoverSettings().activeSplitCount + 1;
    const auto selectedRange = juce::jmin(selectedCrossoverRange.load(std::memory_order_relaxed),
                                          requiredRangeCount - 1);
    selectedCrossoverRange.store(selectedRange, std::memory_order_relaxed);

    auto prepareModule = [this] (auto& processor)
    {
        if (currentSampleRate > 0.0 && preparedBlockSize > 0)
            processor->prepareToPlay(currentSampleRate, preparedBlockSize);

        return true;
    };

    switch (module)
    {
        case ActiveModule::eql:
            eqlProcessorBank = std::make_shared<EqlProcessorBank>();
            eqlProcessorBank->ensureRangeCount(requiredRangeCount);
            eqlProcessorBank->setSelectedRange(selectedRange);
            return prepareModule(eqlProcessorBank);

        case ActiveModule::chain:
            blockChainModuleProcessor = std::make_shared<BlockChainProcessor>(*this);
            blockChainModuleProcessor->ensureRangeCount(requiredRangeCount);
            return prepareModule(blockChainModuleProcessor);

        case ActiveModule::dyn:
            dynModuleProcessor = std::make_shared<DynModuleProcessor>(*this);
            dynModuleProcessor->ensureRangeCount(requiredRangeCount);
            return prepareModule(dynModuleProcessor);

        case ActiveModule::none:
            break;
    }

    return false;
}

EqlModuleProcessor* AvaAudioProcessor::getEqlModuleProcessor() noexcept
{
    auto* bank = getEqlProcessorBank();
    return bank != nullptr ? bank->getSelectedProcessor() : nullptr;
}

const EqlModuleProcessor* AvaAudioProcessor::getEqlModuleProcessor() const noexcept
{
    auto* bank = getEqlProcessorBank();
    return bank != nullptr ? bank->getSelectedProcessor() : nullptr;
}

EqlProcessorBank* AvaAudioProcessor::getEqlProcessorBank() noexcept
{
    return renderingOwner == this && renderingGraph != nullptr ? renderingGraph->eql.get() : eqlProcessorBank.get();
}

const EqlProcessorBank* AvaAudioProcessor::getEqlProcessorBank() const noexcept
{
    return renderingOwner == this && renderingGraph != nullptr ? renderingGraph->eql.get() : eqlProcessorBank.get();
}

void AvaAudioProcessor::setSelectedCrossoverRange(const size_t rangeIndex)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const auto activeRangeCount = static_cast<size_t>(getCrossoverSettings().activeSplitCount + 1);
    const auto selectedRange = juce::jmin(rangeIndex, activeRangeCount - 1);
    selectedCrossoverRange.store(selectedRange, std::memory_order_relaxed);
    if (const auto it=moduleCopies.find(selectedModuleCopy.load());
        it!=moduleCopies.end() && size_t(it->second->range)!=selectedRange)
        selectedModuleCopy.store(-1);
    parameters.state.setProperty(crossover_ui::makeStatePropertyId("crossover", "visible_band_index"),
                                 static_cast<int>(selectedRange), nullptr);
    ensureActiveCrossoverRangeCount(activeRangeCount);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->setSelectedRange(selectedRange);

    registerLoadedModuleStateListeners();
    refreshHostSlotTargets();
}

size_t AvaAudioProcessor::getSelectedCrossoverRange() const noexcept
{
    return selectedCrossoverRange.load(std::memory_order_relaxed);
}

module_order::Order AvaAudioProcessor::getModuleOrderForRange(size_t rangeIndex) const noexcept
{
    if (rangeIndex >= bandModuleOrders.size()) return module_order::defaultOrder;
    return module_order::unpack(bandModuleOrders[rangeIndex].load(std::memory_order_acquire));
}

module_order::Order AvaAudioProcessor::getModuleOrder() const noexcept
{
    return getModuleOrderForRange(getSelectedCrossoverRange());
}

void AvaAudioProcessor::setModuleOrder(const module_order::Order& order)
{
    if (!module_order::valid(order)) return;
    const auto range = juce::jmin(getSelectedCrossoverRange(), bandModuleOrders.size() - 1);
    if (getModuleOrderForRange(range) == order) return;
    bandModuleOrders[range].store(module_order::pack(order), std::memory_order_release);
    parameters.state.setProperty(module_order::bandStateKey(range), module_order::serialise(order), nullptr);
    notifyHostOfStateChange();
}

bool AvaAudioProcessor::isModuleHiddenForRange(size_t rangeIndex, int moduleId) const noexcept
{
    if (rangeIndex >= bandHiddenMasks.size()) return false;
    if (moduleId != module_order::eql && moduleId != module_order::dyn) return false;
    return (bandHiddenMasks[rangeIndex].load(std::memory_order_acquire)
            & (moduleId == module_order::eql ? 1u : 2u)) != 0;
}

juce::RangedAudioParameter* AvaAudioProcessor::getBandModuleVisibilityParameter(size_t rangeIndex, int moduleId) const noexcept
{
    if (rangeIndex >= bandVisibilityParameters.size()) return nullptr;
    if (moduleId != module_order::eql && moduleId != module_order::dyn) return nullptr;
    return bandVisibilityParameters[rangeIndex][moduleId == module_order::eql ? 0u : 1u].get();
}

void AvaAudioProcessor::setModuleHiddenForRange(size_t rangeIndex, int moduleId, bool hidden)
{
    if (rangeIndex >= bandHiddenMasks.size()) return;
    if (moduleId != module_order::eql && moduleId != module_order::dyn) return;
    const auto bit = moduleId == module_order::eql ? 1u : 2u;
    const auto oldMask = bandHiddenMasks[rangeIndex].load(std::memory_order_acquire);
    const auto newMask = hidden ? oldMask | bit : oldMask & ~bit;
    if (newMask == oldMask) return;
    bandHiddenMasks[rangeIndex].store(newMask, std::memory_order_release);
    parameters.state.setProperty(module_order::bandHiddenKey(rangeIndex, moduleId), hidden, nullptr);
    updateShellLatency();
    notifyHostOfStateChange();
}

unsigned AvaAudioProcessor::getModuleBypassMask() const noexcept
{
    if (renderingOwner == this && renderingGraph != nullptr && renderingGraph->buffers != nullptr) return audioBuffers().bypassMask;
    // Per-band EQL/DYN availability is independent of module bypass.
    return moduleBypasses.load(std::memory_order_acquire);
}
void AvaAudioProcessor::refreshModuleBypasses()
{
    unsigned mask = 0;
    for (const auto& module : module_bypass::descriptions)
        if (bool(parameters.state.getProperty(module.bypassStateKey, false))) mask |= 1u << module.id;
    const auto changed = moduleBypasses.exchange(mask, std::memory_order_acq_rel) ^ mask;
    for (const auto& module : module_bypass::descriptions)
        if ((changed & (1u << module.id)) != 0)
            if (auto* target = moduleBypassTargets[size_t(module.id)].get())
                target->sendValueChangedMessageToListeners(target->getValue());
}
void AvaAudioProcessor::setModuleBypassed(int id, bool bypassed)
{
    if (!juce::isPositiveAndBelow(id, int(module_bypass::descriptions.size()))) return;
    parameters.state.setProperty(module_bypass::descriptions[size_t(id)].bypassStateKey, bypassed, nullptr);
}

void AvaAudioProcessor::remapEqlFilterTargets(size_t band, const std::array<int, 64>& mapping, int copyUid)
{
    auto& owner = getRoutingOwner();
    const juce::ScopedLock lock(owner.processingLock);
    owner.invalidateHostSlotTargets();
    const auto prefix = "band-" + juce::String(int(band) + 1) + "_filter-";
    const auto remap = [&](const juce::String& id) -> juce::String {
        auto parameter = id;
        int uid = -1;
        juce::String suffixLocal;
        const auto copied = ava::module_copy_targets::parse(id, uid, suffixLocal);
        if (copied)
        {
            if (uid != copyUid) return id;
            parameter = suffixLocal;
        }
        else if (copyUid >= ava::module_stack::originalCount) return id;
        if (!parameter.startsWith(prefix)) return id;
        const auto suffix = parameter.substring(prefix.length());
        const auto separator = suffix.indexOfChar('_');
        if (separator < 1) return id;
        const auto index = suffix.substring(0, separator).getIntValue() - 1;
        if (!juce::isPositiveAndBelow(index, int(mapping.size()))) return id;
        const auto destination = mapping[size_t(index)];
        if (destination < 0) return {};
        const auto changed = prefix + juce::String(destination + 1) + suffix.substring(separator);
        return copied ? ava::module_copy_targets::make(uid, changed) : changed;
    };
    for (int slot = 0; slot < assignableHostSlotCount; ++slot) {
        const auto key = getHostSlotTargetStateKey(slot);
        const auto assigned = owner.parameters.state.getProperty(key).toString();
        int instance = 0; juce::String local;
        const auto qualified = ava::routing::parseParameterTarget(assigned, instance, local);
        if (qualified && instance != getRoutingInstanceId()) continue;
        if (!qualified && routingInstance) continue;
        const auto target = remap(qualified ? local : assigned);
        if (target.isEmpty()) owner.clearHostSlotAssignment(slot);
        else owner.parameters.state.setProperty(key,
            qualified ? ava::routing::parameterTargetId(instance, target) : target, nullptr);
    }
    const auto selectionKey = [](const juce::String& id) {
        const auto identity = "parameter:" + id;
        return shell_editor_ui::key("selected." + juce::String::toHexString(
            identity.toRawUTF8(), int(identity.getNumBytesAsUTF8()), 0));
    };
    juce::StringArray selected;
    if (copyUid < ava::module_stack::originalCount)
    for (int i = 0; i < int(mapping.size()); ++i)
        for (const auto* suffix : {"type", "phase", "freq", "bw", "order", "gain", "bp"}) {
            const auto id = prefix + juce::String(i + 1) + "_" + suffix;
            const auto key = selectionKey(id);
            if (bool(parameters.state.getProperty(key, false))) selected.add(remap(id));
            parameters.state.removeProperty(key, nullptr);
        }
    for (const auto& id : selected) if (id.isNotEmpty()) parameters.state.setProperty(selectionKey(id), true, nullptr);
    owner.refreshHostSlotTargets();
    notifyHostOfStateChange();
}
