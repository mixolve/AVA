#include "Editor.h"
#include "FilterSection.h"
#include "WindowState.h"

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
        bool hostParameters = false;
        int filterScrollY = 0;
    };

    const PreservedUiState preservedUiState
    {
        hostParametersExpanded,
        filterViewport.getViewPositionY()
    };

    const juce::ScopedValueSetter<bool> suppressHistory(suppressHistorySnapshots, true);
    const juce::ScopedValueSetter<bool> suppressHostSlotSync(suppressHostSlotAutomationSync, true);
    pendingHistorySnapshot.store(false, std::memory_order_relaxed);

    {
        // Root-property callbacks are synchronous, so rebind only after replacement.
        const juce::ScopedValueSetter<bool> suppressResync(suppressProcessorStateResync, true);
        detachModuleEditorBindings();

        if (! audioProcessor.applyStateInformationForABCompare(restoredSnapshot.getData(),
                                                                static_cast<int>(restoredSnapshot.getSize())))
        {
            restoreEditorStateFromValueTree();
            ensureModuleTitle();
            updateSectionStates();
            resized();
            refreshABCompareButton();
            return false;
        }
    }

    restoreEditorStateFromValueTree();
    ensureModuleTitle();

    if (auto* eqlProcessor = getActiveEqlProcessor())
    {
        refreshFilterPresetList(eqlProcessor->getSelectedFilterPresetName());
        refreshEqlFilterSectionsFromProcessor();
    }
    else
    {
        refreshFilterPresetList({});
    }

    hostParametersExpanded = preservedUiState.hostParameters;

    storeEditorStateToValueTree();
    syncEditorWidthToBounds();
    updateSectionStates();
    resized();

    const auto filterMaxOffset = juce::jmax(0, getActiveFilterContentHeight() - filterViewport.getHeight());
    filterViewport.setViewPosition(0, juce::jlimit(0, filterMaxOffset, preservedUiState.filterScrollY));

    audioProcessor.getStateInformationForABCompareSnapshot(committedHistorySnapshot);
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

void AvaAudioProcessorEditor::copyCurrentABStateToOtherSlot()
{
    captureCurrentABState();

    const auto currentSlot = audioProcessor.getABCompareActiveSlot();
    const auto targetSlot = currentSlot == 0 ? 1 : 0;
    const auto currentSnapshot = audioProcessor.getABCompareSnapshot(currentSlot);

    if (! currentSnapshot.isEmpty())
        audioProcessor.setABCompareSnapshot(targetSlot, currentSnapshot);

    refreshABCompareButton();
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::refreshABCompareButton()
{
    if (abSlotAButton == nullptr || abSwitchButton == nullptr || abSlotBButton == nullptr)
        return;

    const auto activeABSlot = audioProcessor.getABCompareActiveSlot();
    abSlotAButton->setToggleState(activeABSlot == 0, juce::dontSendNotification);
    abSlotBButton->setToggleState(activeABSlot == 1, juce::dontSendNotification);
    abSwitchButton->setButtonText({});
}
