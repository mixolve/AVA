#pragma once

#include "UiControls.h"

namespace shell_parameter_focus
{
juce::Slider* getFocusedValueSlider(juce::Component& owner) noexcept;
void clearFocus() noexcept;
void clearFocus(juce::Component& owner) noexcept;
void clearFocusIfNotShowing(juce::Component& owner) noexcept;
}

namespace parameter_control_support
{
inline const juce::Colour titleFocusColour { uiWhite };

void focusTitleButton(BoxTextButton* button, juce::Slider* valueSlider);
bool isTitleButtonFocused(const BoxTextButton* button, const juce::Slider* valueSlider) noexcept;
void clearFocusedTitleButton(BoxTextButton* button, juce::Slider* valueSlider);
bool canUseFocusedPotentiometer(juce::RangedAudioParameter* parameter,
                                const std::function<void()>& valueClickAction) noexcept;
bool assignTitleToHostSlot(juce::Component& source,
                           BoxTextButton* titleButton,
                           const juce::String& parameterId,
                           juce::RangedAudioParameter* parameter);
}
