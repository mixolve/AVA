#include "Processor.h"

int AvaAudioProcessor::getABCompareActiveSlot() const noexcept
{
    return juce::jlimit(0, 1, abCompareActiveSlot.load(std::memory_order_acquire));
}

void AvaAudioProcessor::setABCompareActiveSlot(const int slot, const bool notifyHost) noexcept
{
    const auto selected = juce::jlimit(0, 1, slot);
    if (abCompareActiveSlot.exchange(selected, std::memory_order_acq_rel) == selected)
        return;

    if (notifyHost)
        notifyHostOfStateChange();
    else
    {
        // Initialising a missing A/B slot when the GUI opens is not a host
        // edit, but the next project save must include this metadata.
        const juce::ScopedLock lock(processingLock);
        cachedHostAudioRevision = ~uint64_t(0);
    }
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

void AvaAudioProcessor::setABCompareSnapshot(const int slot, const juce::MemoryBlock& snapshot,
                                             const bool notifyHost)
{
    if (! juce::isPositiveAndBelow(slot, static_cast<int>(abCompareSnapshots.size())))
        return;

    {
        const juce::ScopedLock lock(abCompareLock);
        auto& stored = abCompareSnapshots[static_cast<size_t>(slot)];
        if (stored == snapshot) return;
        stored = snapshot;
    }

    if (notifyHost)
        notifyHostOfStateChange();
    else
    {
        // A default snapshot is UI initialisation, not a DAW Undo action.
        // Invalidate the old serialized state without notifying the host.
        const juce::ScopedLock lock(processingLock);
        cachedHostAudioRevision = ~uint64_t(0);
    }
}
