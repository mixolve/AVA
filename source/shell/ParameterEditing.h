#pragma once

#include "Controls.h"

namespace parameter_control_support
{
void configureHiddenSlider(juce::Slider& slider);
inline void applySliderValue(juce::Component& owner,
                             juce::Slider& slider,
                             ValueBoxComponent* valueBox,
                             const double value,
                             const bool sendNotification)
{
    slider.setValue(value, sendNotification ? juce::sendNotificationSync
                                            : juce::dontSendNotification);

    if (valueBox != nullptr)
        valueBox->repaint();

    owner.repaint();

    if (auto* parent = owner.getParentComponent())
        parent->repaint();
}
inline juce::Rectangle<int> getValueBoxBounds(const std::unique_ptr<ValueBoxComponent>& valueBox) noexcept
{
    return valueBox != nullptr ? valueBox->getBounds() : juce::Rectangle<int>();
}
bool assignTitleToHostSlot(juce::Component& source,
                           BoxTextButton* titleButton,
                           const juce::String& parameterId,
                           juce::RangedAudioParameter* parameter);
}
