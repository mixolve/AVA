#include "Editor.h"
#include "ValueEditor.h"
#include "EditorStateSchema.h"
#include "../routing/State.h"
#include "EditorSetup.h"
#include "ChoiceControl.h"
#include "FilterSection.h"
#include "EditorPreferences.h"
#include "FilterOrderState.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/shared/BlockParameters.h"
#include "../crossover/ParameterIds.h"
#include "../crossover/ModuleBindings.h"
#include "../modules/dyn/Processor.h"
#include "../routing/Panel.h"
#include "PresetSection.h"
#include "ParameterControl.h"
#include "TextControl.h"
#include <map>

EqlModuleProcessor* AvaAudioProcessorEditor::getActiveEqlProcessor() noexcept
{
    return audioProcessor.getEqlModuleProcessor();
}

const EqlModuleProcessor* AvaAudioProcessorEditor::getActiveEqlProcessor() const noexcept
{
    return audioProcessor.getEqlModuleProcessor();
}

void AvaAudioProcessorEditor::performHistoryStep(const HistoryAction action)
{
    if (audioProcessor.isRoutingInstance()) {
        if (auto* editor = audioProcessor.getRoutingOwner().getContentEditor())
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(editor), action] {
                if (safe != nullptr) safe->performHistoryStep(action);
            });
        return;
    }
    commitPendingHistorySnapshot(true);

    auto& sourceHistory = action == HistoryAction::undo ? undoHistory : redoHistory;
    auto& destinationHistory = action == HistoryAction::undo ? redoHistory : undoHistory;

    if (sourceHistory.empty())
        return;

    const auto entry = sourceHistory.back();
    sourceHistory.pop_back();

    const auto currentSnapshot = committedHistorySnapshot;
    if (entry.isEqlPreset())
    {
        juce::MemoryBlock currentBank;
        if (applyEqlPresetHistoryState(entry.eqlPresetInstanceId, entry.eqlPresetBank, currentBank))
            destinationHistory.emplace_back(currentSnapshot, entry.eqlPresetInstanceId, currentBank);
        else
            sourceHistory.push_back(entry);
    }
    else if (applyHistorySnapshot(entry.snapshot))
        destinationHistory.emplace_back(currentSnapshot);
    else
        sourceHistory.push_back(entry);

    updateUndoRedoButtons();
}

void AvaAudioProcessorEditor::restoreFilterDisplayOrderFromValueTree()
{
    cancelFilterDrag();
    filterDisplayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);

    if (getActiveEqlProcessor() == nullptr)
        return;

    const auto activeCount = getActiveFilterCount();

    if (activeCount <= 0)
        return;

    const auto key = AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(
        audioProcessor.getSelectedCrossoverRange());
    const auto savedOrder = valueTreeState.state.getProperty(key).toString();

    if (const auto restored = shell_filter_order_state::decode(savedOrder,
                                                                activeCount,
                                                                EqlModuleProcessor::maxFilterCount))
        filterDisplayOrder = *restored;
}

void AvaAudioProcessorEditor::storeFilterDisplayOrderToValueTree() noexcept
{
    if (getActiveEqlProcessor() == nullptr)
        return;

    auto& state = valueTreeState.state;
    const auto key = AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(
        audioProcessor.getSelectedCrossoverRange());
    const auto activeCount = getActiveFilterCount();
    const auto encodedOrder = shell_filter_order_state::encode(filterDisplayOrder, activeCount);

    if (encodedOrder.isEmpty())
        state.removeProperty(key, nullptr);
    else
        state.setProperty(key, encodedOrder, nullptr);
}

void AvaAudioProcessorEditor::restoreEditorStateFromValueTree()
{
    cancelHostSlotMove();
    const juce::ScopedValueSetter<bool> restoreGuard(restoringEditorUiState, true);

    displayedModule = audioProcessor.getActiveModule();

    refreshHostSlotButtons();
    rebindActiveModuleEditors();
    restoreFilterDisplayOrderFromValueTree();
    restoreEditorUiState();
}

void AvaAudioProcessorEditor::storeEditorStateToValueTree() noexcept
{
    if (!canStoreEditorUiState()) return;
    storeEditorUiState();
    auto& state = valueTreeState.state;
    const auto activeModuleId = juce::String(AvaAudioProcessor::stateIdForModule(audioProcessor.getActiveModule()));

    if (activeModuleId.isNotEmpty())
        state.setProperty(AvaAudioProcessor::activeModuleStateKey, activeModuleId, nullptr);
    else
        state.removeProperty(AvaAudioProcessor::activeModuleStateKey, nullptr);

    storeFilterDisplayOrderToValueTree();

    if (!suppressEditorSizeStateSave && !isDockOnly() && getWidth() > 0 && getHeight() > 0)
    {
        const juce::Point<int> size { getWidth(), getHeight() };
        audioProcessor.setLastEditorSize(size.x, size.y);
        state.setProperty(AvaAudioProcessor::editorWidthStateKey, size.x, nullptr);
        state.setProperty(AvaAudioProcessor::editorHeightStateKey, size.y, nullptr);
    }

}

int AvaAudioProcessorEditor::getActiveFilterCount() const noexcept
{
    if (const auto* eqlProcessor = getActiveEqlProcessor())
        return eqlProcessor->getActiveFilterCount();

    return 0;
}

bool AvaAudioProcessorEditor::canStoreEditorUiState() const noexcept
{
    // Applying a snapshot can queue layouts before its own final resync.
    // Those layouts still describe the old window and must not overwrite it.
    return !restoringEditorUiState && !processorStateResyncPending.load()
        && !audioProcessor.isRestoringState()
        && !audioProcessor.getRoutingOwner().isRestoringState();
}

void AvaAudioProcessorEditor::storeEditorUiState()
{
    if (!canStoreEditorUiState()) return;
    auto& state = valueTreeState.state;
    const auto put = [&state](const char* name, juce::var value) { state.setProperty(shell_editor_ui::key(name), value, nullptr); };
    put("active_instance", activeInstanceId);
    put("about.expanded", unitPage == UnitPage::about);

    {
        // Layout can temporarily clamp the viewport to zero while the page is
        // rebuilt; only a real scroll should replace the persisted position.
        if (!layingOutEditor)
            put("scroll.eql", filterViewport.getViewPositionY());
        int expanded = -1;
        for (size_t i = 0; i < filterSections.size(); ++i)
            if (filterSections[i] != nullptr && filterSections[i]->expanded) expanded = static_cast<int>(i);
        state.setProperty(shell_editor_ui::key("eql.expanded." + juce::String(static_cast<int>(audioProcessor.getSelectedCrossoverRange()))), expanded, nullptr);
    }

}

void AvaAudioProcessorEditor::restoreEditorUiState()
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoringEditorUiState, true);
    const auto& state = valueTreeState.state;
    const auto get = [&state](const char* name, juce::var fallback) { return state.getProperty(shell_editor_ui::key(name), fallback); };
    if (unitPage == UnitPage::main || unitPage == UnitPage::about)
        unitPage = bool(get("about.expanded", false)) ? UnitPage::about : UnitPage::main;
    if (routingPanel != nullptr) routingPanel->restoreUiState();

    const auto expanded = static_cast<int>(state.getProperty(shell_editor_ui::key("eql.expanded." + juce::String(static_cast<int>(audioProcessor.getSelectedCrossoverRange()))), -1));
    enforceSingleExpandedFilterSection(expanded);
    updateSectionStates();
    resized();
    filterViewport.setViewPosition(0, static_cast<int>(get("scroll.eql", 0)));

}
