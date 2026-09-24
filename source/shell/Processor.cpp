#include "Processor.h"
#include "../modules/eql/ProcessorBank.h"
#include "../modules/fft/ProcessorBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/fft/Processor.h"
#include "../modules/trs/Processor.h"
#include "../modules/eql/Processor.h"
#include "FilterOrderState.h"
#include "OscController.h"
#include "../routing/Runtime.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

namespace
{
juce::String formatOscValue(const float value)
{
    const auto rounded = std::round(value);
    if (std::abs(value - rounded) <= 1.0e-6f)
        return juce::String(static_cast<int>(rounded));

    return juce::String(value, 6).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}

juce::String formatOscRange(const float minimum,
                            const float maximum,
                            const bool discrete = false)
{
    auto values = formatOscValue(minimum)
        + " " + juce::String::charToString(0x2014) + " "
        + formatOscValue(maximum);

    if (discrete)
        values += ", d";

    return values;
}

juce::String getAcceptedOscValues(const juce::RangedAudioParameter& parameter)
{
    const auto& range = parameter.getNormalisableRange();
    if (dynamic_cast<const juce::AudioParameterBool*>(&parameter) != nullptr)
        return "0, 1";

    const auto choice = dynamic_cast<const juce::AudioParameterChoice*>(&parameter) != nullptr;
    const auto discrete = choice
        || dynamic_cast<const juce::AudioParameterInt*>(&parameter) != nullptr;
    return formatOscRange(range.start + (choice ? 1.0f : 0.0f),
                          range.end + (choice ? 1.0f : 0.0f),
                          discrete);
}

juce::String getAcceptedPresetValues(const juce::StringArray& presetNames)
{
    return presetNames.isEmpty()
        ? juce::String {}
        : formatOscRange(1.0f, static_cast<float>(presetNames.size()), true);
}

void appendOscParameter(std::vector<OscParameterInfo>& result,
                        const juce::AudioProcessorValueTreeState& state,
                        const juce::String& parameterId,
                        const juce::String& oscParameterId)
{
    const auto* parameter = state.getParameter(parameterId);
    if (parameter == nullptr
        || std::any_of(result.begin(), result.end(), [&oscParameterId] (const auto& existing)
        {
            return existing.internalName == oscParameterId;
        }))
        return;

    result.push_back({ oscParameterId,
                       getAcceptedOscValues(*parameter),
                       "/ava/" + oscParameterId });
}

void appendBandModuleParameter(std::vector<OscParameterInfo>& result,
                               const juce::AudioProcessorValueTreeState& state,
                               const int bandNumber,
                               const juce::String& suffix)
{
    const auto parameterId = "band-" + juce::String(bandNumber) + "_" + suffix;
    const auto* parameter = state.getParameter(parameterId);
    if (parameter == nullptr)
        return;

    appendOscParameter(result,
                       state,
                       parameterId,
                       parameterId);
}

void appendPrefixedBandModuleParameter(std::vector<OscParameterInfo>& result,
                                       const juce::AudioProcessorValueTreeState& state,
                                       const int bandNumber,
                                       const juce::String& parameterId)
{
    const auto* parameter = state.getParameter(parameterId);
    if (parameter == nullptr)
        return;

    appendOscParameter(result,
                       state,
                       parameterId,
                       "band-" + juce::String(bandNumber) + "_" + parameterId);
}
}

AvaAudioProcessor::AvaAudioProcessor(const bool routingInstanceIn)
    : AudioProcessor(BusesProperties()
                          .withInput("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    parameters(*this, nullptr, "ava_state", createParameterLayout()),
    routingInstance(routingInstanceIn)
{
    if (! routingInstance)
        oscController = std::make_unique<OscController>(*this);
    globalBypassParam = parameters.getRawParameterValue(paramGlobalBypassId);
    crossoverActiveSplitCountParam = parameters.getRawParameterValue(paramCrossoverActiveSplitCountId);

    for (size_t splitIndex = 0; splitIndex < crossoverSplitFrequencyParams.size(); ++splitIndex)
    {
        const auto suffix = "split-" + juce::String(static_cast<int>(splitIndex + 1));
        crossoverSplitFrequencyParams[splitIndex] = parameters.getRawParameterValue(getCrossoverParameterId(suffix.toRawUTF8()));
    }

    for (size_t rangeIndex = 0; rangeIndex < crossoverSoloParams.size(); ++rangeIndex)
        crossoverSoloParams[rangeIndex] = parameters.getRawParameterValue(getCrossoverSoloParameterId(rangeIndex));

    constexpr std::array<const char*, 7> globalListenSuffixes {
        "lc", "rc", "mc", "sc", "ll", "rr", "ss"
    };

    for (size_t listenIndex = 0; listenIndex < globalListenParams.size(); ++listenIndex)
        globalListenParams[listenIndex] = parameters.getRawParameterValue(getCrossoverParameterId(globalListenSuffixes[listenIndex]));

    parameters.addParameterListener(paramCrossoverActiveSplitCountId, this);

    for (int slotIndex = 0; slotIndex < hostAutomationSlotCount; ++slotIndex)
    {
        hostSlotParameterIds[static_cast<size_t>(slotIndex)] = getHostSlotParameterId(slotIndex);
        parameters.addParameterListener(hostSlotParameterIds[static_cast<size_t>(slotIndex)], this);
    }

    if (! routingInstance)
        routingRuntime = std::make_unique<ava::routing::Runtime>(*this);
}

AvaAudioProcessor::~AvaAudioProcessor()
{
    cancelPendingUpdate();
    oscController.reset();
    parameters.removeParameterListener(paramCrossoverActiveSplitCountId, this);

    for (const auto& parameterId : hostSlotParameterIds)
        parameters.removeParameterListener(parameterId, this);

    clearActiveModuleStateListeners();
}

std::shared_ptr<AvaAudioProcessor> AvaAudioProcessor::getRoutingInstanceHandle(const int instanceId) noexcept
{
    const juce::ScopedLock lock(processingLock);
    return routingRuntime == nullptr ? nullptr : routingRuntime->getInstanceHandle(instanceId);
}

void AvaAudioProcessor::synchronizeRouting()
{
    if (routingRuntime == nullptr)
        return;

    const auto nextTopology = ava::routing::readState(parameters.state);
    if (nextTopology.rootInstanceId != routingRuntime->getRootInstanceId())
    {
        if (const auto promoted = routingRuntime->getInstanceHandle(nextTopology.rootInstanceId))
        {
            juce::MemoryBlock processorState;
            promoted->getStateInformation(processorState);
            const auto xml = getXmlFromBinary(processorState.getData(),
                                               static_cast<int>(processorState.getSize()));
            if (xml != nullptr)
            {
                auto promotedState = juce::ValueTree::fromXml(*xml);
                auto currentState = parameters.copyState();
                routingRuntime->writeProcessorStates(currentState, nextTopology.rootInstanceId);
                for (const auto* key : { ava::routing::instancesStateKey,
                                         ava::routing::nextInstanceIdStateKey,
                                         ava::routing::rootInstanceIdStateKey,
                                         ava::routing::namesStateKey,
                                         ava::routing::processorStatesKey,
                                         editorWidthStateKey,
                                         editorHeightStateKey,
                                         editorRoutingExpandedStateKey,
                                         editorHostParametersExpandedStateKey,
                                         oscEnabledStateKey,
                                         oscInputPortStateKey,
                                         oscOutputHostStateKey,
                                         oscOutputPortStateKey })
                {
                    if (currentState.hasProperty(key))
                        promotedState.setProperty(key, currentState.getProperty(key), nullptr);
                    else
                        promotedState.removeProperty(key, nullptr);
                }
                if (const auto promotedXml = promotedState.createXml())
                {
                    juce::MemoryBlock mergedState;
                    copyXmlToBinary(*promotedXml, mergedState);
                    if (restoreStateInformation(mergedState.getData(),
                                                static_cast<int>(mergedState.getSize()), true))
                    {
                        notifyHostOfStateChange();
                        return;
                    }
                }
            }
        }
    }
    const juce::ScopedLock lock(processingLock);
    routingRuntime->synchronize(parameters.state);
    updateShellLatency();
    notifyHostOfStateChange();
}

OscSettings AvaAudioProcessor::getOscSettings() const
{
    if (routingOwner != nullptr)
        return routingOwner->getOscSettings();
    OscSettings settings;
    const auto& state = parameters.state;
    settings.enabled = state.hasProperty(oscEnabledStateKey)
        ? static_cast<bool>(state.getProperty(oscEnabledStateKey))
        : false;
    settings.inputPort = static_cast<int>(state.getProperty(oscInputPortStateKey,
                                                            OscSettings::defaultInputPort));
    settings.outputHost = state.getProperty(oscOutputHostStateKey, "127.0.0.1").toString();
    settings.outputPort = static_cast<int>(state.getProperty(oscOutputPortStateKey,
                                                             OscSettings::defaultOutputPort));

    return settings;
}

bool AvaAudioProcessor::setOscSettings(const OscSettings& settings)
{
    if (routingOwner != nullptr)
        return routingOwner->setOscSettings(settings);
    if (oscController == nullptr || ! oscController->applySettings(settings))
        return false;

    parameters.state.setProperty(oscEnabledStateKey, settings.enabled, nullptr);
    parameters.state.setProperty(oscInputPortStateKey, settings.inputPort, nullptr);
    parameters.state.setProperty(oscOutputHostStateKey, settings.outputHost.trim(), nullptr);
    parameters.state.setProperty(oscOutputPortStateKey, settings.outputPort, nullptr);
    parameters.state.removeProperty(oscInstanceNameStateKey, nullptr);
    notifyHostOfStateChange();
    return true;
}

void AvaAudioProcessor::refreshOscConfiguration()
{
    if (oscController == nullptr)
        return;

    OscSettings settings;
    const auto& state = parameters.state;
    settings.enabled = state.hasProperty(oscEnabledStateKey)
        && static_cast<bool>(state.getProperty(oscEnabledStateKey));
    settings.inputPort = static_cast<int>(state.getProperty(oscInputPortStateKey,
                                                            OscSettings::defaultInputPort));
    settings.outputHost = state.getProperty(oscOutputHostStateKey, "127.0.0.1").toString();
    settings.outputPort = static_cast<int>(state.getProperty(oscOutputPortStateKey,
                                                             OscSettings::defaultOutputPort));
    oscController->applySettingsAsync(settings);
}

bool AvaAudioProcessor::isOscInputPortBusy() const noexcept
{
    if (routingOwner != nullptr)
        return routingOwner->isOscInputPortBusy();
    return oscController != nullptr && oscController->isInputPortBusy();
}

std::vector<OscParameterInfo> AvaAudioProcessor::getVisibleOscParameters() const
{
    std::vector<OscParameterInfo> result;
    const auto activeRangeCount = static_cast<int>(getCrossoverSettings().activeSplitCount + 1);

    const auto appendGlobalControl = [&result] (const char* internalName,
                                                 const juce::String& acceptedValues)
    {
        result.push_back({ internalName,
                           acceptedValues,
                           "/ava/" + juce::String(internalName) });
    };

    appendGlobalControl(oscGlobalAbSlotAId, "1");
    appendGlobalControl(oscGlobalAbSwitchId, "1");
    appendGlobalControl(oscGlobalAbSlotBId, "1");
    appendGlobalControl(oscGlobalUndoId, "1");
    appendGlobalControl(oscGlobalRedoId, "1");
    appendGlobalControl(paramGlobalBypassId, "0, 1");

    appendGlobalControl(oscXovAddId, "1");

    for (int splitIndex = 0; splitIndex < activeRangeCount - 1; ++splitIndex)
    {
        const auto parameterId = getCrossoverParameterId(
            ("split-" + juce::String(splitIndex + 1)).toRawUTF8());
        const auto previousSize = result.size();
        appendOscParameter(result, parameters, parameterId, parameterId);

        if (result.size() > previousSize)
        {
            if (const auto* parameter = parameters.getParameter(parameterId))
            {
                constexpr auto minimumSplitGapHz = 1.0f;
                const auto& range = parameter->getNormalisableRange();
                auto minimum = range.start;
                auto maximum = range.end;

                if (splitIndex > 0)
                {
                    const auto previousId = getCrossoverParameterId(
                        ("split-" + juce::String(splitIndex)).toRawUTF8());
                    if (const auto* previous = parameters.getParameter(previousId))
                        minimum = juce::jmax(minimum,
                                             previous->convertFrom0to1(previous->getValue())
                                                 + minimumSplitGapHz);
                }

                if (splitIndex + 1 < activeRangeCount - 1)
                {
                    const auto nextId = getCrossoverParameterId(
                        ("split-" + juce::String(splitIndex + 2)).toRawUTF8());
                    if (const auto* next = parameters.getParameter(nextId))
                        maximum = juce::jmin(maximum,
                                             next->convertFrom0to1(next->getValue())
                                                 - minimumSplitGapHz);
                }

                result.back().acceptedValues = formatOscRange(minimum, maximum);
            }
        }
    }

    appendGlobalControl(oscXovDelId, "1");

    const auto autoSoloId = getCrossoverParameterId("auto-solo");
    appendOscParameter(result, parameters, autoSoloId, autoSoloId);
    appendGlobalControl(oscSoloModeId, "0, 1");

    constexpr std::array<const char*, 7> orderedListenSuffixes {
        "lc", "rc", "mc", "sc", "ll", "rr", "ss"
    };
    for (const auto* suffix : orderedListenSuffixes)
    {
        const auto parameterId = getCrossoverParameterId(suffix);
        appendOscParameter(result, parameters, parameterId, parameterId);
    }

    if (getActiveModule() == ActiveModule::none)
        appendGlobalControl(oscAddModuleId, formatOscRange(1.0f, 5.0f, true));
    else
        appendGlobalControl(oscCloseModuleId, "1");

    for (int bandIndex = 0; bandIndex < activeRangeCount; ++bandIndex)
    {
        const auto bandNumber = bandIndex + 1;
        const auto soloParameterId = getCrossoverSoloParameterId(static_cast<size_t>(bandIndex));
        if (activeRangeCount > 1)
            appendOscParameter(result, parameters, soloParameterId, soloParameterId);

        switch (getActiveModule())
        {
            case ActiveModule::eql:
                if (const auto* bank = getEqlProcessorBank())
                {
                    const auto* module = bank->getProcessor(static_cast<size_t>(bandIndex));
                    if (module != nullptr)
                    {
                        appendGlobalControl(getEqlAddActionId(static_cast<size_t>(bandIndex)).toRawUTF8(), "1");
                        appendGlobalControl(getEqlDeleteAllActionId(static_cast<size_t>(bandIndex)).toRawUTF8(),
                                            "1");

                        const auto activeFilterCount = module->getActiveFilterCount();
                        auto displayOrder = shell_filter_order_state::makeIdentity(EqlModuleProcessor::maxFilterCount);
                        const auto orderKey = getEditorFilterDisplayOrderStateKey(static_cast<size_t>(bandIndex));

                        if (const auto restored = shell_filter_order_state::decode(
                                parameters.state.getProperty(orderKey).toString(),
                                activeFilterCount,
                                EqlModuleProcessor::maxFilterCount))
                            displayOrder = *restored;

                        for (int displayIndex = 0; displayIndex < activeFilterCount; ++displayIndex)
                        {
                            const auto filterIndex = displayOrder[static_cast<size_t>(displayIndex)];
                            const std::array<juce::String, 7> parameterOrder {
                                EqlModuleProcessor::getFilterBypassParamId(filterIndex),
                                EqlModuleProcessor::getFilterTypeParamId(filterIndex),
                                EqlModuleProcessor::getFilterPlaceParamId(filterIndex),
                                EqlModuleProcessor::getFilterOrderParamId(filterIndex),
                                EqlModuleProcessor::getFilterFrequencyParamId(filterIndex),
                                EqlModuleProcessor::getFilterBandwidthParamId(filterIndex),
                                EqlModuleProcessor::getFilterGainParamId(filterIndex)
                            };

                            for (const auto& parameterId : parameterOrder)
                            {
                                const auto previousSize = result.size();
                                appendPrefixedBandModuleParameter(result,
                                                                  module->getValueTreeState(),
                                                                  bandNumber,
                                                                  parameterId);

                                if (parameterId == EqlModuleProcessor::getFilterBypassParamId(filterIndex)
                                    && result.size() > previousSize)
                                {
                                    appendGlobalControl(getEqlFilterDeleteActionId(
                                                            static_cast<size_t>(bandIndex), filterIndex).toRawUTF8(),
                                        "1");
                                }
                            }
                        }

                        const auto presetNames = module->getFilterPresetNames();
                        appendGlobalControl(getEqlPresetSelectionId(static_cast<size_t>(bandIndex)).toRawUTF8(),
                                            getAcceptedPresetValues(presetNames));
                    }
                }
                break;
            case ActiveModule::fft:
                if (const auto* bank = getFftProcessorBank())
                {
                    const auto* module = bank->getProcessor(static_cast<size_t>(bandIndex));
                    if (module != nullptr)
                    {
                        const auto correlationMode = module->isCorrelationMode();
                        std::vector<const char*> parameterOrder {
                            FftModuleProcessor::paramDspFftSizeId,
                            FftModuleProcessor::paramDspOverlapId,
                            FftModuleProcessor::paramDynamicModeId
                        };

                        if (correlationMode)
                        {
                            parameterOrder.insert(parameterOrder.end(), {
                                FftModuleProcessor::paramCorrelationTypeId,
                                FftModuleProcessor::paramDynamicDirectionId,
                                FftModuleProcessor::paramCorrelationSmoothingId,
                                FftModuleProcessor::paramAttackId,
                                FftModuleProcessor::paramReleaseId,
                                FftModuleProcessor::paramKneeId,
                                FftModuleProcessor::paramRatioId,
                                FftModuleProcessor::paramCorrelationSlopeId,
                                FftModuleProcessor::paramCorrelationThresholdId,
                                FftModuleProcessor::paramCorrelationAdaptiveId,
                                FftModuleProcessor::paramFloorId,
                                FftModuleProcessor::paramCorrelationImpactId,
                                FftModuleProcessor::paramCorrelationAdaptiveOffsetId,
                                FftModuleProcessor::paramAdaptiveAttackId,
                                FftModuleProcessor::paramAdaptiveHoldId,
                                FftModuleProcessor::paramAdaptiveReleaseId
                            });
                        }
                        else
                        {
                            parameterOrder.insert(parameterOrder.end(), {
                                FftModuleProcessor::paramDynamicDirectionId,
                                FftModuleProcessor::paramAttackId,
                                FftModuleProcessor::paramReleaseId,
                                FftModuleProcessor::paramKneeId,
                                FftModuleProcessor::paramRatioId,
                                FftModuleProcessor::paramDspSlopeId,
                                FftModuleProcessor::paramDualMonoLeftThresholdId,
                                FftModuleProcessor::paramDualMonoLeftAdaptiveId,
                                FftModuleProcessor::paramDualMonoRightThresholdId,
                                FftModuleProcessor::paramDualMonoRightAdaptiveId,
                                FftModuleProcessor::paramDualMonoLinkId,
                                FftModuleProcessor::paramSpectralAdaptiveOffsetId,
                                FftModuleProcessor::paramAdaptiveAttackId,
                                FftModuleProcessor::paramAdaptiveHoldId,
                                FftModuleProcessor::paramAdaptiveReleaseId
                            });
                        }

                        parameterOrder.insert(parameterOrder.end(), {
                            FftModuleProcessor::paramDetectorLowCutId,
                            FftModuleProcessor::paramDetectorHighCutId,
                            FftModuleProcessor::paramDeltaId
                        });

                        for (const auto* parameterId : parameterOrder)
                            appendPrefixedBandModuleParameter(result,
                                                              module->getValueTreeState(),
                                                              bandNumber,
                                                              parameterId);
                    }
                }
                break;
            case ActiveModule::tls:
                if (const auto* module = getTlsModuleProcessor())
                {
                    constexpr std::array<const char*, 9> leadingParameterOrder {
                        "lc", "rc", "mc", "sc",
                        "ll", "rr", "ss",
                        "stereo.gain", "stereo.mute.icon"
                    };
                    constexpr std::array<const char*, 19> trailingParameterOrder {
                        "stereo.delay", "left.delay", "right.delay",
                        "stereo.phase", "left.phase", "right.phase",
                        "left.pan", "right.pan", "law",
                        "impact", "direction",
                        "mid.balance", "side.balance",
                        "degree", "flip-right",
                        "hpos", "hneg",
                        "fpos", "fneg"
                    };

                    for (const auto* suffix : leadingParameterOrder)
                    {
                        appendBandModuleParameter(result,
                                                  module->getValueTreeState(),
                                                  bandNumber,
                                                  suffix);
                    }

                    struct GainRow
                    {
                        const char* channel;
                        float order;
                    };
                    std::array<GainRow, 4> gainRows {{
                        { "left", 2.0f }, { "right", 3.0f }, { "mid", 4.0f }, { "side", 5.0f }
                    }};
                    const auto& tlsState = module->getValueTreeState();
                    for (auto& row : gainRows)
                    {
                        const auto orderId = "band-" + juce::String(bandNumber) + "_" + row.channel + "-order";
                        if (const auto* parameter = tlsState.getParameter(orderId))
                            row.order = parameter->convertFrom0to1(parameter->getValue());
                    }
                    std::stable_sort(gainRows.begin(), gainRows.end(), [] (const auto& left, const auto& right)
                    {
                        return left.order < right.order;
                    });

                    for (const auto& row : gainRows)
                    {
                        const auto channel = juce::String(row.channel);
                        appendBandModuleParameter(result, tlsState, bandNumber, channel + ".gain");
                        appendBandModuleParameter(result, tlsState, bandNumber, channel + ".mute.icon");
                    }

                    for (const auto* suffix : trailingParameterOrder)
                        appendBandModuleParameter(result, tlsState, bandNumber, suffix);
                }
                break;
            case ActiveModule::dyn:
                if (const auto* module = getDynModuleProcessor())
                {
                    constexpr std::array<const char*, 37> parameterOrder {
                        "morph", "ratio", "knee", "peak-hold", "lookahead",
                        "tension-floor", "tension-hysteresis", "release-form", "release-curve",
                        "up-dn", "l-r", "opposite",
                        "l-up-threshold", "l-up-adaptive", "l-up-tension",
                        "l-up-release", "l-up-output",
                        "l-dn-threshold", "l-dn-adaptive", "l-dn-tension",
                        "l-dn-release", "l-dn-output",
                        "r-up-threshold", "r-up-adaptive", "r-up-tension",
                        "r-up-release", "r-up-output",
                        "r-dn-threshold", "r-dn-adaptive", "r-dn-tension",
                        "r-dn-release", "r-dn-output",
                        "offset", "attack", "hold", "release", "delta"
                    };

                    for (const auto* suffix : parameterOrder)
                        appendBandModuleParameter(result,
                                                  module->getValueTreeState(),
                                                  bandNumber,
                                                  suffix);
                }
                break;
            case ActiveModule::trs:
                if (const auto* module = getTrsModuleProcessor())
                {
                    const auto& trsState = module->getValueTreeState();
                    const auto appendTrsParameter = [&] (const char* suffix)
                    {
                        appendBandModuleParameter(result, trsState, bandNumber, suffix);
                    };
                    const auto getTrsChoiceIndex = [&] (const char* suffix)
                    {
                        const auto parameterId = "band-" + juce::String(bandNumber) + "_" + suffix;
                        if (const auto* parameter = trsState.getParameter(parameterId))
                            return juce::roundToInt(parameter->convertFrom0to1(parameter->getValue()));

                        return 0;
                    };

                    appendTrsParameter(TrsModuleProcessor::paramTransientGainId);
                    appendTrsParameter(TrsModuleProcessor::paramTransientMuteId);
                    appendTrsParameter(TrsModuleProcessor::paramSustainGainId);
                    appendTrsParameter(TrsModuleProcessor::paramSustainMuteId);

                    appendTrsParameter(TrsModuleProcessor::paramHoldId);
                    appendTrsParameter(TrsModuleProcessor::paramHoldTypeId);
                    if (getTrsChoiceIndex(TrsModuleProcessor::paramHoldTypeId) > 0)
                        appendTrsParameter(TrsModuleProcessor::paramHoldSyncId);

                    appendTrsParameter(TrsModuleProcessor::paramReleaseId);
                    appendTrsParameter(TrsModuleProcessor::paramReleaseTypeId);
                    if (getTrsChoiceIndex(TrsModuleProcessor::paramReleaseTypeId) > 0)
                        appendTrsParameter(TrsModuleProcessor::paramReleaseSyncId);

                    constexpr std::array<const char*, 6> trailingParameterOrder {
                        TrsModuleProcessor::paramReleaseCurveId,
                        TrsModuleProcessor::paramLookaheadId,
                        TrsModuleProcessor::paramThresholdId,
                        TrsModuleProcessor::paramKneeId,
                        TrsModuleProcessor::paramRetriggerId,
                        TrsModuleProcessor::paramOneShotId
                    };
                    for (const auto* suffix : trailingParameterOrder)
                        appendTrsParameter(suffix);
                }
                break;
            case ActiveModule::none:
                break;
        }
    }

    return result;
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
    return "ava.editor.eql.band_" + juce::String(static_cast<int>(clampedRange)) + ".filter_display_order";
}

juce::String AvaAudioProcessor::getCrossoverParameterId(const char* suffix)
{
    return suffix;
}

juce::String AvaAudioProcessor::getCrossoverSoloParameterId(const size_t rangeIndex)
{
    return "band-" + juce::String(static_cast<int>(rangeIndex + 1)) + "_solo";
}

juce::String AvaAudioProcessor::getEqlAddActionId(const size_t rangeIndex)
{
    return "band-" + juce::String(static_cast<int>(rangeIndex + 1)) + "_add";
}

juce::String AvaAudioProcessor::getEqlDeleteAllActionId(const size_t rangeIndex)
{
    return "band-" + juce::String(static_cast<int>(rangeIndex + 1)) + "_delete-all.hidden";
}

juce::String AvaAudioProcessor::getEqlFilterDeleteActionId(const size_t rangeIndex,
                                                            const int filterIndex)
{
    return "band-" + juce::String(static_cast<int>(rangeIndex + 1))
        + "_filter-" + juce::String(filterIndex + 1) + "_delete.hidden";
}

juce::String AvaAudioProcessor::getEqlPresetSelectionId(const size_t rangeIndex)
{
    return "band-" + juce::String(static_cast<int>(rangeIndex + 1)) + "_preset";
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
        const auto suffix = "split-" + juce::String(static_cast<int>(index + 1));
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
            "BAND " + juce::String(static_cast<int>(rangeIndex + 1)) + " / SOLO",
            false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    }

    for (const auto& [suffix, label] : std::array {
             std::pair { "lc", "LC" },
             std::pair { "rc", "RC" },
             std::pair { "mc", "MC" },
             std::pair { "sc", "SC" },
             std::pair { "ll", "LL" },
             std::pair { "rr", "RR" },
             std::pair { "ss", "SS" }
         })
    {
        parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { getCrossoverParameterId(suffix), 1 },
            "LISTEN / " + juce::String(label),
            false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    }

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { getCrossoverParameterId("auto-solo"), 1 },
        "CROSSOVER / AUTO-SOLO",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramGlobalBypassId, 1 },
        "AVA / GLOBAL / BP",
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
