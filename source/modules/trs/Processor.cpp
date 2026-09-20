#include "Processor.h"
#include "../shared/StateUtilities.h"
#include "../../crossover/ParameterIds.h"
#include "../../crossover/UiState.h"
#include "../shared/DspUtilities.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

namespace
{
using ava::crossover::parameters::makeRangeGroupId;
using ava::crossover::parameters::makeRangeGroupName;
using ava::crossover::parameters::makeRangeParameterId;

struct ParameterOrderEntry
{
    const char* key;
    const char* label;
};

inline constexpr auto trsCrossoverOrder = std::to_array<ParameterOrderEntry>({
    { TrsModuleProcessor::paramTransientEnabledId, "TRANSIENT / MUTE" },
    { TrsModuleProcessor::paramSustainEnabledId, "SUSTAIN / MUTE" },
    { TrsModuleProcessor::paramTransientGainId, "TRANSIENT / GAIN" },
    { TrsModuleProcessor::paramSustainGainId, "SUSTAIN / GAIN" },
    { TrsModuleProcessor::paramHoldId, "HOLD" },
    { TrsModuleProcessor::paramHoldModeId, "HOLD-TYPE" },
    { TrsModuleProcessor::paramHoldSyncId, "HOLD" },
    { TrsModuleProcessor::paramReleaseId, "RELEASE" },
    { TrsModuleProcessor::paramReleaseModeId, "REL-TYPE" },
    { TrsModuleProcessor::paramReleaseSyncId, "RELEASE" },
    { TrsModuleProcessor::paramReleaseCurveId, "REL-CURVE" },
    { TrsModuleProcessor::paramThresholdId, "THRESH" },
    { TrsModuleProcessor::paramKneeId, "KNEE" },
    { TrsModuleProcessor::paramRetriggerId, "RETRIGGER" },
    { TrsModuleProcessor::paramOneShotId, "ONE-SHOT" },
    { TrsModuleProcessor::paramLookaheadId, "LOOKAHEAD" },
});

constexpr auto gainMinDb = -48.0f;
constexpr auto gainMaxDb = 48.0f;
constexpr std::array<int, 9> hostSyncDenominators { 1, 2, 4, 8, 16, 32, 64, 128, 256 };

juce::String formatGainValue(const float value)
{
    return juce::String::formatted("%.2f dB", static_cast<double>(value));
}

juce::String formatDecibelValue(const float value)
{
    return juce::String::formatted("%.1f dB", static_cast<double>(value));
}

juce::String formatTimeValue(const float value)
{
    return juce::String::formatted("%.2f ms", static_cast<double>(value));
}

juce::String formatCurveValue(const float value)
{
    return juce::String::formatted("%.2f", static_cast<double>(value));
}

float getParameterValue(const std::atomic<float>* parameter, const float fallback) noexcept
{
    return parameter != nullptr ? parameter->load(std::memory_order_relaxed) : fallback;
}

bool isEnabled(const std::atomic<float>* parameter, const bool fallback) noexcept
{
    return getParameterValue(parameter, fallback ? 1.0f : 0.0f) >= 0.5f;
}

float getHostSyncMilliseconds(const int choiceIndex, const double bpm, const int typeIndex) noexcept
{
    const auto safeBpm = bpm > 0.0 ? bpm : 120.0;
    const auto safeIndex = juce::jlimit(0, static_cast<int>(hostSyncDenominators.size()) - 1, choiceIndex);
    const auto denominator = hostSyncDenominators[static_cast<size_t>(safeIndex)];
    const auto baseMilliseconds = static_cast<float>((60000.0 / safeBpm) * (4.0 / static_cast<double>(denominator)));

    if (typeIndex == 2)
        return baseMilliseconds * (2.0f / 3.0f);

    if (typeIndex == 3)
        return baseMilliseconds * 1.5f;

    return baseMilliseconds;
}
}

TrsModuleProcessor::TrsModuleProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
      parameters(moduleParameterHost, nullptr, "trs_state", createParameterLayout())
{
    cacheParameterPointers();
}

TrsModuleProcessor::~TrsModuleProcessor() = default;

void TrsModuleProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    processorBank.prepare(currentSampleRate, preparedBlockSize, ownerProcessor.getTotalNumOutputChannels());
    syncParameters();
    processorBank.reset();
}

void TrsModuleProcessor::releaseResources()
{
    processorBank.releaseResources();
}

void TrsModuleProcessor::resetProcessingState()
{
    processorBank.reset();
}

void TrsModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    ava::modules::dsp::clearOutputOnlyChannels(ownerProcessor, buffer);

    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return;

    syncParameters();
    processorBank.processRange(0, buffer);
}

juce::String TrsModuleProcessor::getStateXmlString() const
{
    auto state = const_cast<juce::AudioProcessorValueTreeState&>(parameters).copyState();

    if (auto stateXml = state.createXml())
        return stateXml->toString();

    return {};
}

bool TrsModuleProcessor::setStateFromXmlString(const juce::String& stateXmlString)
{
    if (stateXmlString.trim().isEmpty())
        return false;

    auto stateXml = juce::parseXML(stateXmlString);

    if (stateXml == nullptr)
        return false;

    auto restoredState = juce::ValueTree::fromXml(*stateXml);

    if (! ava::modules::state::hasExactParameterState(restoredState, parameters)
        || ! crossover_ui::hasCurrentStateProperties(restoredState, "trs"))
        return false;

    parameters.replaceState(restoredState);
    cacheParameterPointers();
    syncParameters();
    return true;
}

int TrsModuleProcessor::getLatencySamples() const noexcept
{
    return moduleLatencySamples;
}

TrsModuleProcessor::RangeLatencies TrsModuleProcessor::getRangeLatencies() const noexcept
{
    return processorBank.getRangeLatencies();
}

size_t TrsModuleProcessor::ensureRangeCount(const size_t rangeCount)
{
    const auto createdRangeCount = processorBank.ensureRangeCount(rangeCount);
    processorBank.setRangeParameters(currentRangeParameters);
    return createdRangeCount;
}

size_t TrsModuleProcessor::getCreatedRangeCount() const noexcept
{
    return processorBank.getCreatedRangeCount();
}

void TrsModuleProcessor::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
{
    processorBank.processRange(rangeIndex, buffer);
}

bool TrsModuleProcessor::refreshLatencyState() noexcept
{
    const auto previousLatency = moduleLatencySamples;
    syncParameters();
    return previousLatency != moduleLatencySamples;
}

juce::AudioProcessorValueTreeState& TrsModuleProcessor::getValueTreeState() noexcept
{
    return parameters;
}

const juce::AudioProcessorValueTreeState& TrsModuleProcessor::getValueTreeState() const noexcept
{
    return parameters;
}

juce::StringArray TrsModuleProcessor::getHostSyncChoices()
{
    return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128", "1/256" };
}

int TrsModuleProcessor::getDefaultHostSyncChoiceIndex() noexcept
{
    return 4;
}

juce::AudioProcessorValueTreeState::ParameterLayout TrsModuleProcessor::createParameterLayout()
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using Parameter = std::unique_ptr<juce::RangedAudioParameter>;

    const juce::StringArray timeModeChoices { "MS", "NOTE", "NOTE TRIPLET", "NOTE DOTTED" };

    auto boolParam = [] (const juce::String& id,
                         const juce::String& name,
                         const bool defaultValue,
                         const bool isMeta = false) -> Parameter
    {
        return std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 },
            name,
            defaultValue,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(isMeta));
    };

    auto floatParam = [] (const juce::String& id,
                          const juce::String& name,
                          const juce::NormalisableRange<float>& range,
                          const float defaultValue,
                          std::function<juce::String(float, int)> formatter,
                          const bool isMeta = false) -> Parameter
    {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { id, 1 },
            name,
            range,
            defaultValue,
            juce::AudioParameterFloatAttributes()
                .withAutomatable(false)
                .withMeta(isMeta)
                .withStringFromValueFunction(std::move(formatter)));
    };

    auto choiceParam = [] (const juce::String& id,
                           const juce::String& name,
                           const juce::StringArray& choices,
                           const int defaultIndex,
                           const bool isMeta = false) -> Parameter
    {
        return std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { id, 1 },
            name,
            choices,
            defaultIndex,
            juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(isMeta));
    };

    Layout layout;

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto crossoverGroup = std::make_unique<juce::AudioProcessorParameterGroup>(
            makeRangeGroupId(rangeIndex),
            makeRangeGroupName(rangeIndex),
            " | ");

        for (const auto& entry : trsCrossoverOrder)
        {
            const auto key = juce::String(entry.key);
            const auto id = makeRangeParameterId(rangeIndex, entry.key);
            const auto name = "TRS / RANGE " + juce::String(static_cast<int>(rangeIndex + 1))
                + " / TRANSIENT PROCESSOR / " + juce::String(entry.label);

            if (key == paramTransientEnabledId || key == paramSustainEnabledId)
            {
                crossoverGroup->addChild(boolParam(id, name, true));
                continue;
            }

            if (key == paramTransientGainId || key == paramSustainGainId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { gainMinDb, gainMaxDb, 0.01f },
                                               0.0f,
                                               [] (float value, int) { return formatGainValue(value); }));
                continue;
            }

            if (key == paramHoldId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { 0.0f, 200.0f, 1.0f },
                                               0.0f,
                                               [] (float value, int) { return formatTimeValue(value); }));
                continue;
            }

            if (key == paramHoldModeId || key == paramReleaseModeId)
            {
                crossoverGroup->addChild(choiceParam(id, name, timeModeChoices, 0));
                continue;
            }

            if (key == paramHoldSyncId || key == paramReleaseSyncId)
            {
                crossoverGroup->addChild(choiceParam(id, name, getHostSyncChoices(), getDefaultHostSyncChoiceIndex()));
                continue;
            }

            if (key == paramReleaseId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { 1.0f, 500.0f, 1.0f },
                                               10.0f,
                                               [] (float value, int) { return formatTimeValue(value); }));
                continue;
            }

            if (key == paramReleaseCurveId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { -100.0f, 100.0f, 1.0f },
                                               0.0f,
                                               [] (float value, int) { return formatCurveValue(value); }));
                continue;
            }

            if (key == paramThresholdId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { -48.0f, 0.0f, 0.01f },
                                               -48.0f,
                                               [] (float value, int) { return formatDecibelValue(value); }));
                continue;
            }

            if (key == paramKneeId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { 0.0f, 24.0f, 0.01f },
                                               0.0f,
                                               [] (float value, int) { return formatDecibelValue(value); }));
                continue;
            }

            if (key == paramRetriggerId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { 1.0f, 5000.0f, 0.01f },
                                               1.0f,
                                               [] (float value, int) { return formatTimeValue(value); }));
                continue;
            }

            if (key == paramOneShotId)
            {
                crossoverGroup->addChild(boolParam(id, name, false));
                continue;
            }

            if (key == paramLookaheadId)
            {
                crossoverGroup->addChild(floatParam(id,
                                               name,
                                               juce::NormalisableRange<float> { 0.0f, 20.0f, 0.01f },
                                               1.0f,
                                               [] (float value, int) { return formatTimeValue(value); }));
            }
        }

        layout.add(std::move(crossoverGroup));
    }

    return layout;
}

void TrsModuleProcessor::cacheParameterPointers()
{
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto& crossover = rawRangeParameters[rangeIndex];
        crossover.transientEnabled = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramTransientEnabledId));
        crossover.transientGain = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramTransientGainId));
        crossover.sustainEnabled = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramSustainEnabledId));
        crossover.sustainGain = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramSustainGainId));
        crossover.hold = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramHoldId));
        crossover.holdMode = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramHoldModeId));
        crossover.holdSync = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramHoldSyncId));
        crossover.release = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramReleaseId));
        crossover.releaseCurve = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramReleaseCurveId));
        crossover.releaseMode = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramReleaseModeId));
        crossover.releaseSync = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramReleaseSyncId));
        crossover.threshold = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramThresholdId));
        crossover.knee = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramKneeId));
        crossover.retrigger = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramRetriggerId));
        crossover.oneShot = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramOneShotId));
        crossover.lookahead = parameters.getRawParameterValue(makeRangeParameterId(rangeIndex, paramLookaheadId));
    }

}

trs::dsp::DspCore::Parameters TrsModuleProcessor::readCrossoverRangeParameters(const size_t rangeIndex, const double hostBpm) const noexcept
{
    const auto& crossover = rawRangeParameters[juce::jmin(rangeIndex, numRanges - 1)];
    const auto holdType = static_cast<int>(std::round(getParameterValue(crossover.holdMode, 0.0f)));
    const auto releaseType = static_cast<int>(std::round(getParameterValue(crossover.releaseMode, 0.0f)));
    const auto holdHostSync = holdType > 0;
    const auto releaseHostSync = releaseType > 0;
    const auto holdSyncIndex = static_cast<int>(std::round(getParameterValue(crossover.holdSync,
                                                                              static_cast<float>(getDefaultHostSyncChoiceIndex()))));
    const auto releaseSyncIndex = static_cast<int>(std::round(getParameterValue(crossover.releaseSync,
                                                                                 static_cast<float>(getDefaultHostSyncChoiceIndex()))));

    trs::dsp::DspCore::Parameters result;
    result.transientEnabled = isEnabled(crossover.transientEnabled, true);
    result.sustainEnabled = isEnabled(crossover.sustainEnabled, true);
    result.transientGainDb = juce::jlimit(gainMinDb, gainMaxDb, getParameterValue(crossover.transientGain, 0.0f));
    result.sustainGainDb = juce::jlimit(gainMinDb, gainMaxDb, getParameterValue(crossover.sustainGain, 0.0f));
    result.holdMs = holdHostSync ? getHostSyncMilliseconds(holdSyncIndex, hostBpm, holdType)
                                 : juce::jlimit(0.0f, 200.0f, getParameterValue(crossover.hold, 0.0f));
    result.releaseMs = releaseHostSync ? getHostSyncMilliseconds(releaseSyncIndex, hostBpm, releaseType)
                                       : juce::jlimit(1.0f, 500.0f, getParameterValue(crossover.release, 10.0f));
    result.releaseCurve = juce::jlimit(-100.0f, 100.0f, getParameterValue(crossover.releaseCurve, 0.0f));
    result.thresholdDb = juce::jlimit(-48.0f, 0.0f, getParameterValue(crossover.threshold, -48.0f));
    result.kneeDb = juce::jlimit(0.0f, 24.0f, getParameterValue(crossover.knee, 0.0f));
    result.retriggerMs = juce::jlimit(1.0f, 5000.0f, getParameterValue(crossover.retrigger, 1.0f));
    result.oneShot = isEnabled(crossover.oneShot, false);
    result.lookaheadMs = juce::jlimit(0.0f, 20.0f, getParameterValue(crossover.lookahead, 1.0f));
    return result;
}

void TrsModuleProcessor::syncParameters()
{
    const auto hostBpm = getHostBpm();

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        currentRangeParameters[rangeIndex] = readCrossoverRangeParameters(rangeIndex, hostBpm);

    processorBank.setRangeParameters(currentRangeParameters);
    const auto rangeLatencies = processorBank.getRangeLatencies();
    moduleLatencySamples = *std::max_element(rangeLatencies.begin(), rangeLatencies.end());
}

double TrsModuleProcessor::getHostBpm() const noexcept
{
    if (auto* playHead = ownerProcessor.getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                if (*bpm > 0.0)
                    return *bpm;
            }
        }
    }

    return 120.0;
}
