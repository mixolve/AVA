#pragma once

#include <JuceHeader.h>

namespace window_geometry
{
bool canMoveNativeWindow(juce::Component& component);
juce::Rectangle<int> getBounds(juce::Component& component);
void setPosition(juce::Component& component, juce::Point<int> position);
}
