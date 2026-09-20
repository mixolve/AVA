#pragma once

#include "Processor.h"
#include "../shared/ProcessorRangeBank.h"

#include <cstddef>

class EqlProcessorBank final
{
public:
    static constexpr size_t numRanges = ava::modules::ProcessorRangeBank<EqlModuleProcessor>::numRanges;

    EqlProcessorBank();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void processRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer);
    int getLatencySamples() const noexcept;
    ava::modules::ProcessorRangeBank<EqlModuleProcessor>::RangeLatencies getRangeLatencies() const noexcept;
    void loadInitialFilterPreset();
    size_t ensureRangeCount(size_t rangeCount);
    size_t getCreatedRangeCount() const noexcept;
    int getRangeActiveFilterCount(size_t rangeIndex) const noexcept;
    void getStateInformation(juce::MemoryBlock& destData);
    bool setStateInformation(const void* data, int sizeInBytes);

    void setSelectedRange(size_t rangeIndex) noexcept;
    EqlModuleProcessor* getSelectedProcessor() noexcept;
    const EqlModuleProcessor* getSelectedProcessor() const noexcept;

private:
    ava::modules::ProcessorRangeBank<EqlModuleProcessor> ranges;
    bool initialFilterPresetLoaded = false;
};
