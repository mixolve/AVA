#include "ChoiceControl.h"
#include "ParameterControlSupport.h"

#include <utility>

ChoiceControl::ChoiceControl(juce::AudioProcessorValueTreeState& state,
                             const juce::String& parameterIdIn,
                             const juce::String& titleText,
                             std::vector<int> displayOrderIn)
    : parameterId(parameterIdIn),
      parameter(dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterIdIn)))
{
    jassert(parameter != nullptr);

    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    titleButton = std::make_unique<BoxTextButton>(parameter_control_support::titleFocusColour);
    titleButton->setButtonText(titleText);
    titleButton->setTextJustification(juce::Justification::centredLeft);
    titleButton->onClick = [this]
    {
        clearKeyboardFocus(*this);
    };
    titleButton->setLongPressPromptActions([this]
    {
        if (onTitleClick)
        {
            onTitleClick();
        }
        else if (parameter != nullptr)
        {
            auto& rangedParameter = static_cast<juce::RangedAudioParameter&>(*parameter);
            setSelectedChoiceIndex(juce::roundToInt(rangedParameter.convertFrom0to1(rangedParameter.getDefaultValue())),
                                   true);
        }

        clearKeyboardFocus(*this);
    }, [this]
    {
        parameter_control_support::assignTitleToHostSlot(*this, titleButton.get(), parameterId, parameter);
        clearKeyboardFocus(*this);
    });

    comboBox.setEditableText(false);
    comboBox.setJustificationType(juce::Justification::centred);
    comboBox.setColour(juce::ComboBox::backgroundColourId, uiGrey800);
    comboBox.setColour(juce::ComboBox::outlineColourId, uiGrey500);
    comboBox.setColour(juce::ComboBox::textColourId, uiWhite);
    comboBox.setColour(juce::ComboBox::arrowColourId, uiWhite);
    comboBox.setColour(juce::ComboBox::buttonColourId, uiGrey800);
    comboBox.setWantsKeyboardFocus(false);
    comboBox.setMouseClickGrabsKeyboardFocus(false);
    comboBox.setName(titleText);
    comboBox.setPromptStylePopupEnabled(true);

    if (parameter != nullptr)
    {
        choices = parameter->choices;

        if (displayOrderIn.empty())
        {
            displayOrderIn.reserve(static_cast<size_t>(choices.size()));

            for (int choiceIndex = 0; choiceIndex < choices.size(); ++choiceIndex)
                displayOrderIn.push_back(choiceIndex);
        }

        displayOrder = std::move(displayOrderIn);
        choiceToDisplayIndex.resize(static_cast<size_t>(choices.size()), 0);

        for (int displayIndex = 0; displayIndex < static_cast<int>(displayOrder.size()); ++displayIndex)
        {
            const auto choiceIndex = displayOrder[static_cast<size_t>(displayIndex)];

            if (! juce::isPositiveAndBelow(choiceIndex, choices.size()))
                continue;

            comboBox.addItem(choices[choiceIndex], displayIndex + 1);
            choiceToDisplayIndex[static_cast<size_t>(choiceIndex)] = displayIndex;
        }
    }

    comboBox.onChange = [this]
    {
        if (ignoreCallbacks || parameter == nullptr)
            return;

        const auto displayIndex = comboBox.getSelectedItemIndex();

        if (! juce::isPositiveAndBelow(displayIndex, static_cast<int>(displayOrder.size())))
            return;

        attachment->setValueAsCompleteGesture(static_cast<float>(displayOrder[static_cast<size_t>(displayIndex)]));

        if (onValueChanged)
            onValueChanged();
    };

    if (parameter != nullptr)
    {
        attachment = std::make_unique<Attachment>(*parameter,
                                                  [this] (float newValue)
                                                  {
                                                      const auto choiceIndex = juce::roundToInt(newValue);

                                                      if (! juce::isPositiveAndBelow(choiceIndex, static_cast<int>(choiceToDisplayIndex.size())))
                                                          return;

                                                      const auto displayIndex = choiceToDisplayIndex[static_cast<size_t>(choiceIndex)];
                                                      const juce::ScopedValueSetter<bool> scopedIgnore(ignoreCallbacks, true);
                                                      comboBox.setSelectedItemIndex(displayIndex, juce::dontSendNotification);
                                                  },
                                                  state.undoManager);
        attachment->sendInitialUpdate();
    }

    addAndMakeVisible(*titleButton);
    addAndMakeVisible(comboBox);
}

int ChoiceControl::getPreferredHeight() const noexcept
{
    return rowHeight;
}

ChoiceControl::~ChoiceControl()
{
    parameter_control_support::clearFocusedTitleButton(titleButton.get(), nullptr);
}

void ChoiceControl::detach() noexcept
{
    attachment.reset();
    parameter = nullptr;
}

void ChoiceControl::rebind(juce::AudioProcessorValueTreeState& state)
{
    attachment.reset();
    parameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterId));
    jassert(parameter != nullptr);

    if (parameter == nullptr)
        return;

    if (displayOrder.empty())
    {
        displayOrder.reserve(static_cast<size_t>(parameter->choices.size()));

        for (int choiceIndex = 0; choiceIndex < parameter->choices.size(); ++choiceIndex)
            displayOrder.push_back(choiceIndex);
    }

    if (choices.size() != parameter->choices.size())
        choices = parameter->choices;

    choiceToDisplayIndex.assign(static_cast<size_t>(parameter->choices.size()), 0);

    const juce::ScopedValueSetter<bool> scopedIgnore(ignoreCallbacks, true);
    comboBox.clear(juce::dontSendNotification);

    for (int displayIndex = 0; displayIndex < static_cast<int>(displayOrder.size()); ++displayIndex)
    {
        const auto choiceIndex = displayOrder[static_cast<size_t>(displayIndex)];

        if (! juce::isPositiveAndBelow(choiceIndex, parameter->choices.size()))
            continue;

        comboBox.addItem(choices[choiceIndex], displayIndex + 1);
        choiceToDisplayIndex[static_cast<size_t>(choiceIndex)] = displayIndex;
    }

    attachment = std::make_unique<Attachment>(*parameter,
                                              [this] (float newValue)
                                              {
                                                  const auto choiceIndex = juce::roundToInt(newValue);

                                                  if (! juce::isPositiveAndBelow(choiceIndex, static_cast<int>(choiceToDisplayIndex.size())))
                                                      return;

                                                  const auto displayIndex = choiceToDisplayIndex[static_cast<size_t>(choiceIndex)];
                                                  const juce::ScopedValueSetter<bool> scopedAttachmentIgnore(ignoreCallbacks, true);
                                                  comboBox.setSelectedItemIndex(displayIndex, juce::dontSendNotification);
                                              },
                                              state.undoManager);
    attachment->sendInitialUpdate();
    repaint();
}

int ChoiceControl::getSelectedChoiceIndex() const noexcept
{
    const auto displayIndex = comboBox.getSelectedItemIndex();

    if (! juce::isPositiveAndBelow(displayIndex, static_cast<int>(displayOrder.size())))
        return 0;

    return displayOrder[static_cast<size_t>(displayIndex)];
}

void ChoiceControl::setSelectedChoiceIndex(const int choiceIndex, const bool sendNotification)
{
    if (! juce::isPositiveAndBelow(choiceIndex, static_cast<int>(choiceToDisplayIndex.size())))
        return;

    const auto displayIndex = choiceToDisplayIndex[static_cast<size_t>(choiceIndex)];
    const juce::ScopedValueSetter<bool> scopedIgnore(ignoreCallbacks, true);
    comboBox.setSelectedItemIndex(displayIndex,
                                  sendNotification ? juce::sendNotificationSync
                                                   : juce::dontSendNotification);

    if (sendNotification && attachment != nullptr)
        attachment->setValueAsCompleteGesture(static_cast<float>(choiceIndex));

    if (sendNotification && onValueChanged)
        onValueChanged();
}

void ChoiceControl::setChoices(const juce::StringArray& choicesIn)
{
    if (parameter == nullptr || choicesIn.size() != choices.size())
        return;

    const auto selectedChoiceIndex = getSelectedChoiceIndex();
    choices = choicesIn;

    const juce::ScopedValueSetter<bool> scopedIgnore(ignoreCallbacks, true);
    comboBox.clear(juce::dontSendNotification);

    for (int displayIndex = 0; displayIndex < static_cast<int>(displayOrder.size()); ++displayIndex)
    {
        const auto choiceIndex = displayOrder[static_cast<size_t>(displayIndex)];

        if (! juce::isPositiveAndBelow(choiceIndex, choices.size()))
            continue;

        comboBox.addItem(choices[choiceIndex], displayIndex + 1);
    }

    if (juce::isPositiveAndBelow(selectedChoiceIndex, static_cast<int>(choiceToDisplayIndex.size())))
        comboBox.setSelectedItemIndex(choiceToDisplayIndex[static_cast<size_t>(selectedChoiceIndex)], juce::dontSendNotification);
}

void ChoiceControl::setChoiceEnabled(const int choiceIndex, const bool shouldEnable)
{
    if (! juce::isPositiveAndBelow(choiceIndex, static_cast<int>(choiceToDisplayIndex.size())))
        return;

    const auto displayIndex = choiceToDisplayIndex[static_cast<size_t>(choiceIndex)];

    if (! juce::isPositiveAndBelow(displayIndex, static_cast<int>(displayOrder.size())))
        return;

    comboBox.setItemEnabled(displayIndex + 1, shouldEnable);
}

void ChoiceControl::setInteractionEnabled(const bool shouldEnable)
{
    interactionEnabled = shouldEnable;

    if (titleButton != nullptr)
    {
        titleButton->setEnabled(shouldEnable);
        titleButton->setPressFillEnabled(shouldEnable);
    }

    comboBox.setEnabled(true);
    comboBox.setAlpha(1.0f);
    comboBox.setInterceptsMouseClicks(shouldEnable, shouldEnable);
    const auto displayText = overrideText.isNotEmpty() ? overrideText : comboBox.getText();
    comboBox.setColour(juce::ComboBox::textColourId,
                       shouldEnable ? getDisplayTextColour(displayText) : uiGrey500);
}

void ChoiceControl::setOverrideText(const juce::String& text)
{
    overrideText = text.trim().isNotEmpty() ? text.trim() : juce::String("OFF");
    comboBox.setColour(juce::ComboBox::textColourId, getDisplayTextColour(overrideText));

    const juce::ScopedValueSetter<bool> scopedIgnore(ignoreCallbacks, true);
    comboBox.setText(overrideText, juce::dontSendNotification);
}

void ChoiceControl::clearOverrideText()
{
    if (overrideText.isEmpty())
        return;

    overrideText.clear();
    comboBox.setColour(juce::ComboBox::textColourId, uiWhite);

    if (parameter == nullptr)
        return;

    const auto choiceIndex = parameter->getIndex();

    if (! juce::isPositiveAndBelow(choiceIndex, static_cast<int>(choiceToDisplayIndex.size())))
        return;

    const auto displayIndex = choiceToDisplayIndex[static_cast<size_t>(choiceIndex)];
    const juce::ScopedValueSetter<bool> scopedIgnore(ignoreCallbacks, true);
    comboBox.setSelectedItemIndex(displayIndex, juce::dontSendNotification);
}

void ChoiceControl::resized()
{
    auto row = getLocalBounds();
    const auto titleWidth = getScaledParameterNameWidth(row.getWidth());
    titleButton->setBounds(row.removeFromLeft(titleWidth));
    row.removeFromLeft(parameterGap);
    comboBox.setBounds(row);
}

LocalChoiceControl::LocalChoiceControl(const juce::String& titleText,
                                       const juce::StringArray& choices,
                                       const int defaultChoiceIndexIn)
    : defaultChoiceIndex(defaultChoiceIndexIn)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);

    titleButton = std::make_unique<BoxTextButton>(parameter_control_support::titleFocusColour);
    titleButton->setButtonText(titleText);
    titleButton->setTextJustification(juce::Justification::centredLeft);
    titleButton->setClearsParameterFocusOnMouseDown(false);
    titleButton->onClick = [] {};
    titleButton->setLongPressPromptActions([this]
    {
        setSelectedChoiceIndex(defaultChoiceIndex, true);
    });

    comboBox.setEditableText(false);
    comboBox.setJustificationType(juce::Justification::centred);
    comboBox.setColour(juce::ComboBox::backgroundColourId, uiGrey800);
    comboBox.setColour(juce::ComboBox::outlineColourId, uiGrey500);
    comboBox.setColour(juce::ComboBox::textColourId, uiWhite);
    comboBox.setColour(juce::ComboBox::arrowColourId, uiWhite);
    comboBox.setColour(juce::ComboBox::buttonColourId, uiGrey800);
    comboBox.setWantsKeyboardFocus(false);
    comboBox.setMouseClickGrabsKeyboardFocus(false);
    comboBox.setName(titleText);
    comboBox.setPromptStylePopupEnabled(true);

    for (auto choiceIndex = 0; choiceIndex < choices.size(); ++choiceIndex)
        comboBox.addItem(choices[choiceIndex], choiceIndex + 1);

    comboBox.setSelectedItemIndex(juce::jlimit(0, juce::jmax(0, choices.size() - 1), defaultChoiceIndex),
                                  juce::dontSendNotification);
    comboBox.onChange = [this]
    {
        if (! ignoreCallbacks && onValueChanged)
            onValueChanged();
    };

    addAndMakeVisible(*titleButton);
    addAndMakeVisible(comboBox);
}

int LocalChoiceControl::getPreferredHeight() const noexcept
{
    return rowHeight;
}

int LocalChoiceControl::getSelectedChoiceIndex() const noexcept
{
    return juce::jmax(0, comboBox.getSelectedItemIndex());
}

void LocalChoiceControl::setSelectedChoiceIndex(const int choiceIndex, const bool sendNotification)
{
    const auto clampedChoiceIndex = juce::jlimit(0, juce::jmax(0, comboBox.getNumItems() - 1), choiceIndex);
    const juce::ScopedValueSetter<bool> scopedIgnore(ignoreCallbacks, true);
    comboBox.setSelectedItemIndex(clampedChoiceIndex, juce::dontSendNotification);

    if (sendNotification && onValueChanged)
        onValueChanged();
}

void LocalChoiceControl::setInteractionEnabled(const bool shouldEnable)
{
    titleButton->setEnabled(shouldEnable);
    comboBox.setEnabled(true);
    comboBox.setAlpha(1.0f);
    comboBox.setInterceptsMouseClicks(shouldEnable, shouldEnable);
    comboBox.setColour(juce::ComboBox::textColourId, shouldEnable ? uiWhite : uiGrey500);
    comboBox.setColour(juce::ComboBox::arrowColourId, shouldEnable ? uiWhite : uiGrey500);
}

void LocalChoiceControl::resized()
{
    auto row = getLocalBounds();
    const auto titleWidth = getScaledParameterNameWidth(row.getWidth());
    titleButton->setBounds(row.removeFromLeft(titleWidth));
    row.removeFromLeft(parameterGap);
    comboBox.setBounds(row);
}
