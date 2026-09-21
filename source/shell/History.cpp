#include "Editor.h"
#include "FilterSection.h"
#include "WindowState.h"

void AvaAudioProcessorEditor::scheduleHistorySnapshot()
{
    if (suppressHistorySnapshots)
        return;

    audioProcessor.notifyHostOfStateChange();
    pendingHistorySnapshot.store(true, std::memory_order_relaxed);
    lastHistoryChangeTimeMs.store(juce::Time::getMillisecondCounter(), std::memory_order_relaxed);
}

void AvaAudioProcessorEditor::commitPendingHistorySnapshot(const bool force)
{
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

    if (snapshot == committedHistorySnapshot)
    {
        updateUndoRedoButtons();
        return;
    }

    if (! committedHistorySnapshot.isEmpty())
    {
        undoHistory.push_back(committedHistorySnapshot);

        constexpr size_t maximumHistoryDepth = 128;
        if (undoHistory.size() > maximumHistoryDepth)
            undoHistory.erase(undoHistory.begin());
    }

    committedHistorySnapshot = snapshot;
    redoHistory.clear();
    updateUndoRedoButtons();
}

bool AvaAudioProcessorEditor::applyHistorySnapshot(const juce::MemoryBlock& snapshot)
{
    if (snapshot.isEmpty())
        return false;

    auto mergedStateXml = AvaAudioProcessor::getXmlFromBinary(snapshot.getData(), static_cast<int>(snapshot.getSize()));

    if (mergedStateXml == nullptr || ! mergedStateXml->hasTagName(valueTreeState.state.getType().toString()))
        return false;

    preserveEditorWindowState(*mergedStateXml, valueTreeState.state);

    juce::MemoryBlock mergedSnapshot;
    AvaAudioProcessor::copyXmlToBinary(*mergedStateXml, mergedSnapshot);

    struct PreservedUiState
    {
        bool hostParameters = false;
        int filterScrollY = 0;
    };

    const PreservedUiState preservedUiState
    {
        hostParametersExpanded,
        filterViewport.getViewPositionY()
    };
    auto* bypassParameter = valueTreeState.getParameter(AvaAudioProcessor::paramGlobalBypassId);
    const auto preservedBypassValue = bypassParameter != nullptr ? bypassParameter->getValue() : 0.0f;

    const juce::ScopedValueSetter<bool> suppressHistory(suppressHistorySnapshots, true);
    const juce::ScopedValueSetter<bool> suppressHostSlotSync(suppressHostSlotAutomationSync, true);
    pendingHistorySnapshot.store(false, std::memory_order_relaxed);

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
            ensureModuleTitle();
            updateSectionStates();
            resized();
            return false;
        }
    }

    if (bypassParameter != nullptr)
        bypassParameter->setValueNotifyingHost(preservedBypassValue);

    restoreEditorStateFromValueTree();
    ensureModuleTitle();
    if (auto* eqlProcessor = getActiveEqlProcessor())
        refreshFilterPresetList(eqlProcessor->getSelectedFilterPresetName());
    else
        refreshFilterPresetList({});
    refreshEqlFilterSectionsFromProcessor();

    hostParametersExpanded = preservedUiState.hostParameters;

    storeEditorStateToValueTree();
    syncEditorWidthToBounds();
    updateSectionStates();
    resized();

    const auto filterMaxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
    filterViewport.setViewPosition(0, juce::jlimit(0, filterMaxOffset, preservedUiState.filterScrollY));

    audioProcessor.getStateInformationForABCompareSnapshot(committedHistorySnapshot);
    updateUndoRedoButtons();
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
        undoButton->setEnabled(canUndo);
        undoButton->setAlpha(1.0f);
    }

    if (redoButton != nullptr)
    {
        const auto canRedo = ! redoHistory.empty();
        redoButton->setEnabled(canRedo);
        redoButton->setAlpha(1.0f);
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

void AvaAudioProcessorEditor::removeFilterSectionUiState(const int removedIndex, const int previousCount)
{
    if (previousCount <= 0)
        return;

    if (previousCount == 1)
    {
        resetFilterSectionUiState(0);
        return;
    }

    for (int sourceIndex = removedIndex + 1; sourceIndex < previousCount; ++sourceIndex)
    {
        auto* target = filterSections[static_cast<size_t>(sourceIndex - 1)].get();
        const auto* source = filterSections[static_cast<size_t>(sourceIndex)].get();

        if (target != nullptr && source != nullptr)
            target->expanded = source->expanded;
    }

    std::vector<int> reorderedOrder;
    reorderedOrder.reserve(static_cast<size_t>(previousCount - 1));

    for (int orderIndex = 0; orderIndex < previousCount; ++orderIndex)
    {
        const auto filterIndex = filterDisplayOrder[static_cast<size_t>(orderIndex)];

        if (filterIndex == removedIndex)
            continue;

        reorderedOrder.push_back(filterIndex > removedIndex ? filterIndex - 1 : filterIndex);
    }

    for (size_t orderIndex = 0; orderIndex < reorderedOrder.size(); ++orderIndex)
        filterDisplayOrder[orderIndex] = reorderedOrder[orderIndex];

    for (int orderIndex = static_cast<int>(reorderedOrder.size()); orderIndex < previousCount; ++orderIndex)
    {
        const auto filterIndex = filterDisplayOrder[static_cast<size_t>(orderIndex)];
        filterDisplayOrder[static_cast<size_t>(orderIndex)] = filterIndex > removedIndex ? filterIndex - 1 : filterIndex;
    }

    resetFilterSectionUiState(previousCount - 1);
    storeEditorStateToValueTree();
}
