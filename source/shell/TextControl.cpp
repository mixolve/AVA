#include "TextControl.h"

TextControl::TextControl(const juce::String& title, const juce::String& defaultValueIn, bool editableIn)
    : value(defaultValueIn), defaultValue(defaultValueIn), editable(editableIn), titleText(title), field(model)
{
    parameter_control_support::configureHiddenSlider(model);
    field.setNumericControlEnabled(false);
    field.setTextInputEnabled(editable);
    field.setIdleTextProvider([this] { return titleText; });
    field.displayTextProvider = [this] { return value.isEmpty() ? juce::String::charToString(0x2014) : value; };
    field.editorTextProvider = [this] { return value; };
    field.textCommit = [this](const juce::String& text) { return setValue(text, true); };
    if (editable) field.onResetRequested = [this] { setValue(defaultValue, true); };
    addAndMakeVisible(field);
}

bool TextControl::setValue(const juce::String& text, bool notify)
{
    if (notify && onValueChanged != nullptr && !onValueChanged(text)) return false;
    value = text;
    field.repaint();
    return true;
}

void TextControl::resized()
{
    field.setBounds(getLocalBounds());
}

void TextControl::setTitleText(const juce::String& title)
{
    titleText = title;
    field.repaint();
}

void TextControl::bindParameterIndicators(juce::AudioProcessorValueTreeState* state, const juce::String& parameterId)
{
    if (state != nullptr)
    {
        parameter_control_support::bindParameterIndicators(field, *this, [parameterId] { return parameterId; },
            [state, parameterId] { return state->getParameter(parameterId); }, *state);
        getProperties().set(juce::Identifier("oscParameterId"), parameterId);
    }
    else
    {
        field.bottomIndicators = {};
        getProperties().remove(juce::Identifier("oscParameterId"));
    }
}
