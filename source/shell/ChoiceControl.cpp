#include "ChoiceControl.h"
#include "ParameterControl.h"

ChoiceField::ChoiceField(const juce::String& title) : titleText(title)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    parameter_control_support::configureHiddenSlider(choiceSlider);
    choiceSlider.setRange(0, 1, 1);
    valueBox = std::make_unique<ValueBoxComponent>(choiceSlider);
    valueBox->setNumericControlEnabled(false);
    valueBox->isDiscreteChoiceEnabled = [this](int index)
    {
        return juce::isPositiveAndBelow(index, int(itemEnabled.size()))
            && itemEnabled[static_cast<size_t>(index)];
    };
    valueBox->displayTextProvider = [this]
    {
        const auto index = juce::roundToInt(valueBox->getDisplayedValue());
        const auto text = overrideText.isNotEmpty() ? overrideText
            : juce::isPositiveAndBelow(index, items.size()) ? items[index] : juce::String();
        return displayFormatter != nullptr ? displayFormatter(index, text) : text;
    };
    valueBox->setIdleTextProvider([this] { return titleText; });
    valueBox->onResetRequested = [this] { resetSelection(); };
    choiceSlider.onValueChange = [this]
    {
        auto index = getDisplayIndex();
        const auto previous = previousIndex;
        if (updatingSelection) { previousIndex = index; return; }
        if (!juce::isPositiveAndBelow(index, items.size())) return;
        if (!itemEnabled[static_cast<size_t>(index)])
        {
            const auto direction = index >= previousIndex ? 1 : -1;
            while (juce::isPositiveAndBelow(index, items.size()) && !itemEnabled[static_cast<size_t>(index)])
                index += direction;
            if (!juce::isPositiveAndBelow(index, items.size()))
            {
                index = getDisplayIndex();
                while (juce::isPositiveAndBelow(index, items.size()) && !itemEnabled[static_cast<size_t>(index)])
                    index -= direction;
                if (!juce::isPositiveAndBelow(index, items.size())) index = previous;
            }
            const juce::ScopedValueSetter<bool> guard(updatingSelection, true);
            choiceSlider.setValue(index, juce::sendNotificationSync);
        }
        if (index == previous) return;
        previousIndex = index;
        if (selectionChanged != nullptr) selectionChanged(index);
    };
    addChildComponent(choiceSlider);
    addAndMakeVisible(*valueBox);
}

void ChoiceField::setItems(const juce::StringArray& newItems)
{
    items = newItems;
    itemEnabled.assign(static_cast<size_t>(items.size()), true);
    const juce::ScopedValueSetter<bool> guard(updatingSelection, true);
    choiceSlider.setRange(0, juce::jmax(1, items.size() - 1), 1);
    valueBox->setDiscreteChoiceIndicator(items.size());
}

void ChoiceField::setDisplayIndex(int index)
{
    const juce::ScopedValueSetter<bool> guard(updatingSelection, true);
    choiceSlider.setValue(juce::jlimit(0, juce::jmax(0, items.size() - 1), index), juce::sendNotificationSync);
    previousIndex = getDisplayIndex();
    valueBox->repaint();
}

int ChoiceField::getDisplayIndex() const noexcept { return juce::roundToInt(choiceSlider.getValue()); }

void ChoiceField::setDisplayItemEnabled(int index, bool enabled)
{
    if (juce::isPositiveAndBelow(index, int(itemEnabled.size()))) itemEnabled[static_cast<size_t>(index)] = enabled;
}

void ChoiceField::resetSelection()
{
    if (!interactionEnabled) return;
    if (onResetRequested != nullptr) { onResetRequested(); return; }
    setDisplayIndex(defaultDisplayIndex);
    if (selectionChanged != nullptr) selectionChanged(getDisplayIndex());
}

void ChoiceField::setInteractionEnabled(bool enabled)
{
    interactionEnabled = enabled;
    valueBox->setInteractionEnabled(enabled);
}

void ChoiceField::resized()
{
    valueBox->setBounds(getLocalBounds());
    choiceSlider.setBounds(valueBox->getBounds());
    layoutBottomAccessory(valueBox->getBounds());
}

void ChoiceField::setIdleLabelVisible(bool visible)
{
    if (visible) valueBox->setIdleTextProvider([this] { return titleText; });
    else valueBox->setIdleTextProvider({});
}

void ChoiceField::setTitleText(const juce::String& text)
{
    if (titleText == text) return;
    titleText = text;
    valueBox->repaint();
}

void ChoiceField::bindParameterIndicators(juce::AudioProcessorValueTreeState* state, const juce::String& parameterId)
{
    if (state != nullptr)
    {
        parameter_control_support::bindParameterIndicators(*valueBox, *this, [parameterId] { return parameterId; },
            [state, parameterId] { return state->getParameter(parameterId); }, *state);
        getProperties().set(juce::Identifier("oscParameterId"), parameterId);
    }
    else
    {
        valueBox->bottomIndicators = {};
        getProperties().remove(juce::Identifier("oscParameterId"));
    }
}

ChoiceControl::ChoiceControl(juce::AudioProcessorValueTreeState& state, const juce::String& id,
                             const juce::String& title)
    : ChoiceField(title), parameterId(id)
{
    selectionChanged = [this] (int index) { commitSelection(index); };
    choiceSlider.onDragStart = [this]
    {
        if (attachment != nullptr) { attachment->beginGesture(); gestureActive = true; }
    };
    choiceSlider.onDragEnd = [this]
    {
        if (gestureActive && attachment != nullptr) attachment->endGesture();
        gestureActive = false;
    };
    rebind(state);
    valueBox->setIdleTextProvider([this] { return titleText; });
}

ChoiceControl::~ChoiceControl() { detach(); }

void ChoiceControl::detach() noexcept
{
    valueBox->setInteractionEnabled(false);
    valueBox->bottomIndicators = {};
    if (gestureActive && attachment != nullptr) attachment->endGesture();
    gestureActive = false;
    attachment.reset();
    parameter = nullptr;
    boundState = nullptr;
}

void ChoiceControl::rebind(juce::AudioProcessorValueTreeState& state)
{
    detach();
    boundState = &state;
    parameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterId));
    parameter_control_support::bindParameterIndicators(*valueBox, *this,
        [this] { return parameterId; }, [this] { return parameter; }, state);
    jassert(parameter != nullptr);
    if (parameter == nullptr) return;
    choices = parameter->choices;
    setItems(choices);
    const auto defaultIndex = juce::roundToInt(parameter->convertFrom0to1(static_cast<juce::RangedAudioParameter&>(*parameter).getDefaultValue()));
    defaultDisplayIndex = defaultIndex;
    attachment = std::make_unique<Attachment>(*parameter, [this] (float value)
    {
        setDisplayIndex(juce::roundToInt(value));
    }, state.undoManager);
    attachment->sendInitialUpdate();
    valueBox->setInteractionEnabled(interactionEnabled);
}

int ChoiceControl::getSelectedChoiceIndex() const noexcept
{
    return getDisplayIndex();
}

void ChoiceControl::commitSelection(int displayIndex)
{
    if (attachment == nullptr || !juce::isPositiveAndBelow(displayIndex, choices.size())) return;
    const auto index = static_cast<float>(displayIndex);
    if (gestureActive) attachment->setValueAsPartOfGesture(index);
    else attachment->setValueAsCompleteGesture(index);
    if (onValueChanged != nullptr) onValueChanged();
}

void ChoiceControl::setSelectedChoiceIndex(int index, bool sendNotification)
{
    if (!juce::isPositiveAndBelow(index, choices.size())) return;
    setDisplayIndex(index);
    if (sendNotification) commitSelection(index);
}

void ChoiceControl::setChoices(const juce::StringArray& newChoices)
{
    if (newChoices == choices || newChoices.size() != choices.size()) return;
    const auto selected = getSelectedChoiceIndex();
    choices = newChoices;
    setItems(choices);
    setSelectedChoiceIndex(selected, false);
}

void ChoiceControl::setChoiceEnabled(int index, bool enabled)
{
    setDisplayItemEnabled(index, enabled);
}

void ChoiceControl::setOverrideText(const juce::String& text)
{
    overrideText = text.trim().isEmpty() ? juce::String("OFF") : text.trim();
    valueBox->repaint();
}

void ChoiceControl::clearOverrideText() { overrideText.clear(); valueBox->repaint(); }

LocalChoiceControl::LocalChoiceControl(const juce::String& title, const juce::StringArray& choices, int defaultIndex)
    : ChoiceField(title)
{
    setItems(choices);
    defaultDisplayIndex = defaultIndex;
    setDisplayIndex(defaultIndex);
    selectionChanged = [this] (int) { if (onValueChanged != nullptr) onValueChanged(); };
    valueBox->setIdleTextProvider([this] { return titleText; });
}

void LocalChoiceControl::setSelectedChoiceIndex(int index, bool sendNotification)
{
    setDisplayIndex(index);
    if (sendNotification && onValueChanged != nullptr) onValueChanged();
}

void LocalChoiceControl::setChoices(const juce::StringArray& choices)
{
    if (items == choices) return;
    const auto index = getDisplayIndex();
    setItems(choices);
    setDisplayIndex(index);
}

LocalContinuousControl::LocalContinuousControl(const juce::String& title)
    : titleText(title)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    parameter_control_support::configureHiddenSlider(slider);
    slider.setRange(0.0, 1.0, 0.0);
    slider.setValue(0.0, juce::dontSendNotification);
    valueBox = std::make_unique<ValueBoxComponent>(slider);
    // Preserve the existing macOS cursor-hide/deferred-reveal path on scroll drags.
    valueBox->setHideCursorOnDrag(true);
    valueBox->setIdleTextProvider([this] { return titleText; });
    valueBox->displayTextProvider = [this]
    {
        return juce::String::formatted("%02d",
            juce::jlimit(0, 99, juce::roundToInt(valueBox->getDisplayedValue() * 99.0)));
    };
    valueBox->editorTextProvider = [this]
    {
        return juce::String::formatted("%02d",
            juce::jlimit(0, 99, juce::roundToInt(slider.getValue() * 99.0)));
    };
    valueBox->textToValueParser = [](const juce::String& input)
    {
        return double(juce::jlimit(0, 99, input.getIntValue())) / 99.0;
    };
    valueBox->onResetRequested = [this] { setValue(0.0, true); };
    slider.onValueChange = [this]
    {
        if (valueBox != nullptr) valueBox->repaint();
        repaint();
        if (onValueChanged) onValueChanged();
    };
    setBottomIndicatorsVisible(false, false);
    // The fixed pinned strip is outside the scrolling content and must paint
    // its continuous indicator locally rather than behind an overlay.
    getProperties().set("paintRangeLocally", true);
    addChildComponent(slider);
    addAndMakeVisible(*valueBox);
}

LocalContinuousControl::~LocalContinuousControl() = default;

void LocalContinuousControl::setTitleText(const juce::String& text)
{
    if (titleText == text) return;
    titleText = text;
    if (valueBox != nullptr) valueBox->repaint();
}

void LocalContinuousControl::setValue(double normalized, bool sendNotification)
{
    slider.setValue(juce::jlimit(0.0, 1.0, normalized),
        sendNotification ? juce::sendNotificationSync : juce::dontSendNotification);
    if (valueBox != nullptr) valueBox->repaint();
}

void LocalContinuousControl::setInteractionEnabled(bool enabled)
{
    if (valueBox != nullptr) valueBox->setInteractionEnabled(enabled);
}

void LocalContinuousControl::resized()
{
    if (valueBox != nullptr)
    {
        valueBox->setBounds(getLocalBounds());
        slider.setBounds(valueBox->getBounds());
        layoutBottomAccessory(valueBox->getBounds());
    }
}

PushControl::PushControl(const juce::String& title, bool showStateIndicators) : ChoiceField(title)
{
    setItems({ "OFF", "ON" });
    if (!showStateIndicators) valueBox->setDiscreteChoiceIndicator(0);
    setDisplayIndex(0);
    valueBox->setMomentaryAction([this]
    {
        const auto trigger = onTrigger;
        if (trigger != nullptr) trigger();
    });
}
