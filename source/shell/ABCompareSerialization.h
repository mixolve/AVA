#pragma once

#include "Processor.h"

namespace shell_state_serialization
{
struct RestoredABCompareState
{
    explicit RestoredABCompareState(const juce::ValueTree& state);

    juce::String snapshotA;
    juce::String snapshotB;
    int activeSlot = 0;
};

void removeABCompareStateProperties(juce::ValueTree& state);
void restoreABCompareState(AvaAudioProcessor& processor, const RestoredABCompareState& restoredState);
void storeABCompareState(juce::ValueTree& state, const AvaAudioProcessor& processor);
}
