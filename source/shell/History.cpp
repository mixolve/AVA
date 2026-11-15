#include "Editor.h"
#include "FilterSection.h"
#include "EditorPreferences.h"
#include "../modules/eql/RangeBank.h"

void AvaAudioProcessorEditor::scheduleHistorySnapshot(const bool notifyHost)
{
    if (suppressHistorySnapshots)
        return;

    // Keep AVA's own Undo snapshots for automation, but don't tell the DAW
    // that the host-driven parameter change is also a chunk-state edit.
    if (notifyHost && !audioProcessor.isApplyingHostSlotValue())
        audioProcessor.notifyHostOfStateChange();
    pendingHistorySnapshot.store(true, std::memory_order_relaxed);
    lastHistoryChangeTimeMs.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
}

void AvaAudioProcessorEditor::commitPendingHistorySnapshot(const bool force)
{
    if (audioProcessor.isRoutingInstance()) return;
    if (! pendingHistorySnapshot.load(std::memory_order_relaxed) || suppressHistorySnapshots)
        return;

    constexpr juce::uint32 snapshotDebounceMs = 300;
    const auto now = juce::Time::getMillisecondCounter();
    const auto lastChange = lastHistoryChangeTimeMs.load(std::memory_order_relaxed);

    if (! force && now - lastChange < snapshotDebounceMs)
        return;

    juce::MemoryBlock snapshot;
    audioProcessor.getStateInformationForABCompareSnapshot(snapshot);
    pendingHistorySnapshot.store(false, std::memory_order_relaxed);

    if (historySnapshotsMatchIgnoringViews(snapshot, committedHistorySnapshot))
    {
        committedHistorySnapshot = snapshot;
        audioProcessor.getHistoryState().committedRevision = audioProcessor.getStateRevision();
        updateUndoRedoButtons();
        return;
    }

    if (! committedHistorySnapshot.isEmpty())
    {
        undoHistory.push_back(committedHistorySnapshot);

        constexpr size_t maximumHistoryDepth = 128, maximumHistoryBytes = 64 * 1024 * 1024;
        size_t bytes = 0;
        for (const auto& item : undoHistory) bytes += item.getSize();
        while (undoHistory.size() > maximumHistoryDepth || (bytes > maximumHistoryBytes && undoHistory.size() > 1)) {
            bytes -= undoHistory.front().getSize();
            undoHistory.erase(undoHistory.begin());
        }
    }

    committedHistorySnapshot = snapshot;
    audioProcessor.getHistoryState().committedRevision = audioProcessor.getStateRevision();
    redoHistory.clear();
    updateUndoRedoButtons();
}

void AvaAudioProcessorEditor::recordPresetHistoryChange(const juce::MemoryBlock& before,
                                                        const juce::MemoryBlock& after,
                                                        const int instanceId,
                                                        const juce::MemoryBlock& eqlBefore,
                                                        const juce::MemoryBlock& eqlAfter)
{
    if (audioProcessor.isRoutingInstance())
    {
        if (auto* ownerEditor = audioProcessor.getRoutingOwner().getContentEditor())
            ownerEditor->recordPresetHistoryChange(before, after, instanceId, eqlBefore, eqlAfter);
        return;
    }

    if (before.isEmpty() || after.isEmpty() || eqlBefore.isEmpty() || eqlAfter.isEmpty()
        || instanceId <= 0 || eqlBefore == eqlAfter)
        return;

    pendingHistorySnapshot.store(false, std::memory_order_relaxed);

    // A preceding unrelated edit may still need its own history step.
    if (! committedHistorySnapshot.isEmpty()
        && ! historySnapshotsMatchIgnoringViews(committedHistorySnapshot, before))
        undoHistory.emplace_back(committedHistorySnapshot);

    // Preset switching is an EQL-bank operation. Save its exact state rather
    // than restoring every routing processor just to undo one selection.
    undoHistory.emplace_back(before, instanceId, eqlBefore);

    constexpr size_t maximumHistoryDepth = 128;
    constexpr size_t maximumHistoryBytes = 64 * 1024 * 1024;
    size_t bytes = 0;
    for (const auto& item : undoHistory) bytes += item.getSize();
    while (undoHistory.size() > maximumHistoryDepth
           || (bytes > maximumHistoryBytes && undoHistory.size() > 1))
    {
        bytes -= undoHistory.front().getSize();
        undoHistory.erase(undoHistory.begin());
    }

    committedHistorySnapshot = after;
    audioProcessor.getHistoryState().committedRevision = audioProcessor.getStateRevision();
    redoHistory.clear();
    observedStateRevision = audioProcessor.getStateRevision();
    updateUndoRedoButtons();
}

bool AvaAudioProcessorEditor::applyEqlPresetHistoryState(const int instanceId,
                                                          const juce::MemoryBlock& bankState,
                                                          juce::MemoryBlock& previousBank)
{
    if (instanceId <= 0 || bankState.isEmpty())
        return false;

    auto& owner = audioProcessor.getRoutingOwner();
    std::shared_ptr<AvaAudioProcessor> instanceHandle;
    AvaAudioProcessor* target = &owner;
    if (instanceId != owner.getRoutingInstanceId())
    {
        instanceHandle = owner.getRoutingInstanceHandle(instanceId);
        target = instanceHandle.get();
    }
    if (target == nullptr)
        return false;

    auto* eqlBank = target->getEqlProcessorBank();
    if (eqlBank == nullptr)
        return false;

    eqlBank->getStateInformation(previousBank);
    if (previousBank.isEmpty())
        return false;

    const ScopedAtomicFlag suppressHistory(suppressHistorySnapshots, true);
    if (! eqlBank->setStateInformation(bankState.getData(), static_cast<int>(bankState.getSize())))
        return false;

    // The processor and its attachments stay alive; only the EQL bank changes.
    // Refresh the open editor's filter layout and preset name immediately.
    auto* targetEditor = instanceId == owner.getRoutingInstanceId() ? this
        : (activeInstanceId == instanceId ? activeInstanceEditor.get() : nullptr);
    if (targetEditor != nullptr)
    {
        targetEditor->refreshModuleStateListeners();
        targetEditor->reloadFilterPresetFromProcessor();
        targetEditor->refreshFilterPresetList();
        targetEditor->updateSectionStates();
        targetEditor->resized();
    }

    target->notifyHostOfStateChange();
    owner.getStateInformationForABCompareSnapshot(committedHistorySnapshot);
    audioProcessor.getHistoryState().committedRevision = owner.getStateRevision();
    observedStateRevision = owner.getStateRevision();
    pendingHistorySnapshot.store(false, std::memory_order_relaxed);
    return true;
}

bool AvaAudioProcessorEditor::applyHistorySnapshot(const juce::MemoryBlock& snapshot)
{
    if (snapshot.isEmpty())
        return false;

    auto mergedStateXml = AvaAudioProcessor::getXmlFromBinary(snapshot.getData(), static_cast<int>(snapshot.getSize()));

    if (mergedStateXml == nullptr || ! mergedStateXml->hasTagName(valueTreeState.state.getType().toString()))
        return false;

    juce::MemoryBlock currentState;
    audioProcessor.getStateInformationForABCompareSnapshot(currentState);
    if (auto currentXml = AvaAudioProcessor::getXmlFromBinary(currentState.getData(), static_cast<int>(currentState.getSize())))
        preserveEditorViewState(*mergedStateXml, *currentXml);
    preserveEditorWindowState(*mergedStateXml, valueTreeState.state);

    juce::MemoryBlock mergedSnapshot;
    AvaAudioProcessor::copyXmlToBinary(*mergedStateXml, mergedSnapshot);

    const auto preservedFilterScrollY = filterViewport.getViewPositionY();
    auto* bypassParameter = valueTreeState.getParameter(AvaAudioProcessor::paramGlobalBypassId);
    const auto preservedBypassValue = bypassParameter != nullptr ? bypassParameter->getValue() : 0.0f;

    const ScopedAtomicFlag suppressHistory(suppressHistorySnapshots, true);
    pendingHistorySnapshot.store(false, std::memory_order_relaxed);

    // Restoring the owner's state reconstructs its routing processors. A child
    // editor must be destroyed *before* that happens, otherwise its parameter
    // attachments and references point to the old processor after Undo/Redo.
    const auto previouslyOpenInstance = activeInstanceId;
    if (activeInstanceEditor != nullptr)
    {
        activeInstanceEditor.reset();
        activeInstanceProcessor.reset();
        activeInstanceId = 0;
    }

    {
        // State replacement sends synchronous ValueTree callbacks before the old
        // module processor has been destroyed. Rebind only after the replacement
        // is complete, otherwise the editor can retain listeners to freed state.
        const juce::ScopedValueSetter<bool> suppressResync(suppressProcessorStateResync, true);
        detachModuleEditorBindings();

        if (! audioProcessor.applyHistoryStateInformation(mergedSnapshot.getData(),
                                                           static_cast<int>(mergedSnapshot.getSize())))
        {
            restoreEditorStateFromValueTree();
            updateSectionStates();
            resized();
            if (previouslyOpenInstance != 0)
                openRoutingInstance(previouslyOpenInstance);
            return false;
        }
    }

    if (bypassParameter != nullptr)
        bypassParameter->setValueNotifyingHost(preservedBypassValue);

    restoreEditorStateFromValueTree();
    refreshFilterPresetList();
    refreshEqlFilterSectionsFromProcessor();


    storeEditorStateToValueTree();
    updateEditorResizeLimits();
    updateSectionStates();
    resized();

    const auto filterMaxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
    filterViewport.setViewPosition(0, juce::jlimit(0, filterMaxOffset, preservedFilterScrollY));

    if (previouslyOpenInstance != 0)
        openRoutingInstance(previouslyOpenInstance);

    audioProcessor.getStateInformationForABCompareSnapshot(committedHistorySnapshot);
    audioProcessor.notifyHostOfStateChange();
    audioProcessor.getHistoryState().committedRevision = audioProcessor.getStateRevision();
    observedStateRevision = audioProcessor.getStateRevision();
    pendingHistorySnapshot.store(false, std::memory_order_relaxed);
    return true;
}

void AvaAudioProcessorEditor::refreshEqlFilterSectionsFromProcessor()
{
    for (auto& sectionPtr : filterSections)
    {
        auto* section = sectionPtr.get();

        if (section == nullptr)
            continue;

        section->refreshTypeDependentControls();
    }
}

void AvaAudioProcessorEditor::updateUndoRedoButtons()
{
    if (undoButton != nullptr)
    {
        const auto canUndo = ! undoHistory.empty();
        undoButton->setInteractionEnabled(canUndo);
    }

    if (redoButton != nullptr)
    {
        const auto canRedo = ! redoHistory.empty();
        redoButton->setInteractionEnabled(canRedo);
    }

    refreshABCompareButton();
}

void AvaAudioProcessorEditor::resetFilterSectionUiState(const int filterIndex)
{
    if (! juce::isPositiveAndBelow(filterIndex, static_cast<int>(filterSections.size())))
        return;

    if (auto* section = filterSections[static_cast<size_t>(filterIndex)].get())
        section->expanded = false;
}
