#include "Editor.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"
#include "SetupSupport.h"
#include "../modules/fft/Processor.h"

#include <array>

int AvaAudioProcessorEditor::getFftMainContentHeight() const
{
    if (! fftModuleLoaded)
        return 0;

    const auto visibleHeight = [] (const auto* component, const int height)
    {
        return component != nullptr && component->isVisible() ? height : 0;
    };
    const std::array<int, 31> heights {
        visibleHeight(fftAnalyserTimeControl.get(), fftAnalyserTimeControl != nullptr ? fftAnalyserTimeControl->getPreferredHeight() : 0),
        visibleHeight(fftAnalyserHighControl.get(), fftAnalyserHighControl != nullptr ? fftAnalyserHighControl->getPreferredHeight() : 0),
        visibleHeight(fftAnalyserLowControl.get(), fftAnalyserLowControl != nullptr ? fftAnalyserLowControl->getPreferredHeight() : 0),
        visibleHeight(fftGeneralProcessorHeader.get(), rowHeight),
        visibleHeight(fftDspFftSizeControl.get(), fftDspFftSizeControl != nullptr ? fftDspFftSizeControl->getPreferredHeight() : 0),
        visibleHeight(fftDspOverlapControl.get(), fftDspOverlapControl != nullptr ? fftDspOverlapControl->getPreferredHeight() : 0),
        visibleHeight(fftDynamicProcessorHeader.get(), rowHeight),
        visibleHeight(fftDynamicModeControl.get(), fftDynamicModeControl != nullptr ? fftDynamicModeControl->getPreferredHeight() : 0),
        visibleHeight(fftCorrelationTypeControl.get(), fftCorrelationTypeControl != nullptr ? fftCorrelationTypeControl->getPreferredHeight() : 0),
        visibleHeight(fftDynamicDirectionControl.get(), fftDynamicDirectionControl != nullptr ? fftDynamicDirectionControl->getPreferredHeight() : 0),
        visibleHeight(fftCorrelationSmoothingControl.get(), fftCorrelationSmoothingControl != nullptr ? fftCorrelationSmoothingControl->getPreferredHeight() : 0),
        visibleHeight(fftAttackControl.get(), fftAttackControl != nullptr ? fftAttackControl->getPreferredHeight() : 0),
        visibleHeight(fftReleaseControl.get(), fftReleaseControl != nullptr ? fftReleaseControl->getPreferredHeight() : 0),
        visibleHeight(fftKneeControl.get(), fftKneeControl != nullptr ? fftKneeControl->getPreferredHeight() : 0),
        visibleHeight(fftRatioControl.get(), fftRatioControl != nullptr ? fftRatioControl->getPreferredHeight() : 0),
        visibleHeight(fftFloorControl.get(), fftFloorControl != nullptr ? fftFloorControl->getPreferredHeight() : 0),
        visibleHeight(fftDspSlopeControl.get(), fftDspSlopeControl != nullptr ? fftDspSlopeControl->getPreferredHeight() : 0),
        visibleHeight(fftDualMonoLeftThresholdControl.get(), fftDualMonoLeftThresholdControl != nullptr ? fftDualMonoLeftThresholdControl->getPreferredHeight() : 0),
        visibleHeight(fftDualMonoLeftAdaptiveControl.get(), fftDualMonoLeftAdaptiveControl != nullptr ? fftDualMonoLeftAdaptiveControl->getPreferredHeight() : 0),
        visibleHeight(fftCorrelationImpactControl.get(), fftCorrelationImpactControl != nullptr ? fftCorrelationImpactControl->getPreferredHeight() : 0),
        visibleHeight(fftDualMonoRightThresholdControl.get(), fftDualMonoRightThresholdControl != nullptr ? fftDualMonoRightThresholdControl->getPreferredHeight() : 0),
        visibleHeight(fftDualMonoRightAdaptiveControl.get(), fftDualMonoRightAdaptiveControl != nullptr ? fftDualMonoRightAdaptiveControl->getPreferredHeight() : 0),
        visibleHeight(fftDualMonoLinkButton.get(), rowHeight),
        visibleHeight(fftAdaptiveSettingsHeader.get(), rowHeight),
        visibleHeight(fftAdaptiveOffsetControl.get(), fftAdaptiveOffsetControl != nullptr ? fftAdaptiveOffsetControl->getPreferredHeight() : 0),
        visibleHeight(fftAdaptiveAttackControl.get(), fftAdaptiveAttackControl != nullptr ? fftAdaptiveAttackControl->getPreferredHeight() : 0),
        visibleHeight(fftAdaptiveHoldControl.get(), fftAdaptiveHoldControl != nullptr ? fftAdaptiveHoldControl->getPreferredHeight() : 0),
        visibleHeight(fftAdaptiveReleaseControl.get(), fftAdaptiveReleaseControl != nullptr ? fftAdaptiveReleaseControl->getPreferredHeight() : 0),
        visibleHeight(fftDetectorRangeHeader.get(), rowHeight),
        visibleHeight(fftDetectorLowCutControl.get(), fftDetectorLowCutControl != nullptr ? fftDetectorLowCutControl->getPreferredHeight() : 0),
        visibleHeight(fftDetectorHighCutControl.get(), fftDetectorHighCutControl != nullptr ? fftDetectorHighCutControl->getPreferredHeight() : 0)
    };
    auto totalHeight = 0;
    auto visibleRows = 0;

    for (const auto height : heights)
    {
        if (height <= 0)
            continue;

        totalHeight += height;
        ++visibleRows;
    }

    return totalHeight + (verticalGap * juce::jmax(0, visibleRows - 1)) + (verticalGap * 4);
}

void AvaAudioProcessorEditor::refreshFftAnalyserResponse()
{
    if (fftModuleLoaded)
    {
        if (auto* processor = audioProcessor.getFftModuleProcessor())
            rebindFftModeControls(*processor);

        shell_setup_support::refreshFftAnalyserComponent(fftAnalyserComponent.get());
    }
}
