#pragma once

#include "Controls.h"

#include <functional>
#include <memory>
#include <vector>

class ChoiceControl final : public juce::Component
{
public:
    using Attachment = juce::ParameterAttachment;

    ChoiceControl(juce::AudioProcessorValueTreeState& state,
                  const juce::String& parameterIdIn,
                  const juce::String& titleText,
                  std::vector<int> displayOrderIn = {});
    ~ChoiceControl() override;

    int getPreferredHeight() const noexcept;
    void detach() noexcept;
    void rebind(juce::AudioProcessorValueTreeState& state);
    int getSelectedChoiceIndex() const noexcept;
    void setSelectedChoiceIndex(int choiceIndex, bool sendNotification);
    void setChoices(const juce::StringArray& choicesIn);
    void setChoiceEnabled(int choiceIndex, bool shouldEnable);
    void setInteractionEnabled(bool shouldEnable);
    void setOverrideText(const juce::String&);
    void clearOverrideText();
    void resized() override;

    std::function<void()> onValueChanged;
    std::function<void()> onTitleClick;

private:
    juce::String parameterId;
    juce::AudioParameterChoice* parameter = nullptr;
    std::unique_ptr<BoxTextButton> titleButton;
    NoTickComboBox comboBox;
    std::unique_ptr<Attachment> attachment;
    juce::StringArray choices;
    std::vector<int> displayOrder;
    std::vector<int> choiceToDisplayIndex;
    bool ignoreCallbacks = false;
    juce::String overrideText;
    bool interactionEnabled = true;
};

class LocalChoiceControl final : public juce::Component
{
public:
    LocalChoiceControl(const juce::String& titleText,
                       const juce::StringArray& choices,
                       int defaultChoiceIndexIn);

    int getPreferredHeight() const noexcept;
    int getSelectedChoiceIndex() const noexcept;
    void setSelectedChoiceIndex(int choiceIndex, bool sendNotification);
    void setInteractionEnabled(bool shouldEnable);
    void resized() override;

    std::function<void()> onValueChanged;

private:
    const int defaultChoiceIndex = 0;
    std::unique_ptr<BoxTextButton> titleButton;
    NoTickComboBox comboBox;
    bool ignoreCallbacks = false;
};
