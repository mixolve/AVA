#pragma once
#include <JuceHeader.h>

namespace svg_export
{
// Export the current visible bounds without changing layout or scrolling.
juce::String render(juce::Component& component);
void chooseFile(juce::Component& component);
}
