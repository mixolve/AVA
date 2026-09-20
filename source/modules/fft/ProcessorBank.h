#pragma once

#include "Processor.h"
#include "../shared/ProcessorRangeBank.h"

#include <cstddef>

class FftProcessorBank final
{
public:
    static constexpr size_t numRanges = ava::modules::ProcessorRangeBank<FftModuleProcessor>::numRanges;

    explicit FftProcessorBank(juce::AudioProcessor& owner);

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void processRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer);
    int getLatencySamples() const noexcept;
    ava::modules::ProcessorRangeBank<FftModuleProcessor>::RangeLatencies getRangeLatencies() const noexcept;
    bool refreshLatencyState() noexcept;
    size_t ensureRangeCount(size_t rangeCount);
    size_t getCreatedRangeCount() const noexcept;
    juce::String getStateXmlString() const;
    bool setStateFromXmlString(const juce::String& stateXmlString);

    void setSelectedRange(size_t rangeIndex) noexcept;
    FftModuleProcessor* getSelectedProcessor() noexcept;
    const FftModuleProcessor* getSelectedProcessor() const noexcept;

private:
    ava::modules::ProcessorRangeBank<FftModuleProcessor> ranges;
};
