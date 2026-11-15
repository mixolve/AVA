#include "RangeBank.h"
#include "Presets.h"
#include "PresetStorage.h"
#include "StateValidation.h"
#include "../../crossover/UiStateSchema.h"

#include <algorithm>
#include <vector>

namespace
{
constexpr auto bankStateType = "eql_bank_state";
constexpr auto rangeStateType = "band";
constexpr auto rangeCountKey = "band_count";
constexpr auto rangeIndexKey = "index";
}

EqlProcessorBank::EqlProcessorBank()
    : ranges([] { return std::make_unique<EqlModuleProcessor>(); })
{
    startTimer(1000);
}

EqlProcessorBank::~EqlProcessorBank() { stopTimer(); }

void EqlProcessorBank::timerCallback()
{
    if (!initialFilterPresetLoaded) return;
    const auto stamp = getFilterPresetCatalogStamp();
    if (stamp == presetCatalogStamp && !stamp.isEmpty()) return;
    // The bank owns external catalog changes; the editor does not choose fallback states.
    eql_presets::ensureDefaultPresetExists();
    auto catalog = loadFilterPresetsXml();
    ranges.forEachProcessor([&] (EqlModuleProcessor& processor)
    {
        processor.resolveFilterPresetSelection(catalog.get());
    });
    presetCatalogStamp = getFilterPresetCatalogStamp();
}

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

void EqlProcessorBank::syncParameters(const std::array<int, numRanges>& limits, bool acceptModel) noexcept
{
    for (size_t band = 0; band < ranges.getCreatedRangeCount(); ++band) {
        auto* processor = ranges.getProcessor(band);
        processor->setProcessingFilterLimit(limits[band], acceptModel);
        processor->syncParameters();
    }
}

void EqlProcessorBank::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
{
    ranges.processRange(rangeIndex, buffer);
}

ava::modules::ProcessorRangeBank<EqlModuleProcessor>::RangeLatencies EqlProcessorBank::getRangeLatencies() const noexcept
{
    return ranges.getRangeLatencies();
}

void EqlProcessorBank::loadInitialFilterPreset()
{
    eql_presets::ensureDefaultPresetExists();

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

        juce::ValueTree rangeState(rangeStateType);
        rangeState.setProperty(rangeIndexKey, static_cast<int>(rangeIndex), nullptr);
        rangeState.appendChild(copySerializableState(*processor), nullptr);
        bankState.appendChild(rangeState, nullptr);
    }

    if (auto bankXml = bankState.createXml())
        juce::AudioProcessor::copyXmlToBinary(*bankXml, destData);
}

std::optional<std::vector<juce::ValueTree>> eql_state::readBankState(
    const juce::ValueTree& bankState, juce::AudioProcessorValueTreeState& parameters)
{
    if (!bankState.hasType(bankStateType)
        || bankState.getNumProperties() != 1
        || !bankState.hasProperty(rangeCountKey)
        || !crossover_ui::hasExactIntegerValue(bankState.getProperty(rangeCountKey), 1,
                                               static_cast<int>(EqlProcessorBank::numRanges)))
        return std::nullopt;

    const auto rangeCount = static_cast<int>(bankState.getProperty(rangeCountKey));
    if (rangeCount != bankState.getNumChildren()) return std::nullopt;

    std::vector<juce::ValueTree> states;
    states.reserve(static_cast<size_t>(rangeCount));
    for (int range = 0; range < rangeCount; ++range)
    {
        const auto wrapper = bankState.getChild(range);
        if (!wrapper.hasType(rangeStateType) || wrapper.getNumProperties() != 1
            || wrapper.getProperty(rangeIndexKey).toString() != juce::String(range)
            || wrapper.getNumChildren() != 1
            || !isCurrentState(wrapper.getChild(0), parameters))
            return std::nullopt;
        states.push_back(wrapper.getChild(0));
    }
    return states;
}

bool EqlProcessorBank::setStateInformation(const void* data, const int sizeInBytes)
{
    const auto xml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);
    auto* schema = ranges.getProcessor(0);
    if (xml == nullptr || schema == nullptr) return false;
    const auto states = eql_state::readBankState(juce::ValueTree::fromXml(*xml), schema->getValueTreeState());
    if (!states.has_value() || ranges.setRangeCount(states->size()) != states->size()) return false;

    for (size_t range = 0; range < states->size(); ++range)
        if (!ranges.getProcessor(range)->restoreState((*states)[range].createCopy())) return false;

    initialFilterPresetLoaded = true;
    return true;
}

void EqlProcessorBank::setSelectedRange(const size_t rangeIndex) noexcept
{
    ranges.setSelectedRange(rangeIndex);
}

EqlModuleProcessor* EqlProcessorBank::getProcessor(const size_t rangeIndex) noexcept
{
    return ranges.getProcessor(rangeIndex);
}

const EqlModuleProcessor* EqlProcessorBank::getProcessor(const size_t rangeIndex) const noexcept
{
    return ranges.getProcessor(rangeIndex);
}

EqlModuleProcessor* EqlProcessorBank::getSelectedProcessor() noexcept
{
    return ranges.getSelectedProcessor();
}

const EqlModuleProcessor* EqlProcessorBank::getSelectedProcessor() const noexcept
{
    return ranges.getSelectedProcessor();
}
