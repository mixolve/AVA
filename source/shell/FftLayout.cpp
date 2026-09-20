#include "Editor.h"
#include "FilterSection.h"
#include "UiConstants.h"
#include "PresetSections.h"

void AvaAudioProcessorEditor::layoutFftModuleSections(juce::Rectangle<int>& bounds)
{
    if (fftAttackControl == nullptr
        || fftReleaseControl == nullptr
        || fftKneeControl == nullptr
        || fftRatioControl == nullptr
        || fftFloorControl == nullptr
        || fftAnalyserTimeControl == nullptr
        || fftAnalyserHighControl == nullptr
        || fftAnalyserLowControl == nullptr
        || fftGeneralProcessorHeader == nullptr
        || fftDspFftSizeControl == nullptr
        || fftDspOverlapControl == nullptr
        || fftDspSlopeControl == nullptr
        || fftCorrelationImpactControl == nullptr
        || fftDualMonoLeftThresholdControl == nullptr
        || fftDualMonoLeftAdaptiveControl == nullptr
        || fftDualMonoRightThresholdControl == nullptr
        || fftDualMonoRightAdaptiveControl == nullptr
        || fftDynamicProcessorHeader == nullptr
        || fftDynamicModeControl == nullptr
        || fftCorrelationTypeControl == nullptr
        || fftDynamicDirectionControl == nullptr
        || fftCorrelationSmoothingControl == nullptr
        || fftDualMonoLinkButton == nullptr
        || fftAdaptiveSettingsHeader == nullptr
        || fftAdaptiveOffsetControl == nullptr
        || fftAdaptiveAttackControl == nullptr
        || fftAdaptiveHoldControl == nullptr
        || fftAdaptiveReleaseControl == nullptr
        || fftDetectorRangeHeader == nullptr
        || fftDetectorLowCutControl == nullptr
        || fftDetectorHighCutControl == nullptr
        || fftDeltaButton == nullptr)
    {
        return;
    }

    auto placeControl = [] (juce::Rectangle<int>& area, auto& control)
    {
        if (! control.isVisible())
        {
            control.setBounds({});
            return;
        }

        auto controlBounds = area.removeFromTop(control.getPreferredHeight());
        control.setBounds(controlBounds);

        if (! area.isEmpty())
            area.removeFromTop(verticalGap);
    };

    auto placeButton = [] (juce::Rectangle<int>& area, BoxTextButton& button)
    {
        if (! button.isVisible())
        {
            button.setBounds({});
            return;
        }

        auto buttonBounds = area.removeFromTop(rowHeight);
        button.setBounds(buttonBounds);

        if (! area.isEmpty())
            area.removeFromTop(verticalGap);
    };

    auto viewportBounds = bounds;

    if (! viewportBounds.isEmpty())
        viewportBounds.removeFromBottom(viewportToPotentiometerGap);

    auto fixedButtonsBounds = viewportBounds.removeFromBottom(rowHeight);

    fftDeltaButton->setBounds(fftDeltaButton->isVisible() ? fixedButtonsBounds : juce::Rectangle<int>{});

    if (! viewportBounds.isEmpty())
        viewportBounds.removeFromBottom(verticalGap);

    auto analyserBounds = viewportBounds.removeFromTop(fftInlineAnalyserHeight);

    if (fftAnalyserComponent != nullptr)
        fftAnalyserComponent->setBounds(analyserBounds);

    if (! viewportBounds.isEmpty())
        viewportBounds.removeFromTop(verticalGap);

    const auto contentHeight = getFftMainContentHeight() + moduleContentBottomGap;
    filterViewport.setBounds(viewportBounds);
    filterViewport.setVisible(true);
    filterContent.setSize(viewportBounds.getWidth(),
                          juce::jmax(viewportBounds.getHeight(), contentHeight));

    auto mainBounds = filterContent.getLocalBounds();
    placeControl(mainBounds, *fftAnalyserTimeControl);
    placeControl(mainBounds, *fftAnalyserHighControl);
    placeControl(mainBounds, *fftAnalyserLowControl);
    placeButton(mainBounds, *fftGeneralProcessorHeader);
    placeControl(mainBounds, *fftDspFftSizeControl);
    placeControl(mainBounds, *fftDspOverlapControl);
    placeButton(mainBounds, *fftDynamicProcessorHeader);
    placeControl(mainBounds, *fftDynamicModeControl);
    placeControl(mainBounds, *fftCorrelationTypeControl);
    placeControl(mainBounds, *fftDynamicDirectionControl);
    placeControl(mainBounds, *fftCorrelationSmoothingControl);
    placeControl(mainBounds, *fftAttackControl);
    placeControl(mainBounds, *fftReleaseControl);
    placeControl(mainBounds, *fftKneeControl);
    placeControl(mainBounds, *fftRatioControl);
    placeControl(mainBounds, *fftDspSlopeControl);

    if (! mainBounds.isEmpty())
        mainBounds.removeFromTop(verticalGap);

    placeControl(mainBounds, *fftDualMonoLeftThresholdControl);
    placeControl(mainBounds, *fftDualMonoLeftAdaptiveControl);
    placeControl(mainBounds, *fftFloorControl);
    placeControl(mainBounds, *fftCorrelationImpactControl);
    placeControl(mainBounds, *fftDualMonoRightThresholdControl);
    placeControl(mainBounds, *fftDualMonoRightAdaptiveControl);
    placeButton(mainBounds, *fftDualMonoLinkButton);

    if (! mainBounds.isEmpty())
        mainBounds.removeFromTop(verticalGap);

    placeButton(mainBounds, *fftAdaptiveSettingsHeader);
    placeControl(mainBounds, *fftAdaptiveOffsetControl);
    placeControl(mainBounds, *fftAdaptiveAttackControl);
    placeControl(mainBounds, *fftAdaptiveHoldControl);
    placeControl(mainBounds, *fftAdaptiveReleaseControl);

    if (! mainBounds.isEmpty())
        mainBounds.removeFromTop(verticalGap);

    placeButton(mainBounds, *fftDetectorRangeHeader);
    placeControl(mainBounds, *fftDetectorLowCutControl);
    placeControl(mainBounds, *fftDetectorHighCutControl);
}
