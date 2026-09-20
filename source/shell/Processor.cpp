#include "Processor.h"
#include "../modules/eql/ProcessorBank.h"
#include "../modules/fft/ProcessorBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/fft/Processor.h"
#include "../modules/trs/Processor.h"
#include "../modules/eql/Processor.h"

#include <memory>
#include <utility>
#include <vector>

AvaAudioProcessor::AvaAudioProcessor()
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "ava_state", createParameterLayout())
{
    globalBypassParam = parameters.getRawParameterValue(paramGlobalBypassId);
    crossoverActiveSplitCountParam = parameters.getRawParameterValue(paramCrossoverActiveSplitCountId);

    for (size_t splitIndex = 0; splitIndex < crossoverSplitFrequencyParams.size(); ++splitIndex)
    {
        const auto suffix = "split_" + juce::String(static_cast<int>(splitIndex + 1)) + "_frequency";
        crossoverSplitFrequencyParams[splitIndex] = parameters.getRawParameterValue(getCrossoverParameterId(suffix.toRawUTF8()));
    }

    for (size_t rangeIndex = 0; rangeIndex < crossoverSoloParams.size(); ++rangeIndex)
        crossoverSoloParams[rangeIndex] = parameters.getRawParameterValue(getCrossoverSoloParameterId(rangeIndex));

    constexpr std::array<const char*, 7> globalListenSuffixes {
        "listen_lc", "listen_rc", "listen_mc", "listen_sc", "listen_ll", "listen_rr", "listen_ss"
    };

    for (size_t listenIndex = 0; listenIndex < globalListenParams.size(); ++listenIndex)
        globalListenParams[listenIndex] = parameters.getRawParameterValue(getCrossoverParameterId(globalListenSuffixes[listenIndex]));

    parameters.addParameterListener(paramCrossoverActiveSplitCountId, this);

    for (int slotIndex = 0; slotIndex < hostAutomationSlotCount; ++slotIndex)
    {
        hostSlotParameterIds[static_cast<size_t>(slotIndex)] = getHostSlotParameterId(slotIndex);
        parameters.addParameterListener(hostSlotParameterIds[static_cast<size_t>(slotIndex)], this);
    }
}

AvaAudioProcessor::~AvaAudioProcessor()
{
    cancelPendingUpdate();
    parameters.removeParameterListener(paramCrossoverActiveSplitCountId, this);

    for (const auto& parameterId : hostSlotParameterIds)
        parameters.removeParameterListener(parameterId, this);

    clearActiveModuleStateListeners();
}

juce::String AvaAudioProcessor::getHostSlotParameterId(const int slotIndex)
{
    const auto clampedSlot = juce::jlimit(0, hostAutomationSlotCount - 1, slotIndex);
    return juce::String(paramHostSlotPrefix) + juce::String::formatted("%02d", clampedSlot + 1);
}

juce::String AvaAudioProcessor::getHostSlotLetterLabel(const int slotIndex)
{
    const auto index = juce::jlimit(0, hostAutomationSlotCount - 1, slotIndex);
    const auto first = index / 26;
    const auto second = index % 26;

    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + first))
        + juce::String::charToString(static_cast<juce::juce_wchar>('A' + second));
}

juce::String AvaAudioProcessor::getHostSlotTargetStateKey(const int slotIndex)
{
    const auto clampedSlot = juce::jlimit(0, hostAutomationSlotCount - 1, slotIndex);
    return "ava.host_slot." + juce::String::formatted("%02d", clampedSlot + 1) + ".target";
}

juce::String AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(const size_t rangeIndex)
{
    const auto clampedRange = juce::jmin(rangeIndex, ava::crossover::BufferRouter::numRanges - 1);
    return "ava.editor.eql.range_" + juce::String(static_cast<int>(clampedRange)) + ".filter_display_order";
}

juce::String AvaAudioProcessor::getCrossoverParameterId(const char* suffix)
{
    return juce::String(paramCrossoverPrefix) + suffix;
}

juce::String AvaAudioProcessor::getCrossoverSoloParameterId(const size_t rangeIndex)
{
    return juce::String(paramCrossoverPrefix) + "solo_range_" + juce::String(static_cast<int>(rangeIndex + 1));
}

ava::crossover::Settings AvaAudioProcessor::getCrossoverSettings() const noexcept
{
    ava::crossover::Settings settings;

    if (crossoverActiveSplitCountParam != nullptr)
        settings.activeSplitCount = static_cast<size_t>(juce::jlimit(0,
                                                                    static_cast<int>(ava::crossover::BufferRouter::numSplits),
                                                                    juce::roundToInt(crossoverActiveSplitCountParam->load(std::memory_order_relaxed))));

    for (size_t index = 0; index < settings.splitFrequencies.size(); ++index)
        if (const auto* value = crossoverSplitFrequencyParams[index])
            settings.splitFrequencies[index] = value->load(std::memory_order_relaxed);

    for (size_t index = 0; index < settings.soloMask.size(); ++index)
        if (const auto* value = crossoverSoloParams[index])
            settings.soloMask[index] = value->load(std::memory_order_relaxed) >= 0.5f;

    return settings;
}

juce::RangedAudioParameter* AvaAudioProcessor::findHostSlotTarget(const juce::String& parameterId) noexcept
{
    if (parameterId.isEmpty() || parameterId.startsWith(paramHostSlotPrefix))
        return nullptr;

    if (auto* parameter = parameters.getParameter(parameterId))
        return parameter;

    const auto findInModule = [&parameterId] (auto* processor) -> juce::RangedAudioParameter*
    {
        return processor != nullptr ? processor->getValueTreeState().getParameter(parameterId) : nullptr;
    };

    switch (activeModule.load(std::memory_order_acquire))
    {
        case ActiveModule::eql: return findInModule(getEqlModuleProcessor());
        case ActiveModule::fft: return findInModule(getFftModuleProcessor());
        case ActiveModule::tls: return findInModule(getTlsModuleProcessor());
        case ActiveModule::dyn: return findInModule(getDynModuleProcessor());
        case ActiveModule::trs: return findInModule(getTrsModuleProcessor());
        case ActiveModule::none: break;
    }

    return nullptr;
}

void AvaAudioProcessor::refreshHostSlotTargets()
{
    const juce::ScopedLock lock(processingLock);

    for (int slotIndex = 0; slotIndex < hostAutomationSlotCount; ++slotIndex)
    {
        const auto targetParameterId = parameters.state.getProperty(getHostSlotTargetStateKey(slotIndex)).toString();
        hostSlotTargets[static_cast<size_t>(slotIndex)].store(findHostSlotTarget(targetParameterId),
                                                              std::memory_order_release);
    }
}

void AvaAudioProcessor::applyHostSlotValue(const int slotIndex, const float normalizedValue) noexcept
{
    if (! juce::isPositiveAndBelow(slotIndex, hostAutomationSlotCount))
        return;

    const juce::ScopedTryLock lock(processingLock);

    if (! lock.isLocked())
        return;

    if (auto* targetParameter = hostSlotTargets[static_cast<size_t>(slotIndex)].load(std::memory_order_acquire))
        targetParameter->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, normalizedValue));
}

juce::AudioProcessorValueTreeState::ParameterLayout AvaAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterLayout;

    for (int slotIndex = 0; slotIndex < hostAutomationSlotCount; ++slotIndex)
    {
        parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { getHostSlotParameterId(slotIndex), 2 },
            getHostSlotLetterLabel(slotIndex),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.0f },
            0.0f,
            juce::AudioParameterFloatAttributes().withAutomatable(true)));
    }

    parameterLayout.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { paramCrossoverActiveSplitCountId, 1 },
        "CROSSOVER / COUNT",
        0,
        static_cast<int>(ava::crossover::BufferRouter::numSplits),
        0,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    constexpr std::array<float, 5> splitFrequencyDefaults { 134.0f, 523.0f, 2093.0f, 5000.0f, 10000.0f };
    static_assert(splitFrequencyDefaults.size() == ava::crossover::BufferRouter::numSplits);

    for (size_t index = 0; index < splitFrequencyDefaults.size(); ++index)
    {
        const auto suffix = "split_" + juce::String(static_cast<int>(index + 1)) + "_frequency";
        parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { getCrossoverParameterId(suffix.toRawUTF8()), 1 },
            "CROSSOVER / SPLIT-" + juce::String(static_cast<int>(index + 1)),
            juce::NormalisableRange<float> { 20.0f, 20000.0f, 0.01f },
            splitFrequencyDefaults[index],
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    }

    for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
    {
        parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { getCrossoverSoloParameterId(rangeIndex), 1 },
            "CROSSOVER / SOLO / " + juce::String(static_cast<int>(rangeIndex + 1)),
            false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    }

    for (const auto& [suffix, label] : std::array {
             std::pair { "listen_lc", "LC" },
             std::pair { "listen_rc", "RC" },
             std::pair { "listen_mc", "MC" },
             std::pair { "listen_sc", "SC" },
             std::pair { "listen_ll", "LL" },
             std::pair { "listen_rr", "RR" },
             std::pair { "listen_ss", "SS" }
         })
    {
        parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { getCrossoverParameterId(suffix), 1 },
            "CROSSOVER / LISTEN / " + juce::String(label),
            false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    }

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { getCrossoverParameterId("auto_solo"), 1 },
        "CROSSOVER / AUTO-SOLO",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramGlobalBypassId, 1 },
        "AVA / GLOBAL / B",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    return { parameterLayout.begin(), parameterLayout.end() };
}

const juce::String AvaAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AvaAudioProcessor::acceptsMidi() const
{
    return false;
}

bool AvaAudioProcessor::producesMidi() const
{
    return false;
}

bool AvaAudioProcessor::isMidiEffect() const
{
    return false;
}

double AvaAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AvaAudioProcessor::getNumPrograms()
{
    return 1;
}

int AvaAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AvaAudioProcessor::setCurrentProgram(int)
{
}

const juce::String AvaAudioProcessor::getProgramName(int)
{
    return {};
}

void AvaAudioProcessor::changeProgramName(int, const juce::String&)
{
}
