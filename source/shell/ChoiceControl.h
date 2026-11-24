#pragma once

#include "ParameterEditing.h"

#include <functional>
#include <memory>
#include <vector>

class ChoiceField : public ParameterField
{
public:
    int getPreferredHeight() const noexcept { return getSquareControlSize(*this); }
    void setInteractionEnabled(bool enabled);
    bool isInteractionEnabled() const noexcept { return interactionEnabled; }
    void setSelectionHighlighted(bool selected) { valueBox->setSelectionHighlighted(selected); }
    void resized() override;
    void setIdleLabelVisible(bool visible);
    juce::String getTitleText() const override { return titleText; }
    const juce::StringArray& getChoiceItems() const noexcept { return items; }
    void bindParameterIndicators(juce::AudioProcessorValueTreeState* state, const juce::String& parameterId = {});
    void setTitleText(const juce::String& text);
    std::function<juce::String(int, const juce::String&)> displayFormatter;
    std::function<void()> onValueChanged;
    std::function<void()> onResetRequested;

protected:
    explicit ChoiceField(const juce::String& title);
    void setItems(const juce::StringArray& items);
    void setDisplayIndex(int index);
    int getDisplayIndex() const noexcept;
    void setDisplayItemEnabled(int index, bool enabled);
    void resetSelection();

    juce::String titleText;
    juce::Slider choiceSlider;
    std::unique_ptr<ValueBoxComponent> valueBox;
    juce::StringArray items;
    juce::String overrideText;
    std::vector<bool> itemEnabled;
    std::function<void(int)> selectionChanged;
    int defaultDisplayIndex = 0;
    bool interactionEnabled = true;

private:
    int previousIndex = 0;
    bool updatingSelection = false;
};

class ChoiceControl final : public ChoiceField
{
public:
    using Attachment = juce::ParameterAttachment;
    ChoiceControl(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId,
                  const juce::String& title);
    ~ChoiceControl() override;
    void detach() noexcept;
    void rebind(juce::AudioProcessorValueTreeState& state);
    int getSelectedChoiceIndex() const noexcept;
    const juce::String& getParameterId() const noexcept { return parameterId; }
    juce::AudioProcessorValueTreeState* getBoundState() const noexcept { return boundState; }
    void setSelectedChoiceIndex(int index, bool sendNotification);
    void setChoices(const juce::StringArray& choices);
    void setChoiceEnabled(int index, bool enabled);
    void setOverrideText(const juce::String& text);
    void clearOverrideText();

private:
    void commitSelection(int displayIndex);
    juce::String parameterId;
    juce::AudioParameterChoice* parameter = nullptr;
    std::unique_ptr<Attachment> attachment;
    juce::StringArray choices;
    juce::AudioProcessorValueTreeState* boundState = nullptr;
    bool gestureActive = false;
};

class LocalChoiceControl final : public ChoiceField
{
public:
    LocalChoiceControl(const juce::String& title, const juce::StringArray& choices, int defaultIndex);
    int getSelectedChoiceIndex() const noexcept { return getDisplayIndex(); }
    void setSelectedChoiceIndex(int index, bool sendNotification);
    void setChoiceEnabled(int index, bool enabled) { setDisplayItemEnabled(index, enabled); }
    void setChoices(const juce::StringArray& choices);
};

class PushControl final : public ChoiceField
{
public:
    explicit PushControl(const juce::String& title, bool showStateIndicators = true);
    void setRestingState(bool active) { valueBox->setMomentaryRestingState(active); }
    void setPressFeedbackEnabled(bool enabled) { valueBox->setMomentaryPressFeedbackEnabled(enabled); }
    void setTextTopInset(int pixels) { valueBox->getProperties().set("textTopInset", pixels); valueBox->repaint(); }
    void setDisplayText(const juce::String& text) { overrideText=text;valueBox->repaint(); }
    void setDragAction(std::function<void(const juce::MouseEvent&, bool)> action, std::function<void()> cancelled = {})
    { valueBox->onMomentaryDrag = std::move(action); valueBox->onMomentaryDragCancelled = std::move(cancelled); }
    void setDragTargetHighlighted(bool highlighted) { valueBox->setDragTargetHighlighted(highlighted); }
    std::function<void()> onTrigger;
};
