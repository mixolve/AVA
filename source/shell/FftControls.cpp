#include "Editor.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"
#include "SetupSupport.h"
#include "../modules/fft/Processor.h"

#include <cmath>

void AvaAudioProcessorEditor::rebindFftModeControls(FftModuleProcessor& fftProcessor)
{
    auto& state = fftProcessor.getValueTreeState();
    const auto correlationMode = fftProcessor.isCorrelationMode();
    const auto rebindIfNeeded = [&state] (ParameterControl* control, const char* parameterId)
    {
        if (control != nullptr && ! control->isBoundTo(parameterId))
            control->rebind(state, parameterId);
    };

    rebindIfNeeded(fftDualMonoLeftThresholdControl.get(),
                   correlationMode ? FftModuleProcessor::paramCorrelationThresholdId
                             : FftModuleProcessor::paramDualMonoLeftThresholdId);
    rebindIfNeeded(fftDualMonoLeftAdaptiveControl.get(),
                   correlationMode ? FftModuleProcessor::paramCorrelationAdaptiveId
                             : FftModuleProcessor::paramDualMonoLeftAdaptiveId);
    rebindIfNeeded(fftAdaptiveOffsetControl.get(),
                   correlationMode ? FftModuleProcessor::paramCorrelationAdaptiveOffsetId
                             : FftModuleProcessor::paramSpectralAdaptiveOffsetId);
    rebindIfNeeded(fftAdaptiveAttackControl.get(),
                   correlationMode ? FftModuleProcessor::paramCorrelationAdaptiveAttackId
                             : FftModuleProcessor::paramSpectralAdaptiveAttackId);
    rebindIfNeeded(fftAdaptiveHoldControl.get(),
                   correlationMode ? FftModuleProcessor::paramCorrelationAdaptiveHoldId
                             : FftModuleProcessor::paramSpectralAdaptiveHoldId);
    rebindIfNeeded(fftAdaptiveReleaseControl.get(),
                   correlationMode ? FftModuleProcessor::paramCorrelationAdaptiveReleaseId
                             : FftModuleProcessor::paramSpectralAdaptiveReleaseId);
    rebindIfNeeded(fftDspSlopeControl.get(),
                   correlationMode ? FftModuleProcessor::paramCorrelationSlopeId
                             : FftModuleProcessor::paramDspSlopeId);
    rebindIfNeeded(fftDualMonoRightThresholdControl.get(),
                   FftModuleProcessor::paramDualMonoRightThresholdId);
    rebindIfNeeded(fftDualMonoRightAdaptiveControl.get(),
                   FftModuleProcessor::paramDualMonoRightAdaptiveId);
}

void AvaAudioProcessorEditor::refreshFftAnalyserControls(FftModuleProcessor& fftProcessor)
{
    rebindFftModeControls(fftProcessor);
    const juce::ScopedValueSetter<bool> scopedIgnore(suppressFftAnalyserControlChangeHandlers, true);

    if (fftAnalyserTimeControl != nullptr)
        fftAnalyserTimeControl->setValue(fftProcessor.getAnalyserParameterValue(FftModuleProcessor::paramTimeId), false);

    if (fftAnalyserHighControl != nullptr && fftAnalyserLowControl != nullptr)
    {
        const auto correlationMode = fftProcessor.isCorrelationMode();
        const auto highParameterId = correlationMode ? FftModuleProcessor::paramCorrelationReductionHighId
                                               : FftModuleProcessor::paramSpectralReductionHighId;
        const auto lowParameterId = correlationMode ? FftModuleProcessor::paramCorrelationReductionLowId
                                              : FftModuleProcessor::paramSpectralReductionLowId;
        const auto minimum = correlationMode ? 0.0 : -99.0;
        const auto maximum = correlationMode ? 4.0 : 0.0;

        for (auto* control : { fftAnalyserHighControl.get(), fftAnalyserLowControl.get() })
            control->setValueRange(minimum, maximum, 0.01);

        fftAnalyserHighControl->setDefaultValue(correlationMode ? 2.2 : 0.0);
        fftAnalyserLowControl->setDefaultValue(correlationMode ? 0.0 : -36.0);
        fftAnalyserHighControl->setValue(fftProcessor.getAnalyserParameterValue(highParameterId), false);
        fftAnalyserLowControl->setValue(fftProcessor.getAnalyserParameterValue(lowParameterId), false);
    }
}

void AvaAudioProcessorEditor::setupFftControls(juce::AudioProcessorValueTreeState& fftState,
                                              FftModuleProcessor& fftProcessor)
{
        const auto refreshFftAnalyserState = [this, &fftProcessor]
        {
            refreshFftAnalyserControls(fftProcessor);
            shell_setup_support::refreshFftAnalyserComponent(fftAnalyserComponent.get());
            scheduleHistorySnapshot();
        };

        const auto configureSectionHeader = [] (BoxTextButton& header, const juce::String& text)
        {
            header.setButtonText(text);
            header.setClickingTogglesState(false);
            header.setBorderVisible(true);
            header.setFillVisible(false);
            header.setDividerLineVisible(false);
            header.setPressFillEnabled(false);
            header.setTextJustification(juce::Justification::centredLeft);
            header.setInterceptsMouseClicks(false, false);
        };

        fftGeneralProcessorHeader = std::make_unique<BoxTextButton>(uiAccent);
        configureSectionHeader(*fftGeneralProcessorHeader, "MAIN");
        filterContent.addAndMakeVisible(*fftGeneralProcessorHeader);

        fftDspFftSizeControl = std::make_unique<ChoiceControl>(fftState,
                                                               FftModuleProcessor::paramDspFftSizeId,
                                                               "WIN-SIZE");
        filterContent.addAndMakeVisible(*fftDspFftSizeControl);

        fftDspOverlapControl = std::make_unique<ChoiceControl>(fftState,
                                                               FftModuleProcessor::paramDspOverlapId,
                                                               "OVERLAP");
        filterContent.addAndMakeVisible(*fftDspOverlapControl);

        fftDynamicProcessorHeader = std::make_unique<BoxTextButton>(uiAccent);
        configureSectionHeader(*fftDynamicProcessorHeader, "DYNAMIC PROCESSOR");
        filterContent.addAndMakeVisible(*fftDynamicProcessorHeader);

        fftDynamicModeControl = std::make_unique<ChoiceControl>(fftState,
                                                                 FftModuleProcessor::paramDynamicModeId,
                                                                 "MODE");
        fftDynamicModeControl->onValueChanged = [this]
        {
            updateSectionStates();
            resized();
            clearKeyboardFocus(*this);

            juce::Component::SafePointer<AvaAudioProcessorEditor> safeEditor(this);
            juce::MessageManager::callAsync([safeEditor]
            {
                if (safeEditor == nullptr)
                    return;

                if (auto* processor = safeEditor->audioProcessor.getFftModuleProcessor())
                {
                    safeEditor->rebindFftModeControls(*processor);
                    safeEditor->refreshFftAnalyserControls(*processor);
                    shell_setup_support::refreshFftAnalyserComponent(safeEditor->fftAnalyserComponent.get());
                }
            });
        };
        filterContent.addAndMakeVisible(*fftDynamicModeControl);

        fftCorrelationTypeControl = std::make_unique<ChoiceControl>(fftState,
            FftModuleProcessor::paramCorrelationTypeId, "TYPE");
        filterContent.addAndMakeVisible(*fftCorrelationTypeControl);

        fftDynamicDirectionControl = std::make_unique<ChoiceControl>(fftState,
                                                                      FftModuleProcessor::paramDynamicDirectionId,
                                                                      "DIRECTION");
        filterContent.addAndMakeVisible(*fftDynamicDirectionControl);

        fftCorrelationSmoothingControl = std::make_unique<ParameterControl>(
            fftState,
            FftModuleProcessor::paramCorrelationSmoothingId,
            "SMOOTHING",
            2);
        filterContent.addAndMakeVisible(*fftCorrelationSmoothingControl);

        fftAttackControl = std::make_unique<ParameterControl>(fftState,
                                                              FftModuleProcessor::paramAttackId,
                                                              "ATTACK",
                                                              2);
        filterContent.addAndMakeVisible(*fftAttackControl);

        fftReleaseControl = std::make_unique<ParameterControl>(fftState,
                                                               FftModuleProcessor::paramReleaseId,
                                                               "RELEASE",
                                                               2);
        filterContent.addAndMakeVisible(*fftReleaseControl);

        fftKneeControl = std::make_unique<ParameterControl>(fftState,
                                                            FftModuleProcessor::paramKneeId,
                                                            "KNEE",
                                                            2);
        filterContent.addAndMakeVisible(*fftKneeControl);

        fftRatioControl = std::make_unique<ParameterControl>(fftState,
                                                             FftModuleProcessor::paramRatioId,
                                                             "RATIO",
                                                             2);
        filterContent.addAndMakeVisible(*fftRatioControl);

        fftFloorControl = std::make_unique<ParameterControl>(fftState,
                                                              FftModuleProcessor::paramFloorId,
                                                              "FLOOR",
                                                              2);
        filterContent.addAndMakeVisible(*fftFloorControl);

        fftDspSlopeControl = std::make_unique<ParameterControl>(fftState,
                                                                FftModuleProcessor::paramDspSlopeId,
                                                                "SLOPE",
                                                                2);
        filterContent.addAndMakeVisible(*fftDspSlopeControl);

        fftCorrelationImpactControl = std::make_unique<ParameterControl>(fftState,
                                                                    FftModuleProcessor::paramCorrelationImpactId,
                                                                    "IMPACT",
                                                                    2);
        const auto formatImpact = [] (const double value)
        {
            if (value <= -99.995)
                return juce::String("LEFT");
            if (value >= 99.995)
                return juce::String("RIGHT");
            if (std::abs(value) <= 0.005)
                return juce::String("BOTH");

            return formatFixedDecimalValue(value, 2);
        };
        fftCorrelationImpactControl->setValueTextTransform(
            formatImpact,
            formatImpact,
            [] (const juce::String& text)
            {
                const auto valueText = text.trim();

                if (valueText.equalsIgnoreCase("LEFT"))
                    return -100.0;
                if (valueText.equalsIgnoreCase("BOTH"))
                    return 0.0;
                if (valueText.equalsIgnoreCase("RIGHT"))
                    return 100.0;

                return parseNumericInput(valueText);
            });
        filterContent.addAndMakeVisible(*fftCorrelationImpactControl);

        fftDeltaButton = std::make_unique<BoxTextButton>(uiAccent);
        fftDeltaButton->setButtonText("DELTA");
        fftDeltaButton->setTextJustification(juce::Justification::centred);
        fftDeltaButton->setClickingTogglesState(true);
        fftDeltaAttachment = std::make_unique<ButtonAttachment>(fftState,
                                                                FftModuleProcessor::paramDeltaId,
                                                                *fftDeltaButton);
        fftDeltaButton->setLongPressPromptActions({}, [this]
        {
            if (auto* parameter = findHostAssignableParameter(FftModuleProcessor::paramDeltaId))
                handleHostSlotAssignRequest(FftModuleProcessor::paramDeltaId, "DELTA", parameter->getValue());
        });
        fftDeltaButton->onClick = [this]
        {
            clearKeyboardFocus(*this);
        };
        addAndMakeVisible(*fftDeltaButton);

        fftDualMonoLeftThresholdControl = std::make_unique<ParameterControl>(fftState,
                                                                             FftModuleProcessor::paramDualMonoLeftThresholdId,
                                                                             "L.THRESH",
                                                                             2);
        filterContent.addAndMakeVisible(*fftDualMonoLeftThresholdControl);

        fftDualMonoLeftAdaptiveControl = std::make_unique<ParameterControl>(fftState,
                                                                            FftModuleProcessor::paramDualMonoLeftAdaptiveId,
                                                                            "L.ADAPTIVE",
                                                                            2);
        filterContent.addAndMakeVisible(*fftDualMonoLeftAdaptiveControl);

        fftDualMonoRightThresholdControl = std::make_unique<ParameterControl>(fftState,
                                                                              FftModuleProcessor::paramDualMonoRightThresholdId,
                                                                              "R.THRESH",
                                                                              2);
        filterContent.addAndMakeVisible(*fftDualMonoRightThresholdControl);

        fftDualMonoRightAdaptiveControl = std::make_unique<ParameterControl>(fftState,
                                                                             FftModuleProcessor::paramDualMonoRightAdaptiveId,
                                                                             "R.ADAPTIVE",
                                                                             2);
        filterContent.addAndMakeVisible(*fftDualMonoRightAdaptiveControl);

        fftDualMonoLinkButton = std::make_unique<BoxTextButton>(uiAccent);
        fftDualMonoLinkButton->setButtonText("LINK-LR (STEREO)");
        fftDualMonoLinkButton->setTextJustification(juce::Justification::centred);
        fftDualMonoLinkButton->setClickingTogglesState(true);
        fftDualMonoLinkAttachment = std::make_unique<ButtonAttachment>(fftState,
                                                                       FftModuleProcessor::paramDualMonoLinkId,
                                                                       *fftDualMonoLinkButton);
        fftDualMonoLinkButton->setLongPressPromptActions({}, [this]
        {
            if (auto* parameter = findHostAssignableParameter(FftModuleProcessor::paramDualMonoLinkId))
                handleHostSlotAssignRequest(FftModuleProcessor::paramDualMonoLinkId,
                                            "LINK-LR",
                                            parameter->getValue());
        });
        fftDualMonoLinkButton->onClick = [this]
        {
            clearKeyboardFocus(*this);
        };
        filterContent.addAndMakeVisible(*fftDualMonoLinkButton);

        fftAdaptiveSettingsHeader = std::make_unique<BoxTextButton>(uiAccent);
        configureSectionHeader(*fftAdaptiveSettingsHeader, "ADAPTIVE SETTINGS");
        filterContent.addAndMakeVisible(*fftAdaptiveSettingsHeader);

        fftAdaptiveOffsetControl = std::make_unique<ParameterControl>(fftState,
                                                                       FftModuleProcessor::paramSpectralAdaptiveOffsetId,
                                                                       "OFFSET",
                                                                       2);
        filterContent.addAndMakeVisible(*fftAdaptiveOffsetControl);

        fftAdaptiveAttackControl = std::make_unique<ParameterControl>(fftState,
                                                                       FftModuleProcessor::paramSpectralAdaptiveAttackId,
                                                                       "ATTACK",
                                                                       2);
        filterContent.addAndMakeVisible(*fftAdaptiveAttackControl);

        fftAdaptiveHoldControl = std::make_unique<ParameterControl>(fftState,
                                                                     FftModuleProcessor::paramSpectralAdaptiveHoldId,
                                                                     "HOLD",
                                                                     2);
        filterContent.addAndMakeVisible(*fftAdaptiveHoldControl);

        fftAdaptiveReleaseControl = std::make_unique<ParameterControl>(fftState,
                                                                        FftModuleProcessor::paramSpectralAdaptiveReleaseId,
                                                                        "RELEASE",
                                                                        2);
        filterContent.addAndMakeVisible(*fftAdaptiveReleaseControl);

        fftDetectorRangeHeader = std::make_unique<BoxTextButton>(uiAccent);
        configureSectionHeader(*fftDetectorRangeHeader, "RANGE");
        filterContent.addAndMakeVisible(*fftDetectorRangeHeader);

        fftDetectorLowCutControl = std::make_unique<ParameterControl>(fftState,
            FftModuleProcessor::paramDetectorLowCutId, "LOW-CUT", 2);
        filterContent.addAndMakeVisible(*fftDetectorLowCutControl);

        fftDetectorHighCutControl = std::make_unique<ParameterControl>(fftState,
            FftModuleProcessor::paramDetectorHighCutId, "HIGH-CUT", 2);
        filterContent.addAndMakeVisible(*fftDetectorHighCutControl);

        fftAnalyserTimeControl = std::make_unique<LocalParameterControl>("TIME",
                                                                         0,
                                                                         0.0,
                                                                         1000.0,
                                                                         1.0,
                                                                         50.0);
        fftAnalyserTimeControl->onValueChanged = [this, &fftProcessor, refreshFftAnalyserState]
        {
            if (suppressFftAnalyserControlChangeHandlers)
                return;

            fftProcessor.setAnalyserParameterValue(FftModuleProcessor::paramTimeId,
                                                   static_cast<float>(fftAnalyserTimeControl->getValue()));
            refreshFftAnalyserState();
        };
        filterContent.addAndMakeVisible(*fftAnalyserTimeControl);

        fftAnalyserHighControl = std::make_unique<LocalParameterControl>("HIGH", 2, -99.0, 0.0, 0.01, 0.0);
        fftAnalyserLowControl = std::make_unique<LocalParameterControl>("LOW", 2, -99.0, 0.0, 0.01, -36.0);

        const auto updateAnalyserBound = [this, &fftProcessor, refreshFftAnalyserState]
        {
            if (suppressFftAnalyserControlChangeHandlers)
                return;

            const auto correlationMode = fftProcessor.isCorrelationMode();
            fftProcessor.setAnalyserParameterValue(
                correlationMode ? FftModuleProcessor::paramCorrelationReductionHighId
                          : FftModuleProcessor::paramSpectralReductionHighId,
                static_cast<float>(fftAnalyserHighControl->getValue()));
            fftProcessor.setAnalyserParameterValue(
                correlationMode ? FftModuleProcessor::paramCorrelationReductionLowId
                          : FftModuleProcessor::paramSpectralReductionLowId,
                static_cast<float>(fftAnalyserLowControl->getValue()));
            refreshFftAnalyserState();
        };
        fftAnalyserHighControl->onValueChanged = updateAnalyserBound;
        fftAnalyserLowControl->onValueChanged = updateAnalyserBound;
        filterContent.addAndMakeVisible(*fftAnalyserHighControl);
        filterContent.addAndMakeVisible(*fftAnalyserLowControl);

        refreshFftAnalyserControls(fftProcessor);

}
