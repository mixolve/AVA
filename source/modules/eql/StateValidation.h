#pragma once

#include <JuceHeader.h>

namespace eql_state
{
bool isCurrentState(const juce::ValueTree& state,
                    juce::AudioProcessorValueTreeState& parameters);
}
