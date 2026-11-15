#pragma once

#include <JuceHeader.h>
#include <optional>
#include <vector>

namespace eql_state
{
bool isCurrentState(const juce::ValueTree& state,
                    juce::AudioProcessorValueTreeState& parameters);
std::optional<std::vector<juce::ValueTree>> readBankState(
    const juce::ValueTree& state, juce::AudioProcessorValueTreeState& parameters);
}
