#pragma once

#include "ParameterControlSupport.h"

#include <functional>
#include <memory>

class ParameterControl final : public juce::Component
{
public:
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterIdIn,
                     const juce::String& titleText,
                     int editorDecimalsIn);
    ~ParameterControl() override;

    int getPreferredHeight() const noexcept;
    double getValue() const noexcept;
    void detach() noexcept;
    void rebind(juce::AudioProcessorValueTreeState& state);
    void rebind(juce::AudioProcessorValueTreeState& state, const juce::String& parameterIdIn);
    bool isBoundTo(const juce::String& parameterIdIn) const noexcept;
    const juce::String& getParameterId() const noexcept { return parameterId; }
    void setValue(double value, bool sendNotification);
    void setOverrideText(const juce::String& text);
    void clearOverrideText();
    void setInteractionEnabled(bool shouldEnable);
    void setValueClickAction(std::function<void()> action);
    void setValueRange(double minimum, double maximum, double interval);
    void setTitleWidthOverride(int width) noexcept;
    void setValueLeadingInset(int width) noexcept;
    void setTitleText(const juce::String& text);
    void setValueTextTransform(std::function<juce::String(double)> displayFormatter,
                               std::function<juce::String(double)> editorFormatter,
                               std::function<double(const juce::String&)> textParser);
    void clearValueTextTransform();
    juce::Rectangle<int> getValueBounds() const noexcept;
    void setTitleMoveArmedAction(std::function<void()> action);
    void resized() override;

    std::function<void()> onValueChanged;
    std::function<void()> onTitleClick;

private:
    juce::String formatDisplayValue(double value) const;
    juce::String formatEditorValue() const;
    double parseText(const juce::String& text) const;

    juce::String parameterId;
    juce::RangedAudioParameter* parameter = nullptr;
    const int editorDecimals = 2;
    std::unique_ptr<BoxTextButton> titleButton;
    juce::Slider slider;
    std::unique_ptr<ValueBoxComponent> valueBox;
    std::unique_ptr<Attachment> attachment;
    juce::String overrideText;
    std::function<void()> valueClickAction;
    std::function<juce::String(double)> customDisplayFormatter;
    std::function<juce::String(double)> customEditorFormatter;
    std::function<double(const juce::String&)> customTextParser;
    int titleWidthOverride = -1;
    int valueLeadingInset = 0;
    bool interactionEnabled = true;
};
