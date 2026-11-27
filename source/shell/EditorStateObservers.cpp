#include "EqlCopyEditor.h"
#include "Editor.h"
#include "EditorPreferences.h"
#include "EditorStateSchema.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "EditorSetup.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/RangeBank.h"

void AvaAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float)
{
    if (parameterID == AvaAudioProcessor::paramCrossoverActiveSplitCountId)
        scheduleModuleControlsRefresh(true);
    else if (parameterID.endsWith(".hidden"))
        scheduleModuleControlsRefresh(false);
    else if (parameterID.endsWith("_domain") || parameterID.endsWith("_mode"))
        scheduleModuleControlsRefresh(true);
    else if (parameterID.startsWith("filter-") && parameterID.endsWith("_phase"))
        scheduleModuleControlsRefresh(false);

    // Root APVTS parameters are exposed to the DAW (including host slots).
    // Their automation already creates a host Undo entry. Child-module
    // parameters remain chunk-only and still need state notifications.
    const bool hostParameter = !audioProcessor.isRoutingInstance()
        && valueTreeState.getParameter(parameterID) != nullptr;
    scheduleHistorySnapshot(!hostParameter);
}

void AvaAudioProcessorEditor::scheduleModuleControlsRefresh(const bool rebind)
{
    if (rebind)
        moduleControlsRebindPending.store(true, std::memory_order_release);
    if (moduleControlsRefreshPending.exchange(true, std::memory_order_acq_rel))
        return;

    const auto scheduled = juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            if (safe == nullptr || ! safe->moduleControlsRefreshPending.exchange(false, std::memory_order_acq_rel))
                return;
            if (safe->moduleControlsRebindPending.exchange(false, std::memory_order_acq_rel))
                safe->rebindActiveModuleEditors();
            safe->updateSectionStates();
            safe->resized();
            if (safe->controlSearchPanel != nullptr) safe->controlSearchPanel->refreshMatches();
        });
    if (! scheduled)
        moduleControlsRefreshPending.store(false, std::memory_order_release);
}

void AvaAudioProcessorEditor::resyncEditorFromProcessorState()
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoringEditorUiState, true);
    const auto restoredInstance = static_cast<int>(valueTreeState.state.getProperty(shell_editor_ui::key("active_instance"), 0));
    if (routingInstanceId == 0)
    {
        const juce::ScopedValueSetter<bool> sizeGuard(suppressEditorSizeStateSave, true);
        const auto defaultExpandedHeight = getExpandedEditorWidth(shell_editor_ui::editorColumns(getRootWindowState()));
        const auto savedHeight = int(valueTreeState.state.getProperty(
            AvaAudioProcessor::editorHeightStateKey, defaultExpandedHeight));
        expandedHeightBeforeDock = savedHeight;
        applyDockWindowSize(getEditorWidth(*this), juce::jlimit(
            getMinimumEditorHeight(), getMaximumEditorHeight(),
            isDockOnly() ? getMinimumEditorHeight() : savedHeight));
    }
    restoreEditorStateFromValueTree();
    refreshQuickAccessControls();
    refreshModuleStateListeners();
    updateSectionStates();
    resized();
    repaint();
    if (routingInstanceId == 0)
    {
        if (restoredInstance != activeInstanceId || (activeInstanceProcessor != nullptr
            && audioProcessor.getRoutingInstanceHandle(restoredInstance) != activeInstanceProcessor))
        {
            closeRoutingInstance();
            openRoutingInstance(restoredInstance);
        }
        else if (activeInstanceEditor != nullptr)
            activeInstanceEditor->resyncEditorFromProcessorState();
    }
}

void AvaAudioProcessorEditor::scheduleProcessorStateResync()
{
    if (processorStateResyncPending.exchange(true))
        return;

    const auto scheduled = juce::MessageManager::callAsync(
        [safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            if (safeEditor == nullptr || ! safeEditor->processorStateResyncPending.exchange(false))
                return;

            safeEditor->resyncEditorFromProcessorState();
        });

    if (! scheduled)
        processorStateResyncPending.store(false);
}

void AvaAudioProcessorEditor::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                                       const juce::Identifier& property)
{
    // Actual value edits arrive through parameterChanged. APVTS flushing its
    // PARAM nodes during serialization must not create another undo snapshot.
    if (treeWhosePropertyHasChanged.hasType("PARAM")) return;
    if (treeWhosePropertyHasChanged == valueTreeState.state
        && property == juce::Identifier(AvaAudioProcessor::activeModuleStateKey)
        && ! suppressProcessorStateResync)
        scheduleProcessorStateResync();

    if (property == juce::Identifier(EqlModuleProcessor::filterPresetSelectedStateKey))
        scheduleModuleControlsRefresh(true);

    if (treeWhosePropertyHasChanged == valueTreeState.state
        && property.toString().startsWith("ava.block_hidden.band-"))
        scheduleModuleControlsRefresh(false);

    if (treeWhosePropertyHasChanged == valueTreeState.state
        && property.toString().startsWith("ava.host_slot."))
    {
        refreshHostSlotButtons();
        if (activeInstanceEditor != nullptr) activeInstanceEditor->refreshHostSlotButtons();
    }
    // Zoom and dock layout are presentation settings, not DAW undo events.
    if (property == shell_editor_ui::key("dock_count"))
    {
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
        {
            if (safe == nullptr) return;
            safe->resized();
            if (safe->activeInstanceEditor != nullptr) safe->activeInstanceEditor->resized();
        });
    }
    if (!isEditorViewProperty(property)) scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::valueTreeRedirected(juce::ValueTree& treeWhichHasBeenChanged)
{
    if (treeWhichHasBeenChanged == valueTreeState.state && ! suppressProcessorStateResync)
        scheduleProcessorStateResync();

    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::registerParameterListeners()
{
    for (const auto parameterState : valueTreeState.state)
    {
        const auto parameterId = parameterState.getProperty("id").toString();

        if (parameterId.isNotEmpty())
            valueTreeState.addParameterListener(parameterId, this);
    }

    if (! shellStateListenerRegistered)
    {
        valueTreeState.state.addListener(this);
        shellStateListenerRegistered = true;
    }

    refreshModuleStateListeners();
}

void AvaAudioProcessorEditor::registerObservedModuleParameterListeners(juce::AudioProcessorValueTreeState& moduleValueTreeState)
{
    auto observedListeners = ObservedModuleParameterListeners {};
    observedListeners.valueTreeState = &moduleValueTreeState;
    observedListeners.parameterIds.reserve(static_cast<size_t>(moduleValueTreeState.state.getNumChildren()));

    for (const auto parameterState : moduleValueTreeState.state)
    {
        const auto parameterId = parameterState.getProperty("id").toString();

        if (parameterId.isEmpty())
            continue;

        moduleValueTreeState.addParameterListener(parameterId, this);
        observedListeners.parameterIds.push_back(parameterId);
    }

    observedModuleParameterListeners.push_back(std::move(observedListeners));
}

void AvaAudioProcessorEditor::unregisterParameterListeners()
{
    for (const auto parameterState : valueTreeState.state)
    {
        const auto parameterId = parameterState.getProperty("id").toString();

        if (parameterId.isNotEmpty())
            valueTreeState.removeParameterListener(parameterId, this);
    }

    if (shellStateListenerRegistered)
    {
        valueTreeState.state.removeListener(this);
        shellStateListenerRegistered = false;
    }

    clearModuleStateListeners();
}

void AvaAudioProcessorEditor::refreshModuleStateListeners()
{
    clearModuleStateListeners();

    const auto observeModule = [this](auto* processor) {
        if (processor == nullptr) return;
        auto& state = processor->getValueTreeState();
        registerObservedModuleParameterListeners(state);
        auto tree = state.state;
        tree.addListener(this);
        observedModuleStates.push_back(tree);
    };
    observeModule(audioProcessor.getBlockChainProcessor());
    observeModule(audioProcessor.getDynModuleProcessor());
    if (auto* bank = audioProcessor.getEqlProcessorBank())
        for (size_t band = 0; band < bank->getCreatedRangeCount(); ++band) observeModule(bank->getProcessor(band));

    // A copy has its own APVTS. The original three listeners do not observe
    // changes to the added instances, so ALL can retain stale controls even
    // when the MODULES stack is already updated.
    for (const auto& slot : audioProcessor.getModuleStackForRange(audioProcessor.getSelectedCrossoverRange()))
    {
        if (slot.uid < ava::module_stack::originalCount)
            continue;
        if (auto* chain = audioProcessor.getCopiedBlockChainProcessor(slot.uid))
            observeModule(chain);
        if (auto* dyn = audioProcessor.getCopiedDynProcessor(slot.uid))
            observeModule(dyn);
        if (slot.type == module_order::eql)
            observeModule(audioProcessor.getCopiedEqlProcessor(slot.uid,
                audioProcessor.getSelectedCrossoverRange()));
    }
}

void AvaAudioProcessorEditor::clearModuleStateListeners()
{
    for (auto& observedListeners : observedModuleParameterListeners)
    {
        if (observedListeners.valueTreeState == nullptr)
            continue;

        for (const auto& parameterId : observedListeners.parameterIds)
            observedListeners.valueTreeState->removeParameterListener(parameterId, this);
    }

    observedModuleParameterListeners.clear();

    for (auto& state : observedModuleStates)
        if (state.isValid())
            state.removeListener(this);

    observedModuleStates.clear();
}

void AvaAudioProcessorEditor::detachModuleEditorBindings()
{
    dismissTextPrompt();
    clearModuleStateListeners();
    boundEqlState = nullptr;
    // Existing copied editors can still hold parameter attachments to the
    // previous processors. Destroy them before state restore replaces copies.
    copiedModuleEditors.clear();
    eqlCopyEditors.clear();

    for (auto& section : filterSections)
        if (section != nullptr)
            section->detach();

    shell_setup_support::removeOwnedChild(*this, blockChainModuleEditor);
    shell_setup_support::removeOwnedChild(*this, dynModuleEditor);
}
