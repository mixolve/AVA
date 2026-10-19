#include "Processor.h"
#include "EditorStateSchema.h"
#include "EditorPreferences.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"
#include "FilterOrderState.h"
#include "OscController.h"
#include "../routing/Runtime.h"
#include "../crossover/ModuleBindings.h"
#include "../modules/eql/FilterSupport.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
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

    // Keep the shortest decimal text that still represents the same float.
    for (int decimalPlaces = 1; decimalPlaces <= 6; ++decimalPlaces)
    {
        const auto text = juce::String(value, decimalPlaces);
        if (std::bit_cast<std::uint32_t>(text.getFloatValue()) == std::bit_cast<std::uint32_t>(value))
            return text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    }

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

    if (const auto* floatParameter = dynamic_cast<const juce::AudioParameterFloat*>(&parameter);
        floatParameter != nullptr && floatParameter->paramID.endsWith("_degree"))
        return parameter.getText(0.0f, 0)
            + " " + juce::String::charToString(0x2014) + " "
            + parameter.getText(1.0f, 0);

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
            return existing.sourceId == oscParameterId;
        }))
        return;

    result.push_back({ getOscDisplayName(oscParameterId),
                       getAcceptedOscValues(*parameter),
                       "/ava/" + getOscAddressName(oscParameterId),
                       oscParameterId });
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

void appendRangeControlParameters(std::vector<OscParameterInfo>& result,
                                  const juce::AudioProcessorValueTreeState& state,
                                  const int bandNumber,
                                  const std::vector<CrossoverModuleComponent::CrossoverControlSpec>& controls,
                                  const juce::String& moduleKey)
{
    using Kind = CrossoverModuleComponent::ControlKind;
    for (const auto& control : controls)
    {
        if (! crossover_configs::isRangeControlEnabled(control, state, static_cast<size_t>(bandNumber - 1), moduleKey)
            || control.kind == Kind::heading
            || control.kind == Kind::inactive || control.kind == Kind::readout)
            continue;
        const auto previousSize = result.size();
        if (control.kind == Kind::time)
        {
            const auto modeId = "band-" + juce::String(bandNumber) + "_" + control.modeSuffix;
            const auto* mode = state.getRawParameterValue(modeId);
            appendBandModuleParameter(result, state, bandNumber,
                mode != nullptr && mode->load() >= 0.5f ? control.syncSuffix : control.suffix);
        }
        else
            appendBandModuleParameter(result, state, bandNumber, control.suffix);
        if (result.size() > previousSize)
        {
            auto& info = result.back();
            const auto name = "band-" + juce::String(bandNumber) + "_"
                + juce::String(control.label).toLowerCase().replaceCharacter('/', '-').replaceCharacter(' ', '-');
            info.address = "/ava/" + name;
            info.name = name.toUpperCase();
        }
        if (control.kind == Kind::time && control.showTimeModeButton)
            appendBandModuleParameter(result, state, bandNumber, control.modeSuffix);
        if (juce::String(control.auxiliaryChoiceSuffix).isNotEmpty())
            appendBandModuleParameter(result, state, bandNumber, control.auxiliaryChoiceSuffix);
        if (juce::String(control.auxiliaryToggleSuffix).isNotEmpty())
            appendBandModuleParameter(result, state, bandNumber, control.auxiliaryToggleSuffix);
    }
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
    : AudioProcessor([]
      {
          auto buses = BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
          for (int channel = 4; channel < routingChannelCount; channel += 2)
              buses = buses.withInput("Input " + juce::String(channel + 1) + "-" + juce::String(channel + 2),
                                      juce::AudioChannelSet::stereo(), false);
          return buses.withOutput("Output", juce::AudioChannelSet::stereo(), true);
      }()),
    parameters(*this, nullptr, "ava_state", createParameterLayout()),
    routingInstance(routingInstanceIn)
{
    if (! routingInstance)
        oscController = std::make_unique<OscController>(*this);
    globalBypassParam = parameters.getRawParameterValue(paramGlobalBypassId);
    processingInputParam = parameters.getRawParameterValue(paramProcessingInputId);
    soloInstanceParam = parameters.getRawParameterValue(paramSoloInstanceId);
    soloChannelParam = parameters.getRawParameterValue(paramSoloChannelId);
    crossoverActiveSplitCountParam = parameters.getRawParameterValue(paramCrossoverActiveSplitCountId);

    for (size_t splitIndex = 0; splitIndex < crossoverSplitFrequencyParams.size(); ++splitIndex)
    {
        const auto suffix = "split-" + juce::String(static_cast<int>(splitIndex + 1));
        crossoverSplitFrequencyParams[splitIndex] = parameters.getRawParameterValue(getCrossoverParameterId(suffix.toRawUTF8()));
    }

    for (size_t rangeIndex = 0; rangeIndex < crossoverSoloParams.size(); ++rangeIndex)
        crossoverSoloParams[rangeIndex] = parameters.getRawParameterValue(getCrossoverSoloParameterId(rangeIndex));

    parameters.addParameterListener(paramCrossoverActiveSplitCountId, this);
    parameters.addParameterListener(paramGlobalBypassId, this);
    parameters.addParameterListener(paramSoloInstanceId, this);
    parameters.addParameterListener(paramSoloChannelId, this);

    for (int slotIndex = 0; slotIndex < hostAutomationSlotCount; ++slotIndex)
    {
        hostSlotParameterIds[static_cast<size_t>(slotIndex)] = getHostSlotParameterId(slotIndex);
        parameters.addParameterListener(hostSlotParameterIds[static_cast<size_t>(slotIndex)], this);
    }

    if (! routingInstance)
        routingRuntime = std::make_unique<ava::routing::Runtime>(*this);
}

juce::VST3ClientExtensions* AvaAudioProcessor::getVST3ClientExtensions() { return &hostTrackContext; }
void AvaAudioProcessor::updateTrackProperties(const TrackProperties& properties)
{
    hostTrackContext.updateTrackProperties(properties);
}
HostTrackContext::Location AvaAudioProcessor::refreshHostTrackName()
{
    return getOscOwner().hostTrackContext.refresh();
}

AvaAudioProcessor::~AvaAudioProcessor()
{
    cancelPendingUpdate();
    oscController.reset();
    parameters.removeParameterListener(paramCrossoverActiveSplitCountId, this);
    parameters.removeParameterListener(paramGlobalBypassId, this);
    parameters.removeParameterListener(paramSoloInstanceId, this);
    parameters.removeParameterListener(paramSoloChannelId, this);

    for (const auto& parameterId : hostSlotParameterIds)
        parameters.removeParameterListener(parameterId, this);

    clearActiveModuleStateListeners();
}

std::shared_ptr<AvaAudioProcessor> AvaAudioProcessor::getRoutingInstanceHandle(const int instanceId) noexcept
{
    const juce::ScopedLock lock(processingLock);
    return routingRuntime == nullptr ? nullptr : routingRuntime->getInstanceHandle(instanceId);
}

bool AvaAudioProcessor::isInstanceSoloBypassed() const
{
    const auto& owner = getOscOwner();
    const juce::ScopedLock lock(owner.processingLock);
    const auto* solo = owner.routingRuntime != nullptr ? owner.routingRuntime->getSoloProcessor(false) : nullptr;
    return solo != nullptr && solo != this;
}

void AvaAudioProcessor::synchronizeRouting()
{
    if (routingRuntime == nullptr)
        return;

    auto nextTopology = ava::routing::readState(parameters.state);
    if (! routingRuntime->hasMultipleInstances() && ava::routing::getInstanceCount(nextTopology) > 1
        && nextTopology.rootInstanceId == routingRuntime->getRootInstanceId())
    {
        if (auto* bypass = parameters.getParameter(paramGlobalBypassId); bypass != nullptr && bypass->getValue() >= 0.5f)
        {
            parameters.state.setProperty(ava::routing::rootBypassStateKey, true, nullptr);
            nextTopology.rootBypassed = true;
            bypass->setValueNotifyingHost(0.0f);
        }
    }
    if (ava::routing::getInstanceCount(nextTopology) == 1 && nextTopology.rootBypassed)
    {
        if (auto* bypass = parameters.getParameter(paramGlobalBypassId))
            bypass->setValueNotifyingHost(1.0f);
        parameters.state.removeProperty(ava::routing::rootBypassStateKey, nullptr);
        nextTopology.rootBypassed = false;
    }
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
                                         ava::routing::rootBypassStateKey,
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
                for (const auto* name : { "osc.expanded" })
                {
                    const auto key = shell_editor_ui::key(name);
                    if (currentState.hasProperty(key)) promotedState.setProperty(key, currentState.getProperty(key), nullptr);
                    else promotedState.removeProperty(key, nullptr);
                }
                if (const auto promotedXml = promotedState.createXml())
                {
                    preserveEditorWindowState(*promotedXml, currentState);
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
        result.push_back({ getOscDisplayName(internalName),
                           acceptedValues,
                           "/ava/" + getOscAddressName(internalName),
                           internalName });
    };

    appendGlobalControl(paramGlobalBypassId, "0, 1");

    appendOscParameter(result, parameters, paramProcessingInputId, paramProcessingInputId);
    appendOscParameter(result, parameters, paramSoloInstanceId, paramSoloInstanceId);
    appendOscParameter(result, parameters, paramSoloChannelId, paramSoloChannelId);
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

    if (getActiveModule() == ActiveModule::none)
        appendGlobalControl(oscAddModuleId, formatOscRange(1.0f, static_cast<float>(availableModules.size()), true));
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
                        const auto presetNames = module->getFilterPresetNames();
                        appendGlobalControl(getEqlPresetSelectionId(static_cast<size_t>(bandIndex)).toRawUTF8(),
                                            getAcceptedPresetValues(presetNames));

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
                            const auto& filterState = module->getValueTreeState();
                            const auto readChoice = [&filterState] (const juce::String& id)
                            {
                                const auto* value = filterState.getRawParameterValue(id);
                                return value != nullptr ? juce::roundToInt(value->load()) : 0;
                            };
                            const auto availability = getFilterControlAvailability(
                                EqlModuleProcessor::filterTypeFromChoiceIndex(readChoice(EqlModuleProcessor::getFilterTypeParamId(filterIndex))),
                                readChoice(EqlModuleProcessor::getFilterOrderParamId(filterIndex)));
                            const std::array<juce::String, 7> parameterOrder {
                                EqlModuleProcessor::getFilterBypassParamId(filterIndex),
                                EqlModuleProcessor::getFilterTypeParamId(filterIndex),
                                availability.phase ? EqlModuleProcessor::getFilterPhaseParamId(filterIndex) : juce::String {},
                                availability.order ? EqlModuleProcessor::getFilterOrderParamId(filterIndex) : juce::String {},
                                availability.frequency ? EqlModuleProcessor::getFilterFrequencyParamId(filterIndex) : juce::String {},
                                availability.bandwidth ? EqlModuleProcessor::getFilterBandwidthParamId(filterIndex) : juce::String {},
                                availability.gain ? EqlModuleProcessor::getFilterGainParamId(filterIndex) : juce::String {}
                            };

                            for (const auto& parameterId : parameterOrder)
                            {
                                if (parameterId.isEmpty()) continue;
                                const auto previousSize = result.size();
                                appendPrefixedBandModuleParameter(result,
                                                                  module->getValueTreeState(),
                                                                  bandNumber,
                                                                  parameterId);

                                if (result.size() > previousSize)
                                {
                                    auto& info = result.back();
                                    if (parameterId == EqlModuleProcessor::getFilterFrequencyParamId(filterIndex))
                                    {
                                        const auto type = EqlModuleProcessor::filterTypeFromChoiceIndex(
                                            readChoice(EqlModuleProcessor::getFilterTypeParamId(filterIndex)));
                                        const auto maximum = type == EqlModuleProcessor::FilterType::lowCut
                                            ? maximumLowCutFrequency : maximumVisibleFilterFrequency;
                                        info.parameterRange = juce::Range<float>(minimumVisibleFilterFrequency, maximum);
                                        info.acceptedValues = formatOscRange(minimumVisibleFilterFrequency, maximum);
                                    }
                                    if (parameterId == EqlModuleProcessor::getFilterGainParamId(filterIndex)
                                        && availability.phase && readChoice(EqlModuleProcessor::getFilterPhaseParamId(filterIndex)) != 0)
                                    {
                                        info.valueScale = static_cast<float>(filterPhaseDegreesPerDb);
                                        info.parameterRange = juce::Range<float>(-24.0f, 24.0f);
                                        info.acceptedValues = formatOscRange(-180.0f, 180.0f);
                                        const auto addressName = getOscAddressName(info.sourceId).upToLastOccurrenceOf("_", true, false) + "deg";
                                        info.address = "/ava/" + addressName;
                                        info.name = addressName.toUpperCase();
                                    }
                                }

                                if (parameterId == EqlModuleProcessor::getFilterBypassParamId(filterIndex)
                                    && result.size() > previousSize)
                                {
                                    appendGlobalControl(getEqlFilterDeleteActionId(
                                                            static_cast<size_t>(bandIndex), filterIndex).toRawUTF8(),
                                        "1");
                                }
                            }
                        }

                    }
                }
                break;
            case ActiveModule::tls:
                if (const auto* module = getTlsModuleProcessor())
                    appendRangeControlParameters(result, module->getValueTreeState(), bandNumber,
                                                 crossover_configs::makeTlsRangeControls(), "tls");
                break;
            case ActiveModule::dyn:
                if (const auto* module = getDynModuleProcessor())
                    appendRangeControlParameters(result, module->getValueTreeState(), bandNumber,
                        crossover_configs::makeDynRangeControls(*module, static_cast<size_t>(bandIndex)), "dyn");
                break;
            case ActiveModule::none:
                break;
        }
    }

    appendGlobalControl(oscGlobalAbSlotAId, "1");
    appendGlobalControl(oscGlobalAbSwitchId, "1");
    appendGlobalControl(oscGlobalAbSlotBId, "1");
    appendGlobalControl(oscGlobalUndoId, "1");
    appendGlobalControl(oscGlobalRedoId, "1");

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
        case ActiveModule::tls: return findInModule(getTlsModuleProcessor());
        case ActiveModule::dyn: return findInModule(getDynModuleProcessor());
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

    parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { paramProcessingInputId, 1 }, "CROSSOVER / CH-IN",
        getProcessingInputChoices(), 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto* id : { paramSoloInstanceId, paramSoloChannelId })
        parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, "CROSSOVER / " + juce::String(id).toUpperCase(), false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

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
