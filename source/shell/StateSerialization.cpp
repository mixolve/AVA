#include "Processor.h"
#include "EditorStateSchema.h"
#include "ABCompareSerialization.h"

#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../routing/Runtime.h"

namespace
{
template <typename Processor>
void storeBinaryModuleState(juce::ValueTree& state,
                            const juce::Identifier& property,
                            Processor* processor)
{
    if (processor == nullptr)
    {
        state.removeProperty(property, nullptr);
        return;
    }

    juce::MemoryBlock stateData;
    processor->getStateInformation(stateData);

    if (stateData.getSize() > 0)
        state.setProperty(property, stateData.toBase64Encoding(), nullptr);
    else
        state.removeProperty(property, nullptr);
}

}

void AvaAudioProcessor::removeModuleStateProperties(juce::ValueTree& state) const
{
    state.removeProperty(activeModuleStateKey, nullptr);
    state.removeProperty(eqlModuleStateKey, nullptr);
    state.removeProperty(blockChainModuleStateKey, nullptr);
    state.removeProperty(dynModuleStateKey, nullptr);
    state.removeProperty(moduleStackStateKey, nullptr);
    for (size_t range = 0; range < ava::crossover::BufferRouter::numRanges; ++range)
        state.removeProperty(getEditorFilterDisplayOrderStateKey(range), nullptr);
    // The slot-to-parameter assignment is independent persistent state. A
    // render-side snapshot may temporarily omit a module object; that must
    // not erase its mapping from a project or A/B snapshot. Real removals are
    // handled explicitly by deleteModuleSlot / clearHostSlotAssignment.
}

void AvaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // A host may request the chunk when the editor opens. Do not repeat the
    // expensive module/routing serialization if neither DSP nor view state
    // has changed since the last coherent host snapshot.
    {
        const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
        const juce::ScopedLock lock(processingLock);
        if (cachedHostStateXml != nullptr && !cachedHostStateData.isEmpty()
            && cachedHostAudioRevision == getStateRevision()
            && cachedHostViewRevision == getRoutingOwner().viewStateRevision.load(std::memory_order_relaxed))
        {
            destData = cachedHostStateData;
            return;
        }
    }
    writeStateInformation(destData, true);
}

void AvaAudioProcessor::getStateInformationForABCompareSnapshot(juce::MemoryBlock& destData)
{
    // Reuse the last exact host chunk when it is current. Serializing every
    // module again just because the editor was opened produces unnecessary
    // work on the host's message thread. Do not reuse a stale view snapshot.
    std::unique_ptr<juce::XmlElement> cachedXml;
    {
        const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
        const juce::ScopedLock lock(processingLock);
        if (cachedHostStateXml != nullptr
            && cachedHostAudioRevision == getStateRevision()
            && cachedHostViewRevision == getRoutingOwner().viewStateRevision.load(std::memory_order_relaxed))
            cachedXml = std::make_unique<juce::XmlElement>(*cachedHostStateXml);
    }
    if (cachedXml != nullptr)
    {
        auto state = juce::ValueTree::fromXml(*cachedXml);
        shell_state_serialization::removeABCompareStateProperties(state);
        if (auto snapshot = state.createXml())
        {
            copyXmlToBinary(*snapshot, destData);
            return;
        }
    }
    writeStateInformation(destData, false);
}

void AvaAudioProcessor::writeStateInformation(juce::MemoryBlock& destData, const bool includeABCompareState)
{
    const juce::ScopedLock ownerLock(getRoutingOwner().processingLock);
    const juce::ScopedLock lock(processingLock);
    // State capture must never create new DSP ranges or publish a new render
    // graph: doing so while REAPER opens the GUI starts an audio fade/warmup.
    // Range expansion is handled in prepareToPlay / the model-update path.
    const auto snapshotRevision = getStateRevision();
    const auto snapshotViewRevision = getRoutingOwner().viewStateRevision.load(std::memory_order_relaxed);
    const auto editorWidth = lastEditorWidth.load(std::memory_order_relaxed);
    const auto editorHeight = lastEditorHeight.load(std::memory_order_relaxed);
    auto state = parameters.copyState();
    // Do not prune host-slot assignments from serialized snapshots merely
    // because a temporary offline render model cannot resolve the target.
    // The host parameter IDs remain fixed and the model is re-bound on load.

    if (editorWidth > 0 && editorHeight > 0)
    {
        state.setProperty(editorWidthStateKey, editorWidth, nullptr);
        state.setProperty(editorHeightStateKey, editorHeight, nullptr);
    }

    // Keep all loaded module states even during a transient `none` selection.
    // Losing the CHAIN state here would also lose LISTEN and the other blocks.
    auto active = getActiveModule();
    if (active == ActiveModule::none)
    {
        if (getBlockChainProcessor() != nullptr) active = ActiveModule::chain;
        else if (getDynModuleProcessor() != nullptr) active = ActiveModule::dyn;
        else if (getEqlProcessorBank() != nullptr) active = ActiveModule::eql;
    }
    if (active != ActiveModule::none)
    {
        state.setProperty(activeModuleStateKey, stateIdForModule(active), nullptr);
        storeBinaryModuleState(state, eqlModuleStateKey, getEqlProcessorBank());
        storeBinaryModuleState(state, blockChainModuleStateKey, getBlockChainProcessor());
        storeBinaryModuleState(state, dynModuleStateKey, getDynModuleProcessor());
        saveModuleCopies(state);
    }
    else
        removeModuleStateProperties(state);

    if (includeABCompareState)
        shell_state_serialization::storeABCompareState(state, *this);
    else
        shell_state_serialization::removeABCompareStateProperties(state);

    if (routingRuntime != nullptr)
        routingRuntime->writeProcessorStates(state);

    if (auto stateXml = state.createXml())
    {
        copyXmlToBinary(*stateXml, destData);
        if (includeABCompareState && !routingInstance && getStateRevision() == snapshotRevision
            && viewStateRevision.load(std::memory_order_relaxed) == snapshotViewRevision)
        {
            cachedHostStateXml = std::make_unique<juce::XmlElement>(*stateXml);
            cachedHostStateData = destData;
            cachedHostAudioRevision = snapshotRevision;
            cachedHostViewRevision = snapshotViewRevision;
        }
    }
}

void AvaAudioProcessor::setLastEditorSize(const int width, const int height) noexcept
{
    lastEditorWidth.store(juce::jmax(0, width), std::memory_order_relaxed);
    lastEditorHeight.store(juce::jmax(0, height), std::memory_order_relaxed);
}

// All non-parameter host state notifications are batched and sent by the
// root processor on its asynchronous update, never from an individual module.
void AvaAudioProcessor::queueHostStateNotification()
{
    if (activeHostStateEditId.load(std::memory_order_acquire) != 0)
    {
        activeHostStateEditDirty.store(true, std::memory_order_release);
        // A state change racing with mouse-up must not be discarded after
        // endHostStateEdit has already consumed the dirty flag.
        if (activeHostStateEditId.load(std::memory_order_acquire) == 0)
        {
            if (!pendingHostStateNotification.exchange(true, std::memory_order_acq_rel))
                triggerAsyncUpdate();
        }
        return;
    }
    if (!pendingHostStateNotification.exchange(true, std::memory_order_acq_rel))
        triggerAsyncUpdate();
}

uint64_t AvaAudioProcessor::beginHostStateEdit()
{
    auto& owner = getRoutingOwner();
    // Finish an older gesture if another mouse-down arrives before the
    // previous mouse-up's deferred completion. Do not merge the two actions.
    const auto activeId = owner.activeHostStateEditId.load(std::memory_order_acquire);
    if (activeId != 0)
        owner.endHostStateEdit(activeId);

    // Unrelated asynchronous edits must not be swallowed by the new gesture.
    owner.flushHostStateNotification();
    const auto id = owner.nextHostStateEditId.fetch_add(1, std::memory_order_relaxed) + 1;
    owner.activeHostStateEditDirty.store(false, std::memory_order_release);
    owner.activeHostStateEditId.store(id, std::memory_order_release);
    return id;
}

void AvaAudioProcessor::endHostStateEdit(uint64_t gestureId)
{
    auto& owner = getRoutingOwner();
    if (gestureId == 0 || owner.activeHostStateEditId.load(std::memory_order_acquire) != gestureId)
        return;

    owner.activeHostStateEditId.store(0, std::memory_order_release);
    if (owner.activeHostStateEditDirty.exchange(false, std::memory_order_acq_rel))
    {
        // Mouse-up is on the message thread. Notify now, before the next
        // independent click can modify the snapshot that the host observes.
        owner.pendingHostStateNotification.store(true, std::memory_order_release);
        owner.flushHostStateNotification();
    }
}

void AvaAudioProcessor::notifyHostOfStateChange()
{
    if (suppressHostStateNotifications.load(std::memory_order_relaxed))
        return;

    // A host-slot change already has its own host automation event. Internal
    // module mirroring must invalidate the serialized-state cache, but must
    // not generate a second nonParameterStateChanged Undo entry.
    if (isApplyingHostSlotValue())
    {
        getRoutingOwner().stateRevision.fetch_add(1, std::memory_order_relaxed);
        if (parentStateChanged != nullptr)
            triggerAsyncUpdate();
        return;
    }

    stateRevision.fetch_add(1, std::memory_order_relaxed);
    if (parentStateChanged != nullptr)
    {
        // Routing children are not host-facing processors. Forward the state
        // change to their owner, but still update their own async DSP state.
        parentStateChanged();
        triggerAsyncUpdate();
        return;
    }

    queueHostStateNotification();
}

void AvaAudioProcessor::notifyHostOfViewChange()
{
    auto& owner = getRoutingOwner();
    if (owner.suppressHostStateNotifications.load(std::memory_order_relaxed))
        return;

    // Editor-only changes must never signal a host state edit: hosts can
    // interpret updateHostDisplay(nonParameterStateChanged) as a DAW undo step.
    // They are still serialised by getStateInformation when the host saves.
    owner.viewStateRevision.fetch_add(1, std::memory_order_relaxed);
}
