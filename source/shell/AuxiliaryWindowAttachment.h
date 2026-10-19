#pragma once

#include <JuceHeader.h>

namespace auxiliary_window
{
void attachToOwner(juce::Component& window, juce::Component& owner);
void detachFromOwner(juce::Component& window);
}
