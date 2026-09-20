#include "Processor.h"

int AvaAudioProcessor::getABCompareActiveSlot() const noexcept
{
    return juce::jlimit(0, 1, abCompareActiveSlot.load(std::memory_order_acquire));
}

void AvaAudioProcessor::setABCompareActiveSlot(const int slot) noexcept
{
    abCompareActiveSlot.store(juce::jlimit(0, 1, slot), std::memory_order_release);
}

bool AvaAudioProcessor::isABCompareSnapshotValid(const int slot) const noexcept
{
    if (! juce::isPositiveAndBelow(slot, static_cast<int>(abCompareSnapshots.size())))
        return false;

    const juce::ScopedLock lock(abCompareLock);
    return ! abCompareSnapshots[static_cast<size_t>(slot)].isEmpty();
}

juce::MemoryBlock AvaAudioProcessor::getABCompareSnapshot(const int slot) const
{
    if (! juce::isPositiveAndBelow(slot, static_cast<int>(abCompareSnapshots.size())))
        return {};

    const juce::ScopedLock lock(abCompareLock);
    return abCompareSnapshots[static_cast<size_t>(slot)];
}

void AvaAudioProcessor::setABCompareSnapshot(const int slot, const juce::MemoryBlock& snapshot)
{
    if (! juce::isPositiveAndBelow(slot, static_cast<int>(abCompareSnapshots.size())))
        return;

    const juce::ScopedLock lock(abCompareLock);
    abCompareSnapshots[static_cast<size_t>(slot)] = snapshot;
}
