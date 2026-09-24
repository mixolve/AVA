#include "ProcessorBank.h"

#include <algorithm>
#include <vector>

namespace
{
constexpr auto bankStateType = "fft_bank_state";
constexpr auto rangeStateType = "band";
constexpr auto rangeCountKey = "band_count";
constexpr auto rangeIndexKey = "index";
}

FftProcessorBank::FftProcessorBank(juce::AudioProcessor& owner)
    : ranges([&owner] { return std::make_unique<FftModuleProcessor>(owner); })
{}

void FftProcessorBank::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    ranges.prepareToPlay(sampleRate, samplesPerBlock);
}

void FftProcessorBank::releaseResources()
{
    ranges.releaseResources();
}

void FftProcessorBank::resetProcessingState() noexcept
{
    ranges.resetProcessingState();
}

void FftProcessorBank::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
{
    ranges.processRange(rangeIndex, buffer);
}

int FftProcessorBank::getLatencySamples() const noexcept
{
    const auto latencies = ranges.getRangeLatencies();
    return *std::max_element(latencies.begin(), latencies.end());
}

ava::modules::ProcessorRangeBank<FftModuleProcessor>::RangeLatencies FftProcessorBank::getRangeLatencies() const noexcept
{
    return ranges.getRangeLatencies();
}

bool FftProcessorBank::refreshLatencyState() noexcept
{
    auto changed = false;
    ranges.forEachProcessor([&changed] (FftModuleProcessor& processor)
    {
        changed = processor.refreshLatencyState() || changed;
    });

    return changed;
}

size_t FftProcessorBank::ensureRangeCount(const size_t rangeCount)
{
    return ranges.ensureRangeCount(rangeCount);
}

size_t FftProcessorBank::getCreatedRangeCount() const noexcept
{
    return ranges.getCreatedRangeCount();
}

juce::String FftProcessorBank::getStateXmlString() const
{
    juce::ValueTree bankState(bankStateType);
    const auto rangeCount = ranges.getCreatedRangeCount();
    bankState.setProperty(rangeCountKey, static_cast<int>(rangeCount), nullptr);

    for (size_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
    {
        const auto* processor = ranges.getProcessor(rangeIndex);

        if (processor == nullptr)
            return {};

        auto processorXml = juce::parseXML(processor->getStateXmlString());

        if (processorXml == nullptr)
            return {};

        juce::ValueTree rangeState(rangeStateType);
        rangeState.setProperty(rangeIndexKey, static_cast<int>(rangeIndex), nullptr);
        rangeState.appendChild(juce::ValueTree::fromXml(*processorXml), nullptr);
        bankState.appendChild(rangeState, nullptr);
    }

    if (auto bankXml = bankState.createXml())
        return bankXml->toString();

    return {};
}

bool FftProcessorBank::setStateFromXmlString(const juce::String& stateXmlString)
{
    if (stateXmlString.isEmpty())
        return false;

    auto bankXml = juce::parseXML(stateXmlString);

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

    const auto* schemaProcessor = ranges.getProcessor(0);

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

        if (! schemaProcessor->isCurrentState(restoredState))
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

        if (rangeXml == nullptr || ! processor->setStateFromXmlString(rangeXml->toString()))
            return false;
    }

    return true;
}

void FftProcessorBank::setSelectedRange(const size_t rangeIndex) noexcept
{
    ranges.setSelectedRange(rangeIndex);
}

FftModuleProcessor* FftProcessorBank::getProcessor(const size_t rangeIndex) noexcept
{
    return ranges.getProcessor(rangeIndex);
}

const FftModuleProcessor* FftProcessorBank::getProcessor(const size_t rangeIndex) const noexcept
{
    return ranges.getProcessor(rangeIndex);
}

FftModuleProcessor* FftProcessorBank::getSelectedProcessor() noexcept
{
    return ranges.getSelectedProcessor();
}

const FftModuleProcessor* FftProcessorBank::getSelectedProcessor() const noexcept
{
    return ranges.getSelectedProcessor();
}
