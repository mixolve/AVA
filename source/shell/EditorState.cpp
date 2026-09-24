#include "Editor.h"
#include "FilterSection.h"
#include "WindowState.h"
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

    filterDisplayOrder.clear();
    filterDisplayOrder.reserve(EqlModuleProcessor::maxFilterCount);

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        filterDisplayOrder.push_back(filterIndex);
        resetFilterSectionUiState(filterIndex);
    }

    storeEditorStateToValueTree();
    updateSectionStates();
    resized();
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::performUndo()
{
    commitPendingHistorySnapshot(true);

    if (undoHistory.empty())
        return;

    const auto snapshot = undoHistory.back();
    undoHistory.pop_back();

    const auto currentSnapshot = committedHistorySnapshot;

    if (applyHistorySnapshot(snapshot))
        redoHistory.push_back(currentSnapshot);
    else
        undoHistory.push_back(snapshot);

    updateUndoRedoButtons();
}

void AvaAudioProcessorEditor::performRedo()
{
    commitPendingHistorySnapshot(true);

    if (redoHistory.empty())
        return;

    const auto snapshot = redoHistory.back();
    redoHistory.pop_back();

    const auto currentSnapshot = committedHistorySnapshot;

    if (applyHistorySnapshot(snapshot))
        undoHistory.push_back(currentSnapshot);
    else
        redoHistory.push_back(snapshot);

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
    auto& state = valueTreeState.state;

    setLoadedModuleFlags(audioProcessor.getActiveModule());
    hostParametersExpanded = static_cast<bool>(state.getProperty(
        AvaAudioProcessor::editorHostParametersExpandedStateKey, false));
    routingExpanded = static_cast<bool>(state.getProperty(
        AvaAudioProcessor::editorRoutingExpandedStateKey, false));
    if (routingExpanded)
        hostParametersExpanded = false;

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        auto* section = filterSections[static_cast<size_t>(filterIndex)].get();

        if (section != nullptr)
            section->expanded = false;
    }

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
    {
        auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];
        assignment.parameterId = state.getProperty(AvaAudioProcessor::getHostSlotTargetStateKey(slotIndex)).toString();
        assignment.parameterName.clear();
    }

    refreshHostSlotButtons();
    rebindActiveModuleEditors();
    restoreFilterDisplayOrderFromValueTree();
}

void AvaAudioProcessorEditor::setLoadedModuleFlags(const AvaAudioProcessor::ActiveModule activeModule) noexcept
{
    eqlModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::eql;
    fftModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::fft;
    tlsModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::tls;
    dynModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::dyn;
    trsModuleLoaded = activeModule == AvaAudioProcessor::ActiveModule::trs;
}

void AvaAudioProcessorEditor::storeEditorStateToValueTree() noexcept
{
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
