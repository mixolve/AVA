#pragma once

#include <JuceHeader.h>
#include "WindowSnap.h"

namespace window_geometry
{
bool canResizeNativeWindow(juce::Component& component);
juce::Rectangle<int> getBounds(juce::Component& component);
void setPosition(juce::Component& component, juce::Point<int> position);
std::vector<SnapTarget> getSnapTargets(juce::Component& component);
void setWindowDragActive(juce::Component& component, bool active);
void hideWindowDragCursor();
}
