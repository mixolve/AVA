#include "LocalParameterControl.h"
#include "ParameterControlSupport.h"


LocalParameterControl::LocalParameterControl(const juce::String& titleText,
                                             const int editorDecimalsIn,
                                             const double minimum,
                                             const double maximum,
                                             const double interval,
                                             const double defaultValueIn,
                                             const double skewCentre,
                                             const bool supportsBrickwText,
                                             const bool supportsNoteTextIn)
    : defaultValue(defaultValueIn),
      editorDecimals(editorDecimalsIn),
      brickwSupported(supportsBrickwText),
      supportsNoteText(supportsNoteTextIn)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    titleButton = std::make_unique<BoxTextButton>(parameter_control_support::titleFocusColour);
    titleButton->setButtonText(titleText);
    titleButton->setTextJustification(juce::Justification::centredLeft);
    titleButton->setClearsParameterFocusOnMouseDown(false);
    titleButton->onClick = [this]
    {
        if (parameter_control_support::isTitleButtonFocused(titleButton.get(), &slider))
            return;

        if (interactionEnabled)
            parameter_control_support::focusTitleButton(titleButton.get(), &slider);
        else
            shell_parameter_focus::clearFocus(*this);

        clearKeyboardFocus(*this);
    };
    titleButton->setLongPressPromptActions([this]
    {
        if (! interactionEnabled)
        {
            parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);
            clearKeyboardFocus(*this);
            return;
        }

        setValue(defaultValue, true);
        clearKeyboardFocus(*this);
    });

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setInterceptsMouseClicks(false, false);
    slider.setScrollWheelEnabled(false);
    slider.setAlpha(0.0f);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);

    juce::NormalisableRange<double> range { minimum, maximum, interval };
    if (skewCentre > 0.0)
        range.setSkewForCentre(skewCentre);
    slider.setNormalisableRange(range);
    slider.setValue(defaultValue, juce::dontSendNotification);
    slider.textFromValueFunction = [this] (const double value)
    {
        return formatDisplayValue(value);
    };
    slider.valueFromTextFunction = [this] (const juce::String& text)
    {
        return parseText(text);
    };
    slider.onValueChange = [this]
    {
        if (valueBox != nullptr)
            valueBox->repaint();

        repaint();

        if (auto* parent = getParentComponent())
            parent->repaint();

        if (onValueChanged)
            onValueChanged();
    };

    valueBox = std::make_unique<ValueBoxComponent>(slider);
    valueBox->displayTextProvider = [this]
    {
        return formatDisplayValue(slider.getValue());
    };
    valueBox->editorTextProvider = [this]
    {
        return formatEditorValue();
    };
    valueBox->onBeforeShowEditor = [this]
    {
        if (interactionEnabled)
            parameter_control_support::focusTitleButton(titleButton.get(), &slider);
        else
            parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);
    };
    valueBox->textToValueParser = [this] (const juce::String& text)
    {
        return parseText(text);
    };
    valueBox->setOutlineColour(uiGrey500);
    valueBox->setHighlightColour(uiBlack);

    addAndMakeVisible(*titleButton);
    addChildComponent(slider);
    addAndMakeVisible(*valueBox);
}

int LocalParameterControl::getPreferredHeight() const noexcept
{
    return rowHeight;
}

LocalParameterControl::~LocalParameterControl()
{
    parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);
}

double LocalParameterControl::getValue() const noexcept
{
    return slider.getValue();
}

void LocalParameterControl::setValue(const double value, const bool sendNotification)
{
    slider.setValue(value, sendNotification ? juce::sendNotificationSync
                                            : juce::dontSendNotification);

    if (valueBox != nullptr)
        valueBox->repaint();

    repaint();

    if (auto* parent = getParentComponent())
        parent->repaint();
}

void LocalParameterControl::setValueRange(const double minimum,
                                           const double maximum,
                                           const double interval,
                                           const bool reversed)
{
    if (std::abs(slider.getMinimum() - minimum) <= 1.0e-9
        && std::abs(slider.getMaximum() - maximum) <= 1.0e-9
        && std::abs(slider.getInterval() - interval) <= 1.0e-9
        && valueRangeReversed == reversed)
    {
        return;
    }

    valueRangeReversed = reversed;

    if (! reversed)
    {
        slider.setRange(minimum, maximum, interval);
        return;
    }

    juce::NormalisableRange<double> range {
        minimum,
        maximum,
        [] (const double start, const double end, const double normalised)
        {
            return end - (normalised * (end - start));
        },
        [] (const double start, const double end, const double value)
        {
            return (end - juce::jlimit(start, end, value)) / (end - start);
        },
        [interval] (const double start, const double end, const double value)
        {
            const auto snapped = start + (std::round((value - start) / interval) * interval);
            return juce::jlimit(start, end, snapped);
        }
    };
    range.interval = interval;
    slider.setNormalisableRange(range);
}

void LocalParameterControl::setDefaultValue(const double value)
{
    defaultValue = slider.getNormalisableRange().snapToLegalValue(value);
}

void LocalParameterControl::setTitleText(const juce::String& text)
{
    if (titleButton == nullptr || titleButton->getButtonText() == text)
        return;

    titleButton->setButtonText(text);
    titleButton->repaint();
}

void LocalParameterControl::setOverrideText(const juce::String& text)
{
    if (overrideText == text)
        return;

    overrideText = text;

    if (valueBox != nullptr)
        valueBox->repaint();
}

void LocalParameterControl::clearOverrideText()
{
    if (overrideText.isEmpty())
        return;

    overrideText.clear();

    if (valueBox != nullptr)
        valueBox->repaint();
}

void LocalParameterControl::setInteractionEnabled(const bool shouldEnable)
{
    interactionEnabled = shouldEnable;

    if (! interactionEnabled)
        parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);

    if (titleButton != nullptr)
    {
        titleButton->setEnabled(shouldEnable);
        titleButton->setPressFillEnabled(shouldEnable);
    }

    if (valueBox != nullptr)
        valueBox->setInteractionEnabled(shouldEnable);
}

juce::Rectangle<int> LocalParameterControl::getValueBounds() const noexcept
{
    return valueBox != nullptr ? valueBox->getBounds() : juce::Rectangle<int>();
}

void LocalParameterControl::resized()
{
    auto row = getLocalBounds();
    const auto titleWidth = getScaledParameterNameWidth(row.getWidth());
    titleButton->setBounds(row.removeFromLeft(titleWidth));
    row.removeFromLeft(parameterGap);
    slider.setBounds(row);

    if (valueBox != nullptr)
        valueBox->setBounds(row);
}

double LocalParameterControl::parseText(const juce::String& text) const
{
    if (brickwSupported && text.trim().containsIgnoreCase("brick"))
        return 96.1;

    return supportsNoteText ? parseFrequencyInput(text)
                            : parseNumericInput(text);
}

juce::String LocalParameterControl::formatDisplayValue(const double value) const
{
    if (overrideText.isNotEmpty())
        return overrideText;

    if (brickwSupported && value > 96.0)
        return "BRICKW";

    return formatFixedDecimalValue(value, editorDecimals);
}

juce::String LocalParameterControl::formatEditorValue() const
{
    if (overrideText.isNotEmpty())
        return overrideText;

    if (brickwSupported && slider.getValue() > 96.0)
        return "BRICKW";

    return formatFixedDecimalValue(slider.getValue(), editorDecimals);
}
