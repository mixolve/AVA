#pragma once

#include "Settings.h"
#include "Splitter.h"

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace ava::crossover
{
class BufferRouter
{
public:
    static constexpr size_t numRanges = Splitter::numRanges;
    static constexpr size_t numSplits = Splitter::numSplits;
    using SplitFrequencies = Splitter::SplitFrequencies;
    using SoloMask = std::array<bool, numRanges>;
    using RangeLatencies = std::array<int, numRanges>;

    void prepare(const double sampleRate,
                 const int maxBlockSize,
                 const int channelCount,
                 const int maximumLatencySamples)
    {
        preparedChannels = juce::jlimit(1, 2, channelCount);
        preparedBlockSize = juce::jmax(1, maxBlockSize);
        alignmentBufferSize = juce::jmax(1, maximumLatencySamples + 1);
        splitter.prepare(sampleRate);

        for (auto& rangeBuffer : rangeBuffers)
            rangeBuffer.setSize(preparedChannels, preparedBlockSize, false, false, true);

        for (auto& rangeChannels : alignmentBuffers)
            for (auto& channel : rangeChannels)
                channel.assign(static_cast<size_t>(alignmentBufferSize), 0.0f);

        splitter.setActiveSplitCount(activeSplitCount);
        splitter.setSplitFrequencies(splitFrequencies);
        reset();
    }

    void reset()
    {
        splitter.reset();

        for (auto& rangeBuffer : rangeBuffers)
            rangeBuffer.clear();

        clearAlignmentBuffers();
    }

    void setSettings(const Settings& settings)
    {
        const auto newActiveSplitCount = std::min(settings.activeSplitCount, numSplits);
        const auto splitCountChanged = activeSplitCount != newActiveSplitCount;
        const auto frequenciesChanged = splitFrequencies != settings.splitFrequencies;

        if (splitCountChanged)
        {
            activeSplitCount = newActiveSplitCount;
            splitter.setActiveSplitCount(activeSplitCount);
        }

        if (frequenciesChanged)
        {
            splitFrequencies = settings.splitFrequencies;
            splitter.setSplitFrequencies(splitFrequencies);
        }

        soloMask = settings.soloMask;
        const auto activeRangeCount = activeSplitCount + 1;
        anySoloActive = std::any_of(soloMask.begin(),
                                    soloMask.begin() + static_cast<std::ptrdiff_t>(activeRangeCount),
                                    [] (const bool soloed) { return soloed; });

        if (splitCountChanged || frequenciesChanged)
        {
            for (auto& rangeBuffer : rangeBuffers)
                rangeBuffer.clear();

            updateLatencyCompensation();
            clearAlignmentBuffers();
        }
    }

    void setRangeLatencies(const RangeLatencies& newRangeLatencies)
    {
        RangeLatencies constrainedLatencies {};

        for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        {
            jassert(newRangeLatencies[rangeIndex] < alignmentBufferSize);
            constrainedLatencies[rangeIndex] = juce::jlimit(0,
                                                            alignmentBufferSize - 1,
                                                            newRangeLatencies[rangeIndex]);
        }

        if (rangeLatencies == constrainedLatencies)
            return;

        rangeLatencies = constrainedLatencies;
        updateLatencyCompensation();
        clearAlignmentBuffers();
    }

    int getLatencySamples() const noexcept
    {
        return targetLatencySamples;
    }

    template <typename ProcessRange>
    void process(juce::AudioBuffer<float>& buffer, ProcessRange&& processRange)
    {
        const auto channelCount = juce::jmin(preparedChannels, buffer.getNumChannels());
        const auto totalSampleCount = buffer.getNumSamples();

        if (channelCount <= 0 || totalSampleCount <= 0)
            return;

        const auto activeRangeCount = activeSplitCount + 1;

        for (auto blockStart = 0; blockStart < totalSampleCount; blockStart += preparedBlockSize)
        {
            const auto sampleCount = juce::jmin(preparedBlockSize, totalSampleCount - blockStart);

            for (size_t rangeIndex = 0; rangeIndex < activeRangeCount; ++rangeIndex)
                for (auto channel = 0; channel < channelCount; ++channel)
                    rangeBuffers[rangeIndex].clear(channel, 0, sampleCount);

            const auto* inputLeft = buffer.getReadPointer(0, blockStart);
            const auto* inputRight = channelCount > 1 ? buffer.getReadPointer(1, blockStart) : nullptr;

            for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                const auto left = static_cast<double>(inputLeft[sampleIndex]);
                const auto right = inputRight != nullptr ? static_cast<double>(inputRight[sampleIndex]) : left;
                const auto ranges = splitter.processSample(left, right);

                for (size_t rangeIndex = 0; rangeIndex < activeRangeCount; ++rangeIndex)
                {
                    rangeBuffers[rangeIndex].setSample(0, sampleIndex, static_cast<float>(ranges[rangeIndex].left));

                    if (channelCount > 1)
                        rangeBuffers[rangeIndex].setSample(1, sampleIndex, static_cast<float>(ranges[rangeIndex].right));
                }
            }

            for (size_t rangeIndex = 0; rangeIndex < activeRangeCount; ++rangeIndex)
            {
                juce::AudioBuffer<float> rangeView(rangeBuffers[rangeIndex].getArrayOfWritePointers(),
                                                   channelCount,
                                                   sampleCount);
                processRange(rangeIndex, rangeView);
            }

            for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
            {
                auto sumLeft = 0.0f;
                auto sumRight = 0.0f;

                for (size_t rangeIndex = 0; rangeIndex < activeRangeCount; ++rangeIndex)
                {
                    const auto compensationSamples = targetLatencySamples - rangeLatencies[rangeIndex];
                    auto readPosition = alignmentWritePosition - compensationSamples;

                    if (readPosition < 0)
                        readPosition += alignmentBufferSize;

                    auto& leftAlignment = alignmentBuffers[rangeIndex][0];
                    leftAlignment[static_cast<size_t>(alignmentWritePosition)] = rangeBuffers[rangeIndex].getSample(0, sampleIndex);

                    if (! anySoloActive || soloMask[rangeIndex])
                        sumLeft += leftAlignment[static_cast<size_t>(readPosition)];

                    if (channelCount > 1)
                    {
                        auto& rightAlignment = alignmentBuffers[rangeIndex][1];
                        rightAlignment[static_cast<size_t>(alignmentWritePosition)] = rangeBuffers[rangeIndex].getSample(1, sampleIndex);

                        if (! anySoloActive || soloMask[rangeIndex])
                            sumRight += rightAlignment[static_cast<size_t>(readPosition)];
                    }
                }

                buffer.setSample(0, blockStart + sampleIndex, sumLeft);

                if (channelCount > 1)
                    buffer.setSample(1, blockStart + sampleIndex, sumRight);

                if (++alignmentWritePosition == alignmentBufferSize)
                    alignmentWritePosition = 0;
            }
        }
    }

private:
    void clearAlignmentBuffers()
    {
        for (auto& rangeChannels : alignmentBuffers)
            for (auto& channel : rangeChannels)
                std::fill(channel.begin(), channel.end(), 0.0f);

        alignmentWritePosition = 0;
    }

    void updateLatencyCompensation()
    {
        const auto activeRangeCount = activeSplitCount + 1;
        targetLatencySamples = *std::max_element(rangeLatencies.begin(),
                                                 rangeLatencies.begin() + static_cast<std::ptrdiff_t>(activeRangeCount));
    }

    Splitter splitter;
    SplitFrequencies splitFrequencies { 134.0, 523.0, 2093.0, 5000.0, 10000.0 };
    SoloMask soloMask {};
    std::array<juce::AudioBuffer<float>, numRanges> rangeBuffers;
    std::array<std::array<std::vector<float>, 2>, numRanges> alignmentBuffers;
    RangeLatencies rangeLatencies {};
    size_t activeSplitCount = 0;
    int preparedChannels = 2;
    int preparedBlockSize = 1;
    int alignmentBufferSize = 1;
    int alignmentWritePosition = 0;
    int targetLatencySamples = 0;
    bool anySoloActive = false;
};
}
