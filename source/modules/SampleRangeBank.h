#pragma once

#include "../crossover/Splitter.h"

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

namespace ava::modules
{
template <typename DspCore>
class SampleRangeBank
{
public:
    static constexpr size_t numRanges = ava::crossover::Splitter::numRanges;

    using Parameters = typename DspCore::Parameters;
    using RangeParameters = std::array<Parameters, numRanges>;
    using RangeLatencies = std::array<int, numRanges>;

    SampleRangeBank()
    {
        ensureRangeCount(1);
    }

    size_t ensureRangeCount(const size_t requestedRangeCount)
    {
        const auto targetRangeCount = std::clamp(requestedRangeCount, size_t { 1 }, numRanges);

        while (createdRangeCount < targetRangeCount)
        {
            auto processor = std::make_unique<DspCore>();

            if (prepared)
                processor->prepare(preparedSampleRate, preparedBlockSize, preparedChannelCount);

            processor->setParameters(parameters[createdRangeCount]);
            rangeProcessors[createdRangeCount++] = std::move(processor);
        }

        updateRangeLatencies();
        return createdRangeCount;
    }

    size_t getCreatedRangeCount() const noexcept
    {
        return createdRangeCount;
    }

    void prepare(const double sampleRate, const int maxBlockSize, const int numChannels)
    {
        preparedSampleRate = sampleRate;
        preparedBlockSize = juce::jmax(1, maxBlockSize);
        preparedChannelCount = juce::jlimit(1, 2, numChannels);
        prepared = true;

        ensureRangeCount(1);

        for (size_t rangeIndex = 0; rangeIndex < createdRangeCount; ++rangeIndex)
        {
            rangeProcessors[rangeIndex]->prepare(preparedSampleRate,
                                                 preparedBlockSize,
                                                 preparedChannelCount);
            rangeProcessors[rangeIndex]->setParameters(parameters[rangeIndex]);
        }

        updateRangeLatencies();
        reset();
    }

    void releaseResources()
    {
        reset();
        prepared = false;
    }

    void reset()
    {
        for (size_t rangeIndex = 0; rangeIndex < createdRangeCount; ++rangeIndex)
            rangeProcessors[rangeIndex]->reset();
    }

    void setRangeParameters(const RangeParameters& newParameters)
    {
        parameters = newParameters;

        for (size_t rangeIndex = 0; rangeIndex < createdRangeCount; ++rangeIndex)
            rangeProcessors[rangeIndex]->setParameters(parameters[rangeIndex]);

        updateRangeLatencies();
    }

    void processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
    {
        if (rangeIndex >= createdRangeCount || rangeProcessors[rangeIndex] == nullptr)
            return;

        if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
            return;

        auto* leftChannel = buffer.getWritePointer(0);
        auto* rightChannel = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
        auto& processor = *rangeProcessors[rangeIndex];

        if (processor.isNeutral())
            return;

        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            const auto leftInput = static_cast<double>(leftChannel[sampleIndex]);
            const auto rightInput = rightChannel != nullptr ? static_cast<double>(rightChannel[sampleIndex]) : leftInput;
            const auto output = processor.processSample(leftInput, rightInput);
            leftChannel[sampleIndex] = static_cast<float>(output.left);

            if (rightChannel != nullptr)
                rightChannel[sampleIndex] = static_cast<float>(output.right);
        }
    }

    RangeLatencies getRangeLatencies() const noexcept
    {
        return rangeLatencies;
    }

private:
    void updateRangeLatencies() noexcept
    {
        rangeLatencies = {};

        for (size_t rangeIndex = 0; rangeIndex < createdRangeCount; ++rangeIndex)
            rangeLatencies[rangeIndex] = rangeProcessors[rangeIndex]->getLatencySamples();
    }

    RangeParameters parameters {};
    std::array<std::unique_ptr<DspCore>, numRanges> rangeProcessors;
    RangeLatencies rangeLatencies {};
    size_t createdRangeCount = 0;
    double preparedSampleRate = 44100.0;
    int preparedBlockSize = 1;
    int preparedChannelCount = 2;
    bool prepared = false;
};
} // namespace ava::modules
