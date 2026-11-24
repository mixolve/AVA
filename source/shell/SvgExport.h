#pragma once
#include <JuceHeader.h>

namespace svg_export
{
juce::String render(juce::Component& component);
void chooseFile(juce::Component& component);
}
