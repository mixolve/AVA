#include "ABCompareSerialization.h"

namespace shell_state_serialization
{
RestoredABCompareState::RestoredABCompareState(const juce::ValueTree& state)
    : snapshotA(state.getProperty(AvaAudioProcessor::abCompareSnapshotAStateKey).toString()),
      snapshotB(state.getProperty(AvaAudioProcessor::abCompareSnapshotBStateKey).toString()),
      activeSlot(juce::jlimit(0,
                              1,
                              static_cast<int>(state.getProperty(AvaAudioProcessor::abCompareActiveSlotStateKey,
                                                                 0))))
{
}

void removeABCompareStateProperties(juce::ValueTree& state)
{
    state.removeProperty(AvaAudioProcessor::abCompareSnapshotAStateKey, nullptr);
    state.removeProperty(AvaAudioProcessor::abCompareSnapshotBStateKey, nullptr);
    state.removeProperty(AvaAudioProcessor::abCompareActiveSlotStateKey, nullptr);
}

void restoreABCompareState(AvaAudioProcessor& processor, const RestoredABCompareState& restoredState)
{
    const auto restoreSnapshot = [&processor] (const int slot, const juce::String& encodedState)
    {
        juce::MemoryBlock snapshot;

        if (encodedState.isNotEmpty())
            snapshot.fromBase64Encoding(encodedState);

        processor.setABCompareSnapshot(slot, snapshot);
    };

    restoreSnapshot(0, restoredState.snapshotA);
    restoreSnapshot(1, restoredState.snapshotB);
    processor.setABCompareActiveSlot(restoredState.activeSlot);
}

void storeABCompareState(juce::ValueTree& state, const AvaAudioProcessor& processor)
{
    const auto storeSnapshot = [&state, &processor] (const int slot, const juce::Identifier& property)
    {
        const auto snapshot = processor.getABCompareSnapshot(slot);

        if (snapshot.isEmpty())
            state.removeProperty(property, nullptr);
        else
            state.setProperty(property, snapshot.toBase64Encoding(), nullptr);
    };

    storeSnapshot(0, AvaAudioProcessor::abCompareSnapshotAStateKey);
    storeSnapshot(1, AvaAudioProcessor::abCompareSnapshotBStateKey);
    state.setProperty(AvaAudioProcessor::abCompareActiveSlotStateKey,
                      processor.getABCompareActiveSlot(),
                      nullptr);
}
}
