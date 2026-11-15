#pragma once

#include "ParameterEditing.h"

#include <functional>
#include <memory>

class ParameterControl final : public ParameterField
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void parameterControlChanged(ParameterControl&) = 0;
    };
    void addListener(Listener* listener) { listeners.add(listener); }
    void removeListener(Listener* listener) { listeners.remove(listener); }
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    ParameterControl(juce::AudioProcessorValueTreeState& state,
                     const juce::String& parameterIdIn,
                     const juce::String& titleText,
                     int editorDecimalsIn);

    juce::String getTitleText() const override { return titleText; }
    int getPreferredHeight() const noexcept;
    double getValue() const noexcept;
    void detach() noexcept;
    void rebind(juce::AudioProcessorValueTreeState& state);
    void rebind(juce::AudioProcessorValueTreeState& state, const juce::String& parameterIdIn);
    const juce::String& getParameterId() const noexcept { return parameterId; }
    juce::AudioProcessorValueTreeState* getBoundState() const noexcept { return boundState; }
    int getEditorDecimals() const noexcept { return editorDecimals; }
    const auto& getValuePresentation() const noexcept { return valuePresentation; }
    void setValue(double value, bool sendNotification);
    void setOverrideText(const juce::String& text);
    void clearOverrideText();
    void setInteractionEnabled(bool shouldEnable);
    bool isInteractionEnabled() const noexcept { return interactionEnabled; }
    void setDiscreteChoiceIndicator(int count, int selected = -1);
    void setValueRange(double minimum, double maximum, double interval);
    void setTitleText(const juce::String& text);
    void setValuePresentation(ava::modules::parameters::NumericPresentation presentation);
    void resized() override;
    void setSelectionHighlighted(bool selected) { valueBox->setSelectionHighlighted(selected); }

    std::function<void()> onValueChanged;
    std::function<void()> onResetRequested;

private:
    void updateSliderMapping();
    juce::String formatDisplayValue(double value) const;
    juce::String formatNumericValue(double value) const;
    juce::String formatEditorValue() const;
    double parseText(const juce::String& text) const;

    juce::String titleText;
    juce::String parameterId;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::AudioProcessorValueTreeState* boundState = nullptr;
    const int editorDecimals = 2;
    juce::Slider slider;
    std::unique_ptr<Attachment> attachment;
    std::unique_ptr<ValueBoxComponent> valueBox;
    juce::String overrideText;
    ava::modules::parameters::NumericPresentation valuePresentation;
    bool interactionEnabled = true;
    bool customSliderMapping = false;
    juce::NormalisableRange<double> ordinarySliderRange;
    juce::ListenerList<Listener> listeners;
};

// A compact, independently clickable text button for the bottom row.
class AccessoryButton final : public juce::Button, private ParameterControl::Listener
{
public:
    explicit AccessoryButton(const juce::String& label);
    bool isActive() const;
    bool isAvailable() const { return isEnabled() && parameter!=nullptr; }
    // Embedded auxiliary buttons have an independent parameter identity in the dock.
    juce::String getParameterId() const { return parameter != nullptr ? parameter->getParameterID() : juce::String(); }
    juce::AudioProcessorValueTreeState* getBoundState() const noexcept { return boundState; }
    ~AccessoryButton() override;
    void bindParameter(juce::AudioProcessorValueTreeState& state, const juce::String& id, bool cycleChoices = false);
    void detach() noexcept;
    void bindParameter(juce::RangedAudioParameter& target, bool cycleChoices = false, juce::UndoManager* undo = nullptr);
    std::function<void()> onValueChanged;
    void bindLowerMuteThreshold(ParameterControl& gain, double threshold);
    void placeIn(juce::Rectangle<int> valueBounds);
    void paintButton(juce::Graphics&, bool hovered, bool pressed) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
private:
    ControlSelectionGesture selectionGesture;
    bool marqueeRepaintPending=false;
    bool reverseCycleOnClick = false;
    juce::RangedAudioParameter* parameter = nullptr;
    juce::AudioProcessorValueTreeState* boundState = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::Component::SafePointer<ParameterControl> thresholdSource;
    double muteThreshold = 0.0;
    void parameterControlChanged(ParameterControl&) override { repaintValueOverlay(*this); }
};
