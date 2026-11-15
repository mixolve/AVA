#pragma once

#include "Processor.h"
#include "../shared/BlockRangeBank.h"

#include <cstddef>

class EqlProcessorBank final : private juce::Timer
{
public:
    static constexpr size_t numRanges = ava::modules::ProcessorRangeBank<EqlModuleProcessor>::numRanges;

    EqlProcessorBank();
    ~EqlProcessorBank() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void syncParameters(const std::array<int, numRanges>& limits, bool acceptModel = true) noexcept;
    void processRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer);
    ava::modules::ProcessorRangeBank<EqlModuleProcessor>::RangeLatencies getRangeLatencies() const noexcept;
    void loadInitialFilterPreset();
    size_t ensureRangeCount(size_t rangeCount);
    size_t getCreatedRangeCount() const noexcept;
    void getStateInformation(juce::MemoryBlock& destData);
    bool setStateInformation(const void* data, int sizeInBytes);

    void setSelectedRange(size_t rangeIndex) noexcept;
    EqlModuleProcessor* getProcessor(size_t rangeIndex) noexcept;
    const EqlModuleProcessor* getProcessor(size_t rangeIndex) const noexcept;
    EqlModuleProcessor* getSelectedProcessor() noexcept;
    const EqlModuleProcessor* getSelectedProcessor() const noexcept;

private:
    void timerCallback() override;
    ava::modules::ProcessorRangeBank<EqlModuleProcessor> ranges;
    bool initialFilterPresetLoaded = false;
    juce::String presetCatalogStamp;
};
