#include "ParameterControl.h"
#include "ParameterEditing.h"
#include "ValueEditor.h"
#include "../modules/shared/ParameterLayout.h"

#include <utility>


ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                   const juce::String& parameterIdIn,
                                   const juce::String& titleTextIn,
                                   const int editorDecimalsIn)
    : titleText(titleTextIn),
      parameterId(parameterIdIn),
      parameter(state.getParameter(parameterIdIn)),
      editorDecimals(editorDecimalsIn)
{
    jassert(parameter != nullptr);

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    parameter_control_support::configureHiddenSlider(slider);
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

        listeners.call(&Listener::parameterControlChanged, *this);
        if (onValueChanged)
            onValueChanged();
    };

    valueBox = std::make_unique<ValueBoxComponent>(slider);
    valueBox->displayTextProvider = [this]
    {
        return formatDisplayValue(valueBox->getDisplayedValue());
    };
    valueBox->editorTextProvider = [this]
    {
        return formatEditorValue();
    };
    valueBox->textToValueParser = [this] (const juce::String& text)
    {
        return parseText(text);
    };

    addChildComponent(slider);
    addAndMakeVisible(*valueBox);
    valueBox->onResetRequested = [this]
    {
        if (!interactionEnabled || !isEnabled()) return;
        if (onResetRequested != nullptr) onResetRequested();
        else if (parameter != nullptr)
            slider.setValue(parameter->convertFrom0to1(parameter->getDefaultValue()), juce::sendNotificationSync);
        clearKeyboardFocus(*this);
    };
    valueBox->setIdleTextProvider([this] { return titleText; });
    rebind(state);
}


int ParameterControl::getPreferredHeight() const noexcept
{
    return getSquareControlSize(*this);
}

double ParameterControl::getValue() const noexcept
{
    return slider.getValue();
}

void ParameterControl::detach() noexcept
{
    if (valueBox != nullptr)
    {
        valueBox->setNumericControlEnabled(false);
        valueBox->bottomIndicators = {};
    }
    attachment.reset();
    parameter = nullptr;
    boundState = nullptr;
}

void ParameterControl::rebind(juce::AudioProcessorValueTreeState& state)
{
    rebind(state, parameterId);
}

void ParameterControl::rebind(juce::AudioProcessorValueTreeState& state,
                              const juce::String& parameterIdIn)
{
    if (valueBox != nullptr) valueBox->setNumericControlEnabled(false);
    attachment.reset();
    parameterId = parameterIdIn;
    boundState = &state;
    parameter = state.getParameter(parameterId);
    parameter_control_support::bindParameterIndicators(*valueBox, *this,
        [this] { return parameterId; }, [this] { return parameter; }, state);
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

    customSliderMapping = false;
    attachment = std::make_unique<Attachment>(state, parameterId, slider);
    updateSliderMapping();
    if (valueBox != nullptr)
    {
        const auto* choice = dynamic_cast<juce::AudioParameterChoice*>(parameter);
        valueBox->setDiscreteChoiceIndicator(choice != nullptr ? choice->choices.size()
            : dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr ? 2 : 0);
    }
    if (valueBox != nullptr)
        valueBox->setNumericControlEnabled(parameter != nullptr
            && dynamic_cast<juce::AudioParameterChoice*>(parameter) == nullptr
            && dynamic_cast<juce::AudioParameterBool*>(parameter) == nullptr);
    repaint();
}

void ParameterControl::setValue(const double value, const bool sendNotification)
{
    parameter_control_support::applySliderValue(*this, slider, valueBox.get(), value, sendNotification);
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
    valueBox->setInteractionEnabled(shouldEnable);
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

void ParameterControl::setTitleText(const juce::String& text)
{
    if (titleText == text) return;
    titleText = text;
    valueBox->repaint();
}

void ParameterControl::setValuePresentation(ava::modules::parameters::NumericPresentation presentation)
{
    if (valuePresentation == presentation) return;
    valuePresentation = presentation;
    updateSliderMapping();
    // An open value editor must change units too, so old rate text cannot be
    // committed as semitones. Keep the user's selection and caret position.
    const auto refresh = [this](auto&& self, juce::Component& component) -> void {
        if (auto* editor = dynamic_cast<ValueEditor*>(&component)) editor->refreshPresentationFor(slider);
        for (auto* child : component.getChildren()) self(self, *child);
    };
    refresh(refresh, *getTopLevelComponent());
    if (valueBox != nullptr) valueBox->repaint();
    repaint();
}

void ParameterControl::updateSliderMapping()
{
    if (parameter == nullptr) return;
    if (valuePresentation.mapping != ava::modules::parameters::NumericPresentation::Mapping::rateToSemitones) {
        if (customSliderMapping) slider.setNormalisableRange(ordinarySliderRange);
        customSliderMapping = false;
        return;
    }
    if (!customSliderMapping) ordinarySliderRange = slider.getNormalisableRange();
    customSliderMapping = true;
    const auto range = parameter->getNormalisableRange();
    const auto presentation = valuePresentation;
    const auto displayStart = presentation.toDisplay(range.start);
    const auto displayEnd = presentation.toDisplay(range.end);
    auto sliderRange = juce::NormalisableRange<double>(range.start, range.end,
        [range, presentation, displayStart, displayEnd](double, double, double position) {
            return presentation.mapping == ava::modules::parameters::NumericPresentation::Mapping::rateToSemitones
                ? presentation.toPlain(displayStart + position * (displayEnd - displayStart))
                : double(range.convertFrom0to1(float(position)));
        },
        [range, presentation, displayStart, displayEnd](double, double, double value) {
            return presentation.mapping == ava::modules::parameters::NumericPresentation::Mapping::rateToSemitones
                ? (presentation.toDisplay(value) - displayStart) / (displayEnd - displayStart)
                : double(range.convertTo0to1(float(value)));
        },
        [range](double, double, double value) { return double(range.snapToLegalValue(float(value))); });
    sliderRange.interval = range.interval;
    slider.setNormalisableRange(sliderRange);
}

void ParameterControl::resized()
{
    valueBox->setBounds(getLocalBounds());
    slider.setBounds(valueBox->getBounds());
    layoutBottomAccessory(valueBox->getBounds());

}

double ParameterControl::parseText(const juce::String& text) const
{
    if (const auto* value = dynamic_cast<const ava::modules::parameters::ValueParameter*>(parameter))
        return value->parsePlainValue(text, valuePresentation);
    if (const auto* choices = dynamic_cast<const juce::AudioParameterChoice*>(parameter))
        return findNearestChoiceIndex(parseNumericInput(text), choices->choices, text);
    return parameter != nullptr ? parameter->convertFrom0to1(parameter->getValueForText(text)) : parseNumericInput(text);
}

juce::String ParameterControl::formatNumericValue(const double value) const
{
    if (const auto* formatted = dynamic_cast<const ava::modules::parameters::ValueParameter*>(parameter))
        return formatted->formatUiValue(value, slider.getMinimum(), slider.getMaximum(), editorDecimals, true, valuePresentation);
    return formatRangeDecimalValue(value, slider.getMinimum(), slider.getMaximum(), editorDecimals);
}

juce::String ParameterControl::formatDisplayValue(const double value) const
{
    if (overrideText.isNotEmpty()) return overrideText;
    if (const auto* formatted = dynamic_cast<const ava::modules::parameters::ValueParameter*>(parameter))
        return formatted->formatUiValue(value, slider.getMinimum(), slider.getMaximum(), editorDecimals, false, valuePresentation);
    return parameter != nullptr ? parameter->getText(parameter->convertTo0to1(float(value)), 64) : formatNumericValue(value);
}

juce::String ParameterControl::formatEditorValue() const
{
    if (overrideText.isNotEmpty()) return overrideText;
    if (dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr
        || dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr)
        return formatDisplayValue(slider.getValue());
    return formatNumericValue(slider.getValue());
}

void ParameterControl::setDiscreteChoiceIndicator(int count, int selected)
{
    if (valueBox != nullptr) valueBox->setDiscreteChoiceIndicator(count, selected);
}

AccessoryButton::AccessoryButton(const juce::String& label)
    : juce::Button(label)
{
    onStateChange=[this]{repaintValueOverlay(*this);};
    setName("accessory-"+label.toLowerCase());
    setButtonText(label);
    getProperties().set("embeddedControlButton",true);
    // Ctrl+click must pin this auxiliary parameter, not its parent field.
    getProperties().set("independentDockSelection", true);
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
}

AccessoryButton::~AccessoryButton()
{
    detach();
    if (thresholdSource != nullptr) thresholdSource->removeListener(this);
}

void AccessoryButton::detach() noexcept
{
    attachment.reset();
    parameter = nullptr;
    boundState = nullptr;
    getProperties().remove("oscParameterId");
}

void AccessoryButton::bindParameter(juce::AudioProcessorValueTreeState& state,
                                        const juce::String& id, bool cycleChoices)
{
    if (auto* target = state.getParameter(id))
    {
        bindParameter(*target, cycleChoices, state.undoManager);
        boundState = &state;
    }
    else { detach(); jassertfalse; }
}

void AccessoryButton::bindParameter(juce::RangedAudioParameter& target, bool cycleChoices, juce::UndoManager* undo)
{
    detach();
    parameter = &target;
    getProperties().set("oscParameterId", parameter->getParameterID());
    attachment = std::make_unique<juce::ParameterAttachment>(*parameter, [this] (float)
    {
        repaintValueOverlay(*this);
        if (onValueChanged != nullptr) onValueChanged();
    }, undo);
    attachment->sendInitialUpdate();
    onClick = [this, cycleChoices]
    {
        if (parameter == nullptr || attachment == nullptr || !isEnabled()) return;
        float next = 0.0f;
        if (thresholdSource != nullptr && thresholdSource->getValue() < muteThreshold)
            thresholdSource->setValue(muteThreshold, true);
        else if (cycleChoices)
        {
            const auto& range = parameter->getNormalisableRange();
            const auto current = parameter->convertFrom0to1(parameter->getValue());
            next = reverseCycleOnClick
                ? (current <= range.start ? range.end : current - 1.0f)
                : (current >= range.end ? range.start : current + 1.0f);
        }
        else next = parameter->getValue() >= 0.5f ? 0.0f : 1.0f;
        attachment->setValueAsCompleteGesture(next);
    };
}

void AccessoryButton::bindLowerMuteThreshold(ParameterControl& gain, double threshold)
{
    if (thresholdSource != nullptr) thresholdSource->removeListener(this);
    thresholdSource = &gain;
    muteThreshold = threshold;
    gain.addListener(this);
}

void AccessoryButton::placeIn(juce::Rectangle<int> valueBounds)
{
    const auto* field = findParentComponentOfClass<ParameterField>();
    const auto zone = field != nullptr ? field->getBottomTextZone(valueBounds)
                                       : valueBounds.reduced(RangeIndicator::edgeInset).removeFromBottom(RangeIndicator::compactMarkerSize);
    // Mouse hit testing, button fill and the hover frame all use this exact
    // indicator rectangle. An invisible hit area above it made presses
    // and hover highlights appear before the pointer reached the button.
    setBounds(zone);
    toFront(false);
}

bool AccessoryButton::isActive() const
{
    const auto active = (parameter!=nullptr && parameter->convertFrom0to1(parameter->getValue())>=.5f)
        || (thresholdSource!=nullptr && thresholdSource->getValue()<muteThreshold);
    return active;
}

void AccessoryButton::paintButton(juce::Graphics& graphics, bool hovered, bool pressed)
{
    // The same filled bottom-row text button as the multi-action
    // buttons, aligned with the host-slot and bypass indicators (2px gaps).
    const auto* field = findParentComponentOfClass<ParameterField>();
    juce::ignoreUnused(field);
    const auto textBounds = getLocalBounds();
    // Distinguish disabled (#888888), idle (#BBBBBB) and active (#FFFFFF).
    graphics.setColour(!isEnabled() ? uiIndicatorDisabled : (isActive() || pressed) ? uiWhite : uiIndicatorInactive);
    graphics.fillRect(textBounds);
    const auto inverted = isEnabled() && !pressed && isHoverInversionReady(*this, hovered && isEnabled(), "indicator");
    if (inverted) { graphics.setColour(juce::Colour(0xff666666)); graphics.fillRect(textBounds); }
    const auto textColour = inverted ? uiWhite : juce::Colour(0xff222222);
    const auto text = getButtonText();
    if (drawEmbeddedButtonText(graphics, text, textBounds, textColour))
        scheduleMarqueeRepaint(*this, marqueeRepaintPending);
    if (isManualControlSelected(*this))
        paintUiFrame(graphics, textBounds.toFloat(), uiWhite,
            ControlAppearance::pixelThickness(graphics, 1.0f), float(defaultUiCornerRadius));
    // The existing indicator hover outline is drawn by ParameterField after
    // children, so its expanded frame is never clipped at this button's edge.
    juce::ignoreUnused(hovered, pressed);
}

void AccessoryButton::mouseMove(const juce::MouseEvent& event)
{
    juce::Button::mouseMove(event);
    if (auto* field = findParentComponentOfClass<ParameterField>())
        field->repaintBottomHover(*this);
}

void AccessoryButton::mouseDown(const juce::MouseEvent& event)
{
    if (!selectionGesture.begin(*this, event)) juce::Button::mouseDown(event);
}

void AccessoryButton::mouseDrag(const juce::MouseEvent& event)
{
    if (!selectionGesture.isActive()) juce::Button::mouseDrag(event);
}

void AccessoryButton::mouseUp(const juce::MouseEvent& event)
{
    if (!selectionGesture.finish(event))
    {
        const juce::ScopedValueSetter<bool> reverseClick(reverseCycleOnClick,
            event.mods.isShiftDown() && !event.mods.isCtrlDown() && !event.mods.isAltDown());
        juce::Button::mouseUp(event);
    }
}

