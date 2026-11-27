#include "Editor.h"
#include "ChoiceControl.h"
#include "FilterSection.h"
#include "EditorPreferences.h"

void AvaAudioProcessorEditor::captureCurrentABState()
{
    const auto activeSlot = audioProcessor.getABCompareActiveSlot();

    juce::MemoryBlock snapshot;
    audioProcessor.getStateInformationForABCompareSnapshot(snapshot);
    audioProcessor.setABCompareSnapshot(activeSlot, snapshot);
}

bool AvaAudioProcessorEditor::restoreABStateSnapshot(const juce::MemoryBlock& snapshot)
{
    if (snapshot.isEmpty())
        return false;

    auto stateXml = AvaAudioProcessor::getXmlFromBinary(snapshot.getData(), static_cast<int>(snapshot.getSize()));

    if (stateXml == nullptr || ! stateXml->hasTagName(valueTreeState.state.getType().toString()))
        return false;

    preserveEditorWindowState(*stateXml, valueTreeState.state);

    juce::MemoryBlock restoredSnapshot;
    AvaAudioProcessor::copyXmlToBinary(*stateXml, restoredSnapshot);

    struct PreservedUiState
    {
        UnitPage page = UnitPage::main;
        int filterScrollY = 0;
    };

    const PreservedUiState preservedUiState
    {
        unitPage,
        filterViewport.getViewPositionY()
    };

    const ScopedAtomicFlag suppressHistory(suppressHistorySnapshots, true);
    pendingHistorySnapshot.store(false, std::memory_order_relaxed);

    {
        // Root-property callbacks are synchronous, so rebind only after replacement.
        const juce::ScopedValueSetter<bool> suppressResync(suppressProcessorStateResync, true);
        detachModuleEditorBindings();

        if (! audioProcessor.applyStateInformationForABCompare(restoredSnapshot.getData(),
                                                                static_cast<int>(restoredSnapshot.getSize())))
        {
            restoreEditorStateFromValueTree();
            updateSectionStates();
            resized();
            refreshABCompareButton();
            return false;
        }
    }

    restoreEditorStateFromValueTree();

    refreshFilterPresetList();
    if (getActiveEqlProcessor() != nullptr) refreshEqlFilterSectionsFromProcessor();

    unitPage = preservedUiState.page;
    storeEditorStateToValueTree();
    updateEditorResizeLimits();
    updateSectionStates();
    resized();

    const auto filterMaxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
    filterViewport.setViewPosition(0, juce::jlimit(0, filterMaxOffset, preservedUiState.filterScrollY));

    audioProcessor.getStateInformationForABCompareSnapshot(committedHistorySnapshot);
    audioProcessor.getHistoryState().committedRevision = audioProcessor.getStateRevision();
    return true;
}

void AvaAudioProcessorEditor::switchABState()
{
    captureCurrentABState();

    const auto currentSlot = audioProcessor.getABCompareActiveSlot();
    const auto nextSlot = currentSlot == 0 ? 1 : 0;
    const auto currentSnapshot = audioProcessor.getABCompareSnapshot(currentSlot);

    const auto nextSnapshot = audioProcessor.getABCompareSnapshot(nextSlot);

    if (nextSnapshot == currentSnapshot || restoreABStateSnapshot(nextSnapshot))
        audioProcessor.setABCompareActiveSlot(nextSlot);

    updateUndoRedoButtons();
    refreshABCompareButton();
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::refreshABCompareButton()
{
    if(abControl!=nullptr)
    {
        const auto active = audioProcessor.getABCompareActiveSlot();
        abControl->setSelectedChoiceIndex(active, false);
        abControl->setSelectionHighlighted(active == 1);
        if (abCopyButton != nullptr)
            abCopyButton->setTitleText(active == 0 ? "COPY\nA TO B" : "COPY\nB TO A");
    }
}
