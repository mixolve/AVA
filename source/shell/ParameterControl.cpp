#include "ParameterControl.h"
#include "ParameterControlSupport.h"

#include <utility>

ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                   const juce::String& parameterIdIn,
                                   const juce::String& titleText,
                                   const int editorDecimalsIn)
    : parameterId(parameterIdIn),
      parameter(state.getParameter(parameterIdIn)),
      editorDecimals(editorDecimalsIn)
{
    jassert(parameter != nullptr);

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

        if (valueClickAction != nullptr)
        {
            shell_parameter_focus::clearFocus(*this);
            clearKeyboardFocus(*this);
            return;
        }

        if (interactionEnabled && parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction))
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

        if (onTitleClick)
        {
            onTitleClick();
        }
        else if (parameter != nullptr)
        {
            slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()),
                            juce::sendNotificationSync);
        }

        clearKeyboardFocus(*this);
    }, [this]
    {
        parameter_control_support::assignTitleToHostSlot(*this, titleButton.get(), parameterId, parameter);
        clearKeyboardFocus(*this);
    });

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setInterceptsMouseClicks(false, false);
    slider.setScrollWheelEnabled(false);
    slider.setAlpha(0.0f);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);
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

    if (parameter != nullptr)
        attachment = std::make_unique<Attachment>(state, parameterIdIn, slider);
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
        if (interactionEnabled && parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction))
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

ParameterControl::~ParameterControl()
{
    parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);
}

int ParameterControl::getPreferredHeight() const noexcept
{
    return rowHeight;
}

double ParameterControl::getValue() const noexcept
{
    return slider.getValue();
}

void ParameterControl::detach() noexcept
{
    attachment.reset();
    parameter = nullptr;
}

void ParameterControl::rebind(juce::AudioProcessorValueTreeState& state)
{
    rebind(state, parameterId);
}

void ParameterControl::rebind(juce::AudioProcessorValueTreeState& state,
                              const juce::String& parameterIdIn)
{
    attachment.reset();
    parameterId = parameterIdIn;
    parameter = state.getParameter(parameterId);
    jassert(parameter != nullptr);

    if (parameter != nullptr)
    {
        const auto& range = parameter->getNormalisableRange();
        slider.setNormalisableRange({ static_cast<double>(range.start),
                                      static_cast<double>(range.end),
                                      static_cast<double>(range.interval),
                                      static_cast<double>(range.skew),
                                      range.symmetricSkew });
    }

    if (! parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction))
        parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);

    if (titleButton != nullptr)
        titleButton->setPressFillEnabled(interactionEnabled && parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction));

    attachment = std::make_unique<Attachment>(state, parameterId, slider);
    repaint();
}

bool ParameterControl::isBoundTo(const juce::String& parameterIdIn) const noexcept
{
    return parameterId == parameterIdIn;
}

void ParameterControl::setValue(const double value, const bool sendNotification)
{
    slider.setValue(value, sendNotification ? juce::sendNotificationSync
                                            : juce::dontSendNotification);

    if (valueBox != nullptr)
        valueBox->repaint();

    repaint();

    if (auto* parent = getParentComponent())
        parent->repaint();
}

void ParameterControl::setOverrideText(const juce::String& text)
{
    if (overrideText == text)
        return;

    overrideText = text;

    if (valueBox != nullptr)
        valueBox->repaint();
}

void ParameterControl::clearOverrideText()
{
    if (overrideText.isEmpty())
        return;

    overrideText.clear();

    if (valueBox != nullptr)
        valueBox->repaint();
}

void ParameterControl::setInteractionEnabled(const bool shouldEnable)
{
    interactionEnabled = shouldEnable;

    if (! interactionEnabled || ! parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction))
        parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);

    if (titleButton != nullptr)
    {
        titleButton->setEnabled(shouldEnable);
        titleButton->setPressFillEnabled(shouldEnable && parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction));
    }

    if (valueBox != nullptr)
        valueBox->setInteractionEnabled(shouldEnable);
}

void ParameterControl::setValueClickAction(std::function<void()> action)
{
    valueClickAction = std::move(action);

    if (valueBox != nullptr)
        valueBox->setCustomPromptAction(valueClickAction);

    if (! parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction))
        parameter_control_support::clearFocusedTitleButton(titleButton.get(), &slider);

    if (titleButton != nullptr)
        titleButton->setPressFillEnabled(interactionEnabled && parameter_control_support::canUseFocusedPotentiometer(parameter, valueClickAction));
}

void ParameterControl::setValueRange(const double minimum, const double maximum, const double interval)
{
    if (std::abs(slider.getMinimum() - minimum) <= 1.0e-9
        && std::abs(slider.getMaximum() - maximum) <= 1.0e-9
        && std::abs(slider.getInterval() - interval) <= 1.0e-9)
    {
        return;
    }

    slider.setRange(minimum, maximum, interval);
}

void ParameterControl::setTitleWidthOverride(const int width) noexcept
{
    titleWidthOverride = width;
    resized();
}

void ParameterControl::setValueLeadingInset(const int width) noexcept
{
    valueLeadingInset = juce::jmax(0, width);
    resized();
}

void ParameterControl::setTitleText(const juce::String& text)
{
    if (titleButton == nullptr || titleButton->getButtonText() == text)
        return;

    titleButton->setButtonText(text);
    titleButton->repaint();
}

void ParameterControl::setValueTextTransform(std::function<juce::String(double)> displayFormatter,
                                             std::function<juce::String(double)> editorFormatter,
                                             std::function<double(const juce::String&)> textParser)
{
    customDisplayFormatter = std::move(displayFormatter);
    customEditorFormatter = std::move(editorFormatter);
    customTextParser = std::move(textParser);

    if (valueBox != nullptr)
        valueBox->repaint();

    repaint();
}

void ParameterControl::clearValueTextTransform()
{
    if (customDisplayFormatter == nullptr && customEditorFormatter == nullptr && customTextParser == nullptr)
        return;

    customDisplayFormatter = nullptr;
    customEditorFormatter = nullptr;
    customTextParser = nullptr;

    if (valueBox != nullptr)
        valueBox->repaint();

    repaint();
}

juce::Rectangle<int> ParameterControl::getValueBounds() const noexcept
{
    return valueBox != nullptr ? valueBox->getBounds() : juce::Rectangle<int>();
}

void ParameterControl::setTitleMoveArmedAction(std::function<void()> action)
{
    if (titleButton != nullptr)
        titleButton->onMoveArmed = std::move(action);
}

void ParameterControl::resized()
{
    auto row = getLocalBounds();
    const auto defaultTitleWidth = getScaledParameterNameWidth(row.getWidth());
    const auto titleWidth = juce::jlimit(0, row.getWidth(), titleWidthOverride >= 0 ? titleWidthOverride : defaultTitleWidth);
    titleButton->setBounds(row.removeFromLeft(titleWidth));
    if (titleWidth > 0)
        row.removeFromLeft(juce::jmin(parameterGap, row.getWidth()));
    row.removeFromLeft(juce::jmin(valueLeadingInset, row.getWidth()));
    slider.setBounds(row);

    if (valueBox != nullptr)
        valueBox->setBounds(row);
}

double ParameterControl::parseText(const juce::String& text) const
{
    if (text.trim().equalsIgnoreCase("MUTED"))
        return slider.getMinimum();

    if (customTextParser != nullptr)
        return customTextParser(text);

    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*>(parameter))
        return findNearestChoiceIndex(parseNumericInput(text), choiceParameter->choices, text);

    return supportsNoteFrequencyInput(parameterId) ? parseFrequencyInput(text)
                                                   : parseNumericInput(text);
}

juce::String ParameterControl::formatDisplayValue(const double value) const
{
    if (overrideText.isNotEmpty())
        return overrideText;

    if (customDisplayFormatter != nullptr)
        return customDisplayFormatter(value);

    if (parameter == nullptr)
        return formatFixedDecimalValue(value, editorDecimals);

    if (dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr)
        return parameter->getText(parameter->convertTo0to1(static_cast<float>(value)), 64).trim();

    const auto parameterText = parameter->getText(parameter->convertTo0to1(static_cast<float>(value)), 64).trim();
    const auto numericText = parameterText.retainCharacters("0123456789+-.");

    if (numericText.isNotEmpty())
    {
        if (parameterText.containsChar('%'))
            return formatFixedDecimalValue(parseNumericInput(parameterText), editorDecimals) + "%";

        return formatFixedDecimalValue(parseNumericInput(parameterText), editorDecimals);
    }

    if (parameterText.isNotEmpty())
        return parameterText;

    return formatFixedDecimalValue(value, editorDecimals);
}

juce::String ParameterControl::formatEditorValue() const
{
    if (overrideText.isNotEmpty())
        return overrideText;

    if (customEditorFormatter != nullptr)
        return customEditorFormatter(slider.getValue());

    if (parameter != nullptr && dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr)
        return parameter->getText(parameter->convertTo0to1(static_cast<float>(slider.getValue())), 64).trim();

    if (parameter != nullptr)
    {
        const auto parameterText = parameter->getText(parameter->convertTo0to1(static_cast<float>(slider.getValue())), 64).trim();
        const auto numericText = parameterText.retainCharacters("0123456789+-.");

        if (numericText.isEmpty() && parameterText.isNotEmpty())
            return parameterText;
    }

    return formatFixedDecimalValue(slider.getValue(), editorDecimals);
}
