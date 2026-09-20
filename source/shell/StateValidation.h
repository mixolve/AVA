#pragma once

#include <JuceHeader.h>

class AvaAudioProcessor;

namespace shell_state_validation
{
bool hasCurrentState(const juce::ValueTree& state,
                     AvaAudioProcessor& processor,
                     bool includeABCompareState);
}
