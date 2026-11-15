#pragma once

#include "../../crossover/Splitter.h"

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

namespace ava::modules
{
template <typename ModuleProcessor>
class ProcessorRangeBank
{
public:
    static constexpr size_t numRanges = ava::crossover::Splitter::numRanges;
    using RangeLatencies = std::array<int, numRanges>;

    template <typename Factory>
    explicit ProcessorRangeBank(Factory&& factory)
        : processorFactory(std::forward<Factory>(factory))
    {
        ensureRangeCount(1);
    }

    size_t ensureRangeCount(const size_t requestedRangeCount)
    {
        const auto targetRangeCount = std::clamp(requestedRangeCount, size_t { 1 }, numRanges);

        while (createdRangeCount < targetRangeCount)
        {
            auto processor = processorFactory();

            if (processor == nullptr)
                break;

            if (prepared)
                processor->prepareToPlay(preparedSampleRate, preparedSamplesPerBlock);

            const auto index = createdRangeCount.load(std::memory_order_relaxed);
            processors[index] = std::move(processor);
            createdRangeCount.store(index + 1, std::memory_order_release);
        }

        return createdRangeCount;
    }

    size_t getCreatedRangeCount() const noexcept
    {
        return createdRangeCount;
    }

    size_t setRangeCount(const size_t requestedRangeCount)
    {
        const auto targetRangeCount = std::clamp(requestedRangeCount, size_t { 1 }, numRanges);

        while (createdRangeCount > targetRangeCount)
            processors[--createdRangeCount].reset();

        const auto rangeCount = ensureRangeCount(targetRangeCount);
        setSelectedRange(selectedRange);
        return rangeCount;
    }

    void prepareToPlay(const double sampleRate, const int samplesPerBlock)
    {
        preparedSampleRate = sampleRate;
        preparedSamplesPerBlock = samplesPerBlock;
        prepared = true;

        for (auto& processor : processors)
            if (processor != nullptr)
                processor->prepareToPlay(sampleRate, samplesPerBlock);
    }

    void releaseResources()
    {
        for (auto& processor : processors)
            if (processor != nullptr)
                processor->releaseResources();

        prepared = false;
    }

    void resetProcessingState() noexcept
    {
        for (auto& processor : processors)
            if (processor != nullptr)
                processor->resetProcessingState();
    }

    void processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
    {
        if (rangeIndex < createdRangeCount && processors[rangeIndex] != nullptr)
            processors[rangeIndex]->processBlock(buffer);
    }

    RangeLatencies getRangeLatencies() const noexcept
    {
        RangeLatencies latencies {};

        for (size_t rangeIndex = 0; rangeIndex < createdRangeCount; ++rangeIndex)
            if (processors[rangeIndex] != nullptr)
                latencies[rangeIndex] = processors[rangeIndex]->getLatencySamples();

        return latencies;
    }

    void setSelectedRange(const size_t rangeIndex) noexcept
    {
        selectedRange = createdRangeCount == 0 ? 0 : std::min(rangeIndex, createdRangeCount.load() - 1);
    }

    ModuleProcessor* getProcessor(const size_t rangeIndex) noexcept
    {
        return rangeIndex < createdRangeCount.load(std::memory_order_acquire) ? processors[rangeIndex].get() : nullptr;
    }

    const ModuleProcessor* getProcessor(const size_t rangeIndex) const noexcept
    {
        return rangeIndex < createdRangeCount.load(std::memory_order_acquire) ? processors[rangeIndex].get() : nullptr;
    }

    ModuleProcessor* getSelectedProcessor() noexcept
    {
        return selectedRange.load() < createdRangeCount.load() ? processors[selectedRange.load()].get() : nullptr;
    }

    const ModuleProcessor* getSelectedProcessor() const noexcept
    {
        return selectedRange.load() < createdRangeCount.load() ? processors[selectedRange.load()].get() : nullptr;
    }

    template <typename Visitor>
    void forEachProcessor(Visitor&& visitor)
    {
        const auto count = createdRangeCount.load(std::memory_order_acquire);
        for (size_t i = 0; i < count; ++i) visitor(*processors[i]);
    }

private:
    std::function<std::unique_ptr<ModuleProcessor>()> processorFactory;
    std::array<std::unique_ptr<ModuleProcessor>, numRanges> processors;
    std::atomic<size_t> createdRangeCount { 0 };
    std::atomic<size_t> selectedRange { 0 };
    double preparedSampleRate = 0.0;
    int preparedSamplesPerBlock = 0;
    bool prepared = false;
};
}
