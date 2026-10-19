#include "Editor.h"
#include "EditorStateSchema.h"
#include "EditorSetup.h"
#include "ChoiceControl.h"
#include "FilterSection.h"
#include "EditorPreferences.h"
#include "FilterOrderState.h"
#include "../modules/eql/Processor.h"

namespace
{
juce::Point<int> clampEditorSize(const int width, const int height) noexcept
{
    return { juce::jlimit(minimumEditorWidth, maximumEditorWidth, width),
             juce::jlimit(minimumEditorHeight, maximumEditorHeight, height) };
}
}

EqlModuleProcessor* AvaAudioProcessorEditor::getActiveEqlProcessor() noexcept
{
    if (audioProcessor.getActiveModule() != AvaAudioProcessor::ActiveModule::eql)
        return nullptr;

    return audioProcessor.getEqlModuleProcessor();
}

const EqlModuleProcessor* AvaAudioProcessorEditor::getActiveEqlProcessor() const noexcept
{
    if (audioProcessor.getActiveModule() != AvaAudioProcessor::ActiveModule::eql)
        return nullptr;

    return audioProcessor.getEqlModuleProcessor();
}

void AvaAudioProcessorEditor::clearAllFilters()
{
    auto* eqlProcessor = getActiveEqlProcessor();

    if (eqlProcessor == nullptr || ! eqlProcessor->clearFilters())
        return;

    filterDisplayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
        resetFilterSectionUiState(filterIndex);

    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::performHistoryStep(const HistoryAction action)
{
    commitPendingHistorySnapshot(true);

    auto& sourceHistory = action == HistoryAction::undo ? undoHistory : redoHistory;
    auto& destinationHistory = action == HistoryAction::undo ? redoHistory : undoHistory;

    if (sourceHistory.empty())
        return;

    const auto snapshot = sourceHistory.back();
    sourceHistory.pop_back();

    const auto currentSnapshot = committedHistorySnapshot;

    if (applyHistorySnapshot(snapshot))
        destinationHistory.push_back(currentSnapshot);
    else
        sourceHistory.push_back(snapshot);

    updateUndoRedoButtons();
}

void AvaAudioProcessorEditor::restoreFilterDisplayOrderFromValueTree()
{
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
    auto& state = valueTreeState.state;

    setLoadedModuleFlags(audioProcessor.getActiveModule());
    hostParametersExpanded = static_cast<bool>(state.getProperty(
        AvaAudioProcessor::editorHostParametersExpandedStateKey, false));
    routingExpanded = static_cast<bool>(state.getProperty(
        AvaAudioProcessor::editorRoutingExpandedStateKey, false));
    if (routingExpanded)
        hostParametersExpanded = false;

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
    {
        auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];
        assignment.parameterId = state.getProperty(AvaAudioProcessor::getHostSlotTargetStateKey(slotIndex)).toString();
        assignment.parameterName.clear();
    }

    refreshHostSlotButtons();
    rebindActiveModuleEditors();
    restoreFilterDisplayOrderFromValueTree();
    restoreEditorUiState();
}

void AvaAudioProcessorEditor::setLoadedModuleFlags(const AvaAudioProcessor::ActiveModule activeModule) noexcept
{
    eqlModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::eql;
    tlsModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::tls;
    dynModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::dyn;
}

void AvaAudioProcessorEditor::storeEditorStateToValueTree() noexcept
{
    if (restoringEditorUiState) return;
    storeEditorUiState();
    auto& state = valueTreeState.state;
    const auto activeModuleId = juce::String(AvaAudioProcessor::stateIdForModule(audioProcessor.getActiveModule()));

    if (activeModuleId.isNotEmpty())
        state.setProperty(AvaAudioProcessor::activeModuleStateKey, activeModuleId, nullptr);
    else
        state.removeProperty(AvaAudioProcessor::activeModuleStateKey, nullptr);

    state.setProperty(AvaAudioProcessor::editorHostParametersExpandedStateKey, hostParametersExpanded, nullptr);
    state.setProperty(AvaAudioProcessor::editorRoutingExpandedStateKey, routingExpanded, nullptr);
    storeFilterDisplayOrderToValueTree();

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
    {
        const auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];

        if (assignment.parameterId.isEmpty())
        {
            state.removeProperty(AvaAudioProcessor::getHostSlotTargetStateKey(slotIndex), nullptr);
            continue;
        }

        state.setProperty(AvaAudioProcessor::getHostSlotTargetStateKey(slotIndex), assignment.parameterId, nullptr);
    }

    if (! suppressEditorSizeStateSave && getWidth() > 0 && getHeight() > 0)
    {
        const auto size = clampEditorSize(getWidth(), getHeight());
        audioProcessor.setLastEditorSize(size.x, size.y);
        state.setProperty(AvaAudioProcessor::editorWidthStateKey, size.x, nullptr);
        state.setProperty(AvaAudioProcessor::editorHeightStateKey, size.y, nullptr);
    }

    audioProcessor.refreshHostSlotTargets();
}

juce::Point<int> AvaAudioProcessorEditor::getRestoredEditorSize() const noexcept
{
    const auto& state = valueTreeState.state;

    if (state.hasProperty(AvaAudioProcessor::editorWidthStateKey)
        && state.hasProperty(AvaAudioProcessor::editorHeightStateKey))
    {
        return clampEditorSize(static_cast<int>(state.getProperty(AvaAudioProcessor::editorWidthStateKey, initialEditorWidth)),
                               static_cast<int>(state.getProperty(AvaAudioProcessor::editorHeightStateKey, initialEditorHeight)));
    }

    const auto lastSize = audioProcessor.getLastEditorSize();

    if (lastSize.x > 0 && lastSize.y > 0)
        return clampEditorSize(lastSize.x, lastSize.y);

    return { initialEditorWidth, initialEditorHeight };
}

int AvaAudioProcessorEditor::getActiveFilterCount() const noexcept
{
    if (const auto* eqlProcessor = getActiveEqlProcessor())
        return eqlProcessor->getActiveFilterCount();

    return 0;
}

void AvaAudioProcessorEditor::syncEditorWidthToBounds()
{
    const auto restoredWidth = juce::jlimit(minimumEditorWidth, maximumEditorWidth, getWidth());
    setResizeLimits(minimumEditorWidth,
                    minimumEditorHeight,
                    maximumEditorWidth,
                    maximumEditorHeight);

    if (restoredWidth != getWidth())
        setSize(restoredWidth, getHeight());
}

void AvaAudioProcessorEditor::storeEditorUiState()
{
    if (restoringEditorUiState) return;
    auto& state = valueTreeState.state;
    const auto put = [&state](const char* name, juce::var value) { state.setProperty(shell_editor_ui::key(name), value, nullptr); };
    put("osc.expanded", oscExpanded);
    put("active_instance", activeInstanceId);
    if (hostParametersExpanded) put("scroll.host", hostParametersViewport.getViewPositionY());
    if (eqlModuleLoaded)
    {
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
    oscExpanded = static_cast<bool>(get("osc.expanded", false));
    if (oscExpanded) { hostParametersExpanded = false; routingExpanded = false; }

    const auto expanded = static_cast<int>(state.getProperty(shell_editor_ui::key("eql.expanded." + juce::String(static_cast<int>(audioProcessor.getSelectedCrossoverRange()))), -1));
    enforceSingleExpandedFilterSection(expanded);
    updateSectionStates();
    resized();
    filterViewport.setViewPosition(0, static_cast<int>(get("scroll.eql", 0)));
    hostParametersViewport.setViewPosition(0, static_cast<int>(get("scroll.host", 0)));
}
