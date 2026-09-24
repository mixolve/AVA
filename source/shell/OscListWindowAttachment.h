#pragma once

#include <JuceHeader.h>

namespace osc_list_window
{
void attachToOwner(juce::Component& listWindow, juce::Component& owner);
void detachFromOwner(juce::Component& listWindow);
}
