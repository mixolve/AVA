#include "Editor.h"
#include "EditorStateSchema.h"
#include "AnimationState.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "EditorSetup.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"

juce::RangedAudioParameter* AvaAudioProcessorEditor::findHostAssignableParameter(const juce::String& parameterId) const noexcept
{
    const auto trimmedParameterId = parameterId.trim();

    if (trimmedParameterId.isEmpty())
        return nullptr;

    if (auto* parameter = valueTreeState.getParameter(trimmedParameterId))
        return parameter;

    const auto findInModule = [&trimmedParameterId] (auto* processor) -> juce::RangedAudioParameter*
    {
        if (processor == nullptr)
            return nullptr;

        return processor->getValueTreeState().getParameter(trimmedParameterId);
    };

    switch (audioProcessor.getActiveModule())
    {
        case AvaAudioProcessor::ActiveModule::eql: return findInModule(audioProcessor.getEqlModuleProcessor());
        case AvaAudioProcessor::ActiveModule::tls: return findInModule(audioProcessor.getTlsModuleProcessor());
        case AvaAudioProcessor::ActiveModule::dyn: return findInModule(audioProcessor.getDynModuleProcessor());
        case AvaAudioProcessor::ActiveModule::none: break;
    }

    return nullptr;
}

void AvaAudioProcessorEditor::syncHostSlotAssignmentValue(const int slotIndex, const float normalizedValue)
{
    if (! juce::isPositiveAndBelow(slotIndex, static_cast<int>(hostSlotAssignments.size())))
        return;

    const auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];

    if (assignment.parameterId.isEmpty())
        return;

    if (auto* assignedParameter = findHostAssignableParameter(assignment.parameterId))
    {
        assignedParameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalizedValue));
    }
}

void AvaAudioProcessorEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID.endsWith("_domain") || parameterID.endsWith("_mode"))
        scheduleModuleControlsRefresh(true);
    else if (parameterID.startsWith("filter-") && parameterID.endsWith("_phase"))
        scheduleModuleControlsRefresh(false);

    if (! suppressHostSlotAutomationSync)
    {
        juce::ScopedValueSetter<bool> scopedSyncGuard(suppressHostSlotAutomationSync, true);

        for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
        {
            const auto slotParameterId = AvaAudioProcessor::getHostSlotParameterId(slotIndex);

            if (parameterID == slotParameterId)
            {
                scheduleHistorySnapshot();
                return;
            }
        }

        for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotAssignments.size()); ++slotIndex)
        {
            const auto& assignment = hostSlotAssignments[static_cast<size_t>(slotIndex)];

            if (assignment.parameterId != parameterID)
                continue;

            auto* assignedParameter = findHostAssignableParameter(parameterID);
            auto* slotParameter = valueTreeState.getParameter(AvaAudioProcessor::getHostSlotParameterId(slotIndex));

            if (assignedParameter == nullptr || slotParameter == nullptr)
                continue;

            const auto normalizedSourceValue = juce::jlimit(0.0f, 1.0f, assignedParameter->convertTo0to1(newValue));
            slotParameter->setValueNotifyingHost(normalizedSourceValue);
            break;
        }
    }

    scheduleHistorySnapshot();
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
        });
    if (! scheduled)
        moduleControlsRefreshPending.store(false, std::memory_order_release);
}

void AvaAudioProcessorEditor::resyncEditorFromProcessorState()
{
    const juce::ScopedValueSetter<bool> restoreGuard(restoringEditorUiState, true);
    const auto restoredInstance = static_cast<int>(valueTreeState.state.getProperty(shell_editor_ui::key("active_instance"), 0));
    if (routingInstanceId == 0) loadAnimationSettings(*animationSettings, valueTreeState.state);
    restoreEditorStateFromValueTree();
    refreshModuleStateListeners();
    ensureModuleTitle();
    updateSectionStates();
    resized();
    repaint();
    if (routingInstanceId == 0)
    {
        if (restoredInstance != activeInstanceId)
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
    if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        messageManager != nullptr && messageManager->isThisTheMessageThread())
    {
        processorStateResyncPending.store(false);
        resyncEditorFromProcessorState();
        return;
    }

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
    if (treeWhosePropertyHasChanged == valueTreeState.state
        && property == juce::Identifier(AvaAudioProcessor::activeModuleStateKey)
        && ! suppressProcessorStateResync)
        scheduleProcessorStateResync();

    scheduleHistorySnapshot();
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

    observedModuleStates.reserve(1);
    observedModuleParameterListeners.reserve(1);

    juce::ValueTree moduleState;

    auto observeModule = [this, &moduleState] (auto* processor)
    {
        if (processor == nullptr)
            return;

        auto& moduleValueTreeState = processor->getValueTreeState();
        moduleState = moduleValueTreeState.state;
        registerObservedModuleParameterListeners(moduleValueTreeState);
    };

    switch (audioProcessor.getActiveModule())
    {
        case AvaAudioProcessor::ActiveModule::eql:
            observeModule(audioProcessor.getEqlModuleProcessor());
            break;

        case AvaAudioProcessor::ActiveModule::tls:
            observeModule(audioProcessor.getTlsModuleProcessor());
            break;

        case AvaAudioProcessor::ActiveModule::dyn:
            observeModule(audioProcessor.getDynModuleProcessor());
            break;

        case AvaAudioProcessor::ActiveModule::none:
            break;
    }

    if (moduleState.isValid())
    {
        moduleState.addListener(this);
        observedModuleStates.push_back(moduleState);
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
    clearModuleStateListeners();
    boundEqlState = nullptr;

    for (auto& section : filterSections)
        if (section != nullptr)
            section->detach();

    shell_setup_support::removeOwnedChild(*this, tlsModuleEditor);
    shell_setup_support::removeOwnedChild(*this, dynModuleEditor);
}
