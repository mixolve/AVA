#pragma once

#include <JuceHeader.h>

namespace ava::unit_sync
{
juce::ValueTree decode(const juce::String& encoded);
juce::String encode(const juce::ValueTree& state);
bool merge(const juce::ValueTree& before, const juce::ValueTree& after, juce::ValueTree& target);
}
