#include "ProcessorBank.h"
#include "Presets.h"
#include "State.h"

#include <algorithm>
#include <vector>


namespace
{
constexpr auto bankStateType = "eql_bank_state";
constexpr auto rangeStateType = "range";
constexpr auto rangeCountKey = "range_count";
constexpr auto rangeIndexKey = "index";
}

EqlProcessorBank::EqlProcessorBank()
    : ranges([] { return std::make_unique<EqlModuleProcessor>(); })
{}

void EqlProcessorBank::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    ranges.prepareToPlay(sampleRate, samplesPerBlock);
}

void EqlProcessorBank::releaseResources()
{
    ranges.releaseResources();
}

void EqlProcessorBank::resetProcessingState() noexcept
{
    ranges.resetProcessingState();
}

void EqlProcessorBank::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
{
    ranges.processRange(rangeIndex, buffer);
}

int EqlProcessorBank::getLatencySamples() const noexcept
{
    const auto latencies = ranges.getRangeLatencies();
    return *std::max_element(latencies.begin(), latencies.end());
}

ava::modules::ProcessorRangeBank<EqlModuleProcessor>::RangeLatencies EqlProcessorBank::getRangeLatencies() const noexcept
{
    return ranges.getRangeLatencies();
}

void EqlProcessorBank::loadInitialFilterPreset()
{
    if (auto* primaryProcessor = ranges.getProcessor(0))
        eql_presets::ensureDefaultPresetExists(*primaryProcessor);

    ranges.forEachProcessor([] (EqlModuleProcessor& processor)
    {
        processor.loadInitialFilterPreset();
    });

    initialFilterPresetLoaded = true;
}

size_t EqlProcessorBank::ensureRangeCount(const size_t rangeCount)
{
    const auto previousRangeCount = ranges.getCreatedRangeCount();
    const auto createdRangeCount = ranges.ensureRangeCount(rangeCount);

    if (initialFilterPresetLoaded)
    {
        for (auto rangeIndex = previousRangeCount; rangeIndex < createdRangeCount; ++rangeIndex)
            if (auto* processor = ranges.getProcessor(rangeIndex))
                processor->loadInitialFilterPreset();
    }

    return createdRangeCount;
}

size_t EqlProcessorBank::getCreatedRangeCount() const noexcept
{
    return ranges.getCreatedRangeCount();
}

int EqlProcessorBank::getRangeActiveFilterCount(const size_t rangeIndex) const noexcept
{
    const auto* processor = ranges.getProcessor(rangeIndex);
    return processor != nullptr ? processor->getActiveFilterCount() : -1;
}

void EqlProcessorBank::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree bankState(bankStateType);
    const auto rangeCount = ranges.getCreatedRangeCount();
    bankState.setProperty(rangeCountKey, static_cast<int>(rangeCount), nullptr);

    for (size_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
    {
        auto* processor = ranges.getProcessor(rangeIndex);

        if (processor == nullptr)
            return;

        juce::MemoryBlock rangeData;
        processor->getStateInformation(rangeData);
        auto rangeXml = juce::AudioProcessor::getXmlFromBinary(rangeData.getData(), static_cast<int>(rangeData.getSize()));

        if (rangeXml == nullptr)
            return;

        juce::ValueTree rangeState(rangeStateType);
        rangeState.setProperty(rangeIndexKey, static_cast<int>(rangeIndex), nullptr);
        rangeState.appendChild(juce::ValueTree::fromXml(*rangeXml), nullptr);
        bankState.appendChild(rangeState, nullptr);
    }

    if (auto bankXml = bankState.createXml())
        juce::AudioProcessor::copyXmlToBinary(*bankXml, destData);
}

bool EqlProcessorBank::setStateInformation(const void* data, const int sizeInBytes)
{
    auto bankXml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);

    if (bankXml == nullptr || ! bankXml->hasTagName(bankStateType))
        return false;

    auto bankState = juce::ValueTree::fromXml(*bankXml);

    if (bankState.getNumProperties() != 1
        || ! bankState.hasProperty(rangeCountKey)
        || bankState.getNumChildren() < 1
        || bankState.getNumChildren() > static_cast<int>(numRanges))
        return false;

    const auto rangeCountText = bankState.getProperty(rangeCountKey).toString();
    const auto rangeCount = rangeCountText.getIntValue();

    if (rangeCountText != juce::String(rangeCount)
        || rangeCount != bankState.getNumChildren()
        || rangeCount < 1
        || rangeCount > static_cast<int>(numRanges))
        return false;

    auto* schemaProcessor = ranges.getProcessor(0);

    if (schemaProcessor == nullptr)
        return false;

    std::vector<juce::ValueTree> restoredRangeStates;
    restoredRangeStates.reserve(static_cast<size_t>(rangeCount));

    for (int rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
    {
        const auto wrapper = bankState.getChild(rangeIndex);

        if (! wrapper.hasType(rangeStateType)
            || wrapper.getNumProperties() != 1
            || wrapper.getProperty(rangeIndexKey).toString() != juce::String(rangeIndex)
            || wrapper.getNumChildren() != 1)
            return false;

        auto restoredState = wrapper.getChild(0).createCopy();

        if (! eql_state::isCurrentState(restoredState, schemaProcessor->getValueTreeState()))
            return false;

        restoredRangeStates.push_back(std::move(restoredState));
    }

    if (ranges.setRangeCount(static_cast<size_t>(rangeCount)) != static_cast<size_t>(rangeCount))
        return false;

    for (int rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
    {
        auto* processor = ranges.getProcessor(static_cast<size_t>(rangeIndex));

        if (processor == nullptr)
            return false;

        auto rangeXml = restoredRangeStates[static_cast<size_t>(rangeIndex)].createXml();

        if (rangeXml == nullptr)
            return false;

        juce::MemoryBlock rangeData;
        juce::AudioProcessor::copyXmlToBinary(*rangeXml, rangeData);

        if (! processor->setStateInformation(rangeData.getData(), static_cast<int>(rangeData.getSize())))
            return false;
    }

    initialFilterPresetLoaded = true;
    return true;
}

void EqlProcessorBank::setSelectedRange(const size_t rangeIndex) noexcept
{
    ranges.setSelectedRange(rangeIndex);
}

EqlModuleProcessor* EqlProcessorBank::getSelectedProcessor() noexcept
{
    return ranges.getSelectedProcessor();
}

const EqlModuleProcessor* EqlProcessorBank::getSelectedProcessor() const noexcept
{
    return ranges.getSelectedProcessor();
}
