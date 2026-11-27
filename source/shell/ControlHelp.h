#pragma once

#include <JuceHeader.h>

namespace control_help
{
juce::String describe(juce::Component& component, juce::Point<int> screen);
bool paint(juce::Graphics& graphics, const juce::String& text, juce::Rectangle<int> bounds, double startedAtMs);
}
