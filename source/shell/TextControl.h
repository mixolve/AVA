#pragma once

#include "ParameterEditing.h"

class TextControl final : public ParameterField
{
public:
    TextControl(const juce::String& title, const juce::String& defaultValue, bool editable = true);
    void setTitleText(const juce::String& title);
    void setFittedText(bool enabled = true) { field.getProperties().set("fittedText", enabled); field.repaint(); }
    juce::String getTitleText() const override { return titleText; }
    void bindParameterIndicators(juce::AudioProcessorValueTreeState* state, const juce::String& parameterId = {});
    bool setValue(const juce::String& value, bool notify = false);
    const juce::String& getValue() const noexcept { return value; }
    bool isEditable() const noexcept { return editable; }
    void resized() override;
    std::function<bool(const juce::String&)> onValueChanged;

private:
    juce::String value, defaultValue;
    bool editable = true;
    juce::Slider model;
    juce::String titleText;
    ValueBoxComponent field;
};
