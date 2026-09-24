#include "Editor.h"
#include "../crossover/ComponentConfig.h"
#include "FilterSection.h"
#include "SetupSupport.h"
#include "../modules/fft/Processor.h"

void AvaAudioProcessorEditor::rebindActiveModuleEditors()
{
    using namespace crossover_configs;

    if (crossoverEditor == nullptr)
    {
        auto config = makeCrossoverConfig(audioProcessor);
        config.assignHostSlot = [this] (const juce::String& parameterId,
                                        const juce::String& parameterName,
                                        const float normalizedValue)
        {
            return handleHostSlotAssignRequest(parameterId, parameterName, normalizedValue);
        };
        config.onPageChanged = [this]
        {
            storeFilterDisplayOrderToValueTree();

            if (const auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
                audioProcessor.setSelectedCrossoverRange(editor->getVisibleCrossoverRange());

            restoreFilterDisplayOrderFromValueTree();
            rebindActiveModuleEditors();
            refreshHostSlotButtons();
            updateSectionStates();
            resized();
        };
        crossoverEditor = std::make_unique<CrossoverModuleComponent>(std::move(config));
        addAndMakeVisible(*crossoverEditor);
    }

    if (const auto* editor = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get()))
    {
        const auto visibleRange = editor->getVisibleCrossoverRange();

        if (audioProcessor.getSelectedCrossoverRange() != visibleRange)
            audioProcessor.setSelectedCrossoverRange(visibleRange);
    }

    auto rebindFftControls = [this] (juce::AudioProcessorValueTreeState& fftState,
                                     FftModuleProcessor& fftProcessor)
    {
        if (fftAttackControl != nullptr) fftAttackControl->rebind(fftState);
        if (fftReleaseControl != nullptr) fftReleaseControl->rebind(fftState);
        if (fftKneeControl != nullptr) fftKneeControl->rebind(fftState);
        if (fftRatioControl != nullptr) fftRatioControl->rebind(fftState);
        if (fftFloorControl != nullptr) fftFloorControl->rebind(fftState);
        if (fftDynamicModeControl != nullptr) fftDynamicModeControl->rebind(fftState);
        if (fftCorrelationTypeControl != nullptr) fftCorrelationTypeControl->rebind(fftState);
        if (fftDynamicDirectionControl != nullptr) fftDynamicDirectionControl->rebind(fftState);
        if (fftCorrelationSmoothingControl != nullptr) fftCorrelationSmoothingControl->rebind(fftState);
        if (fftDspFftSizeControl != nullptr) fftDspFftSizeControl->rebind(fftState);
        if (fftDspOverlapControl != nullptr) fftDspOverlapControl->rebind(fftState);
        if (fftDspSlopeControl != nullptr) fftDspSlopeControl->rebind(fftState);
        if (fftCorrelationImpactControl != nullptr) fftCorrelationImpactControl->rebind(fftState);
        if (fftDualMonoLeftThresholdControl != nullptr) fftDualMonoLeftThresholdControl->rebind(fftState, FftModuleProcessor::paramDualMonoLeftThresholdId);
        if (fftDualMonoLeftAdaptiveControl != nullptr) fftDualMonoLeftAdaptiveControl->rebind(fftState);
        if (fftDualMonoRightThresholdControl != nullptr) fftDualMonoRightThresholdControl->rebind(fftState, FftModuleProcessor::paramDualMonoRightThresholdId);
        if (fftDualMonoRightAdaptiveControl != nullptr) fftDualMonoRightAdaptiveControl->rebind(fftState);
        if (fftAdaptiveOffsetControl != nullptr) fftAdaptiveOffsetControl->rebind(fftState, FftModuleProcessor::paramSpectralAdaptiveOffsetId);
        if (fftAdaptiveAttackControl != nullptr) fftAdaptiveAttackControl->rebind(fftState, FftModuleProcessor::paramAdaptiveAttackId);
        if (fftAdaptiveHoldControl != nullptr) fftAdaptiveHoldControl->rebind(fftState, FftModuleProcessor::paramAdaptiveHoldId);
        if (fftAdaptiveReleaseControl != nullptr) fftAdaptiveReleaseControl->rebind(fftState, FftModuleProcessor::paramAdaptiveReleaseId);
        if (fftDetectorLowCutControl != nullptr) fftDetectorLowCutControl->rebind(fftState);
        if (fftDetectorHighCutControl != nullptr) fftDetectorHighCutControl->rebind(fftState);
        rebindFftModeControls(fftProcessor);
        refreshFftAnalyserControls(fftProcessor);
    };

    auto rebindFftAttachments = [this] (juce::AudioProcessorValueTreeState& fftState)
    {
        if (fftDeltaButton != nullptr)
            fftDeltaAttachment = std::make_unique<ButtonAttachment>(fftState,
                                                                    FftModuleProcessor::paramDeltaId,
                                                                    *fftDeltaButton);
        if (fftDualMonoLinkButton != nullptr)
            fftDualMonoLinkAttachment = std::make_unique<ButtonAttachment>(fftState,
                                                                           FftModuleProcessor::paramDualMonoLinkId,
                                                                           *fftDualMonoLinkButton);
    };

    auto rebuildFftAnalyser = [this] (FftModuleProcessor& fftProcessor)
    {
        shell_setup_support::removeOwnedChild(*this, fftAnalyserComponent);
        fftAnalyserComponent = shell_setup_support::createFftAnalyserComponent(fftProcessor);
        addAndMakeVisible(*fftAnalyserComponent);
        fftAnalyserComponent->setVisible(fftModuleLoaded);
    };

    auto rebindEqlEditorSections = [this]
    {
        if (auto* eqlProcessor = getActiveEqlProcessor())
        {
            auto& eqlState = eqlProcessor->getValueTreeState();

            if (filterSections.front() == nullptr || addFilterButton == nullptr)
            {
                setupEqlControls(eqlState);
            }
            else if (boundEqlState != &eqlState)
            {
                for (auto& section : filterSections)
                    if (section != nullptr)
                        section->rebind(eqlState);

                boundEqlState = &eqlState;
            }

            refreshFilterPresetList(eqlProcessor->getSelectedFilterPresetName());
        }
    };

    auto rebindFftEditorSections = [this, &rebindFftControls, &rebindFftAttachments, &rebuildFftAnalyser]
    {
        if (auto* fftProcessor = audioProcessor.getFftModuleProcessor())
        {
            auto& fftState = fftProcessor->getValueTreeState();

            if (fftAttackControl == nullptr)
                setupFftControls(fftState, *fftProcessor);
            else
            {
                rebindFftControls(fftState, *fftProcessor);
                rebindFftAttachments(fftState);
            }

            rebuildFftAnalyser(*fftProcessor);
        }
    };

    auto rebindCrossoverEditor = [this] (const bool moduleLoaded,
                                         auto* processor,
                                         std::unique_ptr<juce::Component>& editor,
                                         auto makeConfig)
    {
        if (! moduleLoaded)
        {
            shell_setup_support::removeOwnedChild(*this, editor);
            return;
        }

        if (processor == nullptr)
            return;

        auto* currentEditor = dynamic_cast<CrossoverModuleComponent*>(editor.get());

        if (currentEditor == nullptr || currentEditor->getProcessorIdentity() != processor)
        {
            shell_setup_support::removeOwnedChild(*this, editor);
            auto config = makeConfig(*processor);
            config.assignHostSlot = [this] (const juce::String& parameterId,
                                            const juce::String& parameterName,
                                            const float normalizedValue)
            {
                return handleHostSlotAssignRequest(parameterId, parameterName, normalizedValue);
            };
            config.onModuleCloseRequest = [this]
            {
                juce::MessageManager::callAsync([safeEditor = juce::Component::SafePointer<AvaAudioProcessorEditor>(this)]
                {
                    if (safeEditor == nullptr)
                        return;

                    safeEditor->closeActiveModule();
                    clearKeyboardFocus(*safeEditor);
                });
            };
            editor = std::make_unique<CrossoverModuleComponent>(std::move(config));
            addAndMakeVisible(*editor);
        }
        else
        {
            currentEditor->refreshExternalState();
        }

        if (auto* crossoverModuleEditor = dynamic_cast<CrossoverModuleComponent*>(editor.get()))
        {
            const auto* crossover = dynamic_cast<CrossoverModuleComponent*>(crossoverEditor.get());
            crossoverModuleEditor->setExternalCrossoverRange(crossover != nullptr ? crossover->getVisibleCrossoverRange() : 0);
        }

        editor->setVisible(moduleLoaded);
    };

    refreshModuleStateListeners();

    rebindEqlEditorSections();
    rebindFftEditorSections();
    rebindCrossoverEditor(tlsModuleLoaded,
                          audioProcessor.getTlsModuleProcessor(),
                          tlsModuleEditor,
                          [] (auto& processor) { return makeTlsCrossoverConfig(processor); });
    rebindCrossoverEditor(dynModuleLoaded,
                          audioProcessor.getDynModuleProcessor(),
                          dynModuleEditor,
                          [] (auto& processor) { return makeDynCrossoverConfig(processor); });
    rebindCrossoverEditor(trsModuleLoaded,
                          audioProcessor.getTrsModuleProcessor(),
                          trsModuleEditor,
                          [this] (auto& processor) { return makeTrsCrossoverConfig(processor, *this, trsModuleEditor); });
    refreshHostSlotButtons();
}
