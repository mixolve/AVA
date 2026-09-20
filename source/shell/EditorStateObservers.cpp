#include "Editor.h"
#include "FilterSection.h"
#include "SetupSupport.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/fft/Processor.h"
#include "../modules/trs/Processor.h"
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
        case AvaAudioProcessor::ActiveModule::fft: return findInModule(audioProcessor.getFftModuleProcessor());
        case AvaAudioProcessor::ActiveModule::tls: return findInModule(audioProcessor.getTlsModuleProcessor());
        case AvaAudioProcessor::ActiveModule::dyn: return findInModule(audioProcessor.getDynModuleProcessor());
        case AvaAudioProcessor::ActiveModule::trs: return findInModule(audioProcessor.getTrsModuleProcessor());
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
    auto eqlPlaceFilterIndex = -1;

    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
    {
        if (parameterID == EqlModuleProcessor::getFilterPlaceParamId(filterIndex))
        {
            eqlPlaceFilterIndex = filterIndex;
            break;
        }
    }

    if (eqlPlaceFilterIndex >= 0)
    {
        juce::MessageManager::callAsync([safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this),
                                         eqlPlaceFilterIndex]
        {
            if (safeEditor == nullptr || ! safeEditor->eqlModuleLoaded)
                return;

            const auto displayIndex = safeEditor->getFilterOrderPositionForIndex(eqlPlaceFilterIndex);

            if (! juce::isPositiveAndBelow(eqlPlaceFilterIndex,
                                           static_cast<int>(safeEditor->filterSections.size()))
                || displayIndex < 0)
            {
                return;
            }

            auto* section = safeEditor->filterSections[static_cast<size_t>(eqlPlaceFilterIndex)].get();
            auto* processor = safeEditor->getActiveEqlProcessor();

            if (section == nullptr || processor == nullptr || section->header == nullptr)
                return;

            section->header->setButtonText(processor->getFilterHeaderText(eqlPlaceFilterIndex, displayIndex));
        });
    }

    if (parameterID == FftModuleProcessor::paramDynamicModeId)
    {
        juce::Component::SafePointer<AvaAudioProcessorEditor> safeEditor(this);
        juce::MessageManager::callAsync([safeEditor]
        {
            if (safeEditor == nullptr)
                return;

            if (auto* processor = safeEditor->audioProcessor.getFftModuleProcessor())
                safeEditor->refreshFftAnalyserControls(*processor);

            safeEditor->updateSectionStates();
            safeEditor->resized();
            safeEditor->repaint();
        });
    }

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

void AvaAudioProcessorEditor::resyncEditorFromProcessorState()
{
    restoreEditorStateFromValueTree();
    refreshModuleStateListeners();
    ensureModuleTitle();
    updateSectionStates();
    syncFocusedParameterControl();
    resized();
    repaint();
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
    if (treeWhichHasBeenChanged == valueTreeState.state)
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

        case AvaAudioProcessor::ActiveModule::fft:
            observeModule(audioProcessor.getFftModuleProcessor());
            break;

        case AvaAudioProcessor::ActiveModule::tls:
            observeModule(audioProcessor.getTlsModuleProcessor());
            break;

        case AvaAudioProcessor::ActiveModule::dyn:
            observeModule(audioProcessor.getDynModuleProcessor());
            break;

        case AvaAudioProcessor::ActiveModule::trs:
            observeModule(audioProcessor.getTrsModuleProcessor());
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

    fftDeltaAttachment.reset();
    fftDualMonoLinkAttachment.reset();

    if (fftAttackControl != nullptr) fftAttackControl->detach();
    if (fftReleaseControl != nullptr) fftReleaseControl->detach();
    if (fftKneeControl != nullptr) fftKneeControl->detach();
    if (fftRatioControl != nullptr) fftRatioControl->detach();
    if (fftFloorControl != nullptr) fftFloorControl->detach();
    if (fftDynamicModeControl != nullptr) fftDynamicModeControl->detach();
    if (fftCorrelationTypeControl != nullptr) fftCorrelationTypeControl->detach();
    if (fftDynamicDirectionControl != nullptr) fftDynamicDirectionControl->detach();
    if (fftCorrelationSmoothingControl != nullptr) fftCorrelationSmoothingControl->detach();
    if (fftDspFftSizeControl != nullptr) fftDspFftSizeControl->detach();
    if (fftDspOverlapControl != nullptr) fftDspOverlapControl->detach();
    if (fftDspSlopeControl != nullptr) fftDspSlopeControl->detach();
    if (fftCorrelationImpactControl != nullptr) fftCorrelationImpactControl->detach();
    if (fftDualMonoLeftThresholdControl != nullptr) fftDualMonoLeftThresholdControl->detach();
    if (fftDualMonoLeftAdaptiveControl != nullptr) fftDualMonoLeftAdaptiveControl->detach();
    if (fftDualMonoRightThresholdControl != nullptr) fftDualMonoRightThresholdControl->detach();
    if (fftDualMonoRightAdaptiveControl != nullptr) fftDualMonoRightAdaptiveControl->detach();
    if (fftAdaptiveOffsetControl != nullptr) fftAdaptiveOffsetControl->detach();
    if (fftAdaptiveAttackControl != nullptr) fftAdaptiveAttackControl->detach();
    if (fftAdaptiveHoldControl != nullptr) fftAdaptiveHoldControl->detach();
    if (fftAdaptiveReleaseControl != nullptr) fftAdaptiveReleaseControl->detach();
    if (fftDetectorLowCutControl != nullptr) fftDetectorLowCutControl->detach();
    if (fftDetectorHighCutControl != nullptr) fftDetectorHighCutControl->detach();
    shell_setup_support::removeOwnedChild(*this, fftAnalyserComponent);
    shell_setup_support::removeOwnedChild(*this, tlsModuleEditor);
    shell_setup_support::removeOwnedChild(*this, dynModuleEditor);
    shell_setup_support::removeOwnedChild(*this, trsModuleEditor);
}
