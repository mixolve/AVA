#pragma once

#include "Editor.h"
#include "DisclosureAnimation.h"
#include "ChoiceControl.h"
#include "LocalParameterControl.h"
#include "ParameterControl.h"
#include "../modules/eql/Processor.h"

struct AvaAudioProcessorEditor::FilterSection
{
    static constexpr int accentFrameThickness = 2;
    static constexpr int contentInset = uiGap + accentFrameThickness;

    class AccentFrame final : public juce::Component
    {
    public:
        AccentFrame() { setInterceptsMouseClicks(false, false); setWantsKeyboardFocus(false); }
        void setContour(const int headerLeftIn, const int bodyTopIn)
        {
            headerLeft = static_cast<float>(headerLeftIn);
            bodyTop = static_cast<float>(bodyTopIn);
            repaint();
        }
        void paint(juce::Graphics& graphics) override
        {
            if (getWidth() < 2 || getHeight() < 2) return;
            // Inset the centreline by half the stroke: every edge stays inside
            // the header/body outline, including the step below the header.
            constexpr float outlineThickness=1.0f;
            constexpr float inset=outlineThickness*.5f;
            const auto right = static_cast<float>(getWidth()) - inset;
            const auto bottom = static_cast<float>(getHeight()) - inset;
            juce::Path contour;
            contour.startNewSubPath(headerLeft + inset, inset);
            contour.lineTo(right, inset);
            contour.lineTo(right, bottom);
            contour.lineTo(inset, bottom);
            contour.lineTo(inset, bodyTop + inset);
            contour.lineTo(headerLeft + inset, bodyTop + inset);
            contour.closeSubPath();
            graphics.setColour(uiGreyLight);
            graphics.strokePath(contour,juce::PathStrokeType(outlineThickness));
        }
    private:
        float headerLeft = 0.0f, bodyTop = 0.0f;
    };

    using FilterType = EqlModuleProcessor::FilterType;

    explicit FilterSection(juce::AudioProcessorValueTreeState& state, int filterIndex);

    void detach() noexcept;
    void rebind(juce::AudioProcessorValueTreeState& state);
    FilterType getFilterType() const noexcept;
    bool isPhaseEnabled() const noexcept;
    double getFrequency() const noexcept;
    void updateFrequencyRangeForType();
    void setGainDisplaysDegrees(bool shouldDisplayDegrees);
    void updatePhaseForType(bool normalizeSelection);
    void refreshTypeDependentControls(bool normalizePhaseSelection = true);

    int expandedBodyHeight() const noexcept { return 2 * contentInset + 5 * (rowHeight + verticalGap); }
    int visibleBodyHeight() const noexcept
    { return disclosure.targetExpanded() || disclosure.value() > 0.0 ? expandedBodyHeight() : 0; }
    AccentFrame accentFrame;
    BackgroundClickComponent body;
    DisclosureAnimation disclosure;
    bool disclosureInitialised = false;
    std::unique_ptr<BoxTextButton> header;
    std::unique_ptr<ChoiceControl> typeControl;
    std::unique_ptr<BoxTextButton> phaseButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> phaseAttachment;
    std::unique_ptr<ChoiceControl> orderControl;
    std::unique_ptr<ParameterControl> frequencyControl;
    std::unique_ptr<ParameterControl> bandwidthControl;
    std::unique_ptr<ParameterControl> gainControl;
    std::unique_ptr<BoxTextButton> bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    juce::AudioParameterChoice* typeParameter = nullptr;
    juce::AudioParameterBool* phaseParameter = nullptr;
    juce::AudioParameterFloat* frequencyParameter = nullptr;
    juce::AudioParameterFloat* bandwidthParameter = nullptr;
    juce::AudioParameterChoice* orderParameter = nullptr;
    juce::AudioParameterFloat* gainParameter = nullptr;
    int filterIndex = 0;
    bool expanded = false;
    bool gainDisplaysDegrees = false;

};
