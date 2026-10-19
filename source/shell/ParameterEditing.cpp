#include "ParameterEditing.h"
#include "Editor.h"

void parameter_control_support::configureHiddenSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider.setInterceptsMouseClicks(false, false);
    slider.setScrollWheelEnabled(false);
    slider.setAlpha(0.0f);
    slider.setWantsKeyboardFocus(false);
    slider.setMouseClickGrabsKeyboardFocus(false);
}

bool parameter_control_support::assignTitleToHostSlot(juce::Component& source,
                                                      BoxTextButton* titleButton,
                                                      const juce::String& parameterId,
                                                      juce::RangedAudioParameter* parameter)
{
    if (parameter == nullptr)
        return false;

    if (auto* owner = source.findParentComponentOfClass<AvaAudioProcessorEditor>())
    {
        return owner->handleHostSlotAssignRequest(parameterId,
                                                  titleButton != nullptr ? titleButton->getButtonText() : parameterId,
                                                  parameter->getValue());
    }

    return false;
}
