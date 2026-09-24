#include "OscController.h"
#include "Processor.h"
#include "Editor.h"

#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/ProcessorBank.h"
#include "../modules/fft/Processor.h"
#include "../modules/fft/ProcessorBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/trs/Processor.h"
#include "../crossover/ParameterIds.h"
#include "../crossover/UiState.h"
#include "../routing/State.h"

#include <cmath>

namespace
{
constexpr auto outputRefreshRateHz = 30;
constexpr auto parameterChangeTolerance = 1.0e-6f;
constexpr std::array<const char*, 7> listenSuffixes {
    "lc", "rc", "mc", "sc", "ll", "rr", "ss"
};

bool isValidPort(const int port) noexcept
{
    return port >= 1 && port <= 65535;
}

bool isPortAvailable(const int port)
{
    juce::DatagramSocket socket;
    return socket.bindToPort(port);
}

bool parseBandParameterId(const juce::String& parameterId,
                          size_t& bandIndex,
                          juce::String& unprefixedParameterId)
{
    if (! parameterId.startsWith("band-"))
        return false;

    const auto separatorIndex = parameterId.indexOfChar('_');
    if (separatorIndex <= 5)
        return false;

    const auto bandNumberText = parameterId.substring(5, separatorIndex);
    if (! bandNumberText.containsOnly("0123456789"))
        return false;

    const auto bandNumber = bandNumberText.getIntValue();
    if (bandNumber < 1 || bandNumber > static_cast<int>(ava::crossover::BufferRouter::numRanges))
        return false;

    bandIndex = static_cast<size_t>(bandNumber - 1);
    unprefixedParameterId = parameterId.substring(separatorIndex + 1);
    return unprefixedParameterId.isNotEmpty();
}

int parseEqlFilterDeleteActionIndex(const juce::String& action)
{
    constexpr auto prefix = "filter-";
    constexpr auto suffix = "_delete.hidden";

    if (! action.startsWith(prefix) || ! action.endsWith(suffix))
        return -1;

    const auto numberText = action.substring(juce::String(prefix).length(),
                                             action.length() - juce::String(suffix).length());
    if (numberText.isEmpty() || ! numberText.containsOnly("0123456789"))
        return -1;

    return numberText.getIntValue() - 1;
}

int getCrossoverSoloBandIndex(const juce::String& parameterId)
{
    for (size_t bandIndex = 0; bandIndex < ava::crossover::BufferRouter::numRanges; ++bandIndex)
        if (parameterId == AvaAudioProcessor::getCrossoverSoloParameterId(bandIndex))
            return static_cast<int>(bandIndex);

    return -1;
}

int getCrossoverSplitIndex(const juce::String& parameterId)
{
    for (size_t splitIndex = 0; splitIndex < ava::crossover::BufferRouter::numSplits; ++splitIndex)
    {
        const auto suffix = "split-" + juce::String(static_cast<int>(splitIndex + 1));
        if (parameterId == AvaAudioProcessor::getCrossoverParameterId(suffix.toRawUTF8()))
            return static_cast<int>(splitIndex);
    }

    return -1;
}
}

OscController::OscController(AvaAudioProcessor& owner)
    : processor(owner), currentTarget(&owner)
{
    receiver.addListener(this);
}

OscController::~OscController()
{
    cancelPendingUpdate();
    stopTimer();
    receiver.removeListener(this);
    receiver.disconnect();
    sender.disconnect();
}

bool OscController::isInputPortBusy() const noexcept
{
    if (currentSettings.enabled && inputConnected)
        return false;

    const auto now = juce::Time::getMillisecondCounter();
    const auto previousProbe = lastInputPortProbeTimeMs.load(std::memory_order_relaxed);

    if (now - previousProbe >= 1000)
    {
        lastInputPortProbeTimeMs.store(now, std::memory_order_relaxed);
        inputPortBusy.store(! isPortAvailable(currentSettings.inputPort), std::memory_order_relaxed);
    }

    return inputPortBusy.load(std::memory_order_relaxed);
}

bool OscController::applySettings(const OscSettings& settings)
{
    const auto desiredHost = settings.outputHost.trim();

    if (! isValidPort(settings.inputPort)
        || ! isValidPort(settings.outputPort)
        || desiredHost.isEmpty())
        return false;

    auto desired = settings;
    desired.outputHost = desiredHost;
    const auto previousSettings = currentSettings;
    const auto previousInputConnected = inputConnected;
    const auto previousOutputConnected = outputConnected;

    if (inputConnected)
        receiver.disconnect();
    if (outputConnected)
        sender.disconnect();

    inputConnected = desired.enabled && receiver.connect(desired.inputPort);
    outputConnected = desired.enabled && sender.connect(desired.outputHost, desired.outputPort);
    inputPortBusy.store(desired.enabled ? ! inputConnected
                                       : ! isPortAvailable(desired.inputPort),
                        std::memory_order_relaxed);

    const auto inputSucceeded = ! desired.enabled || inputConnected;
    const auto outputSucceeded = ! desired.enabled || outputConnected;

    if (! inputSucceeded || ! outputSucceeded)
    {
        if (inputConnected)
            receiver.disconnect();
        if (outputConnected)
            sender.disconnect();

        inputConnected = previousInputConnected
            && receiver.connect(previousSettings.inputPort);
        outputConnected = previousOutputConnected
            && sender.connect(previousSettings.outputHost, previousSettings.outputPort);
        currentSettings = previousSettings;

        if (outputConnected)
            startTimerHz(outputRefreshRateHz);
        else
            stopTimer();

        return false;
    }

    currentSettings = desired;
    lastSentParameterValues.clear();
    lastSentModules.clear();
    lastSentSplitCounts.clear();

    if (outputConnected)
    {
        startTimerHz(outputRefreshRateHz);
        sendCurrentState();
    }
    else
    {
        stopTimer();
    }

    return true;
}

void OscController::applySettingsAsync(const OscSettings& settings)
{
    {
        const juce::ScopedLock lock(pendingSettingsLock);
        pendingSettings = settings;
    }

    triggerAsyncUpdate();
}

void OscController::handleAsyncUpdate()
{
    OscSettings settings;

    {
        const juce::ScopedLock lock(pendingSettingsLock);
        settings = pendingSettings;
    }

    applySettings(settings);
}

void OscController::oscMessageReceived(const juce::OSCMessage& message)
{
    const auto address = message.getAddressPattern().toString();
    const auto routing = ava::routing::readState(processor.getValueTreeState().state);
    for (const auto& node : routing.nodes)
    {
        const auto name = ava::routing::getDisplayName(routing, node.id);
        const auto prefix = makeOscAddressPrefix(name);
        if (! address.startsWith(prefix))
            continue;

        auto handle = node.id == routing.rootInstanceId
            ? std::shared_ptr<AvaAudioProcessor> {}
            : processor.getRoutingInstanceHandle(node.id);
        if (node.id != routing.rootInstanceId && handle == nullptr)
            return;

        currentTarget = handle != nullptr ? handle.get() : &processor;
        currentInstanceName = name;
        const auto parameterId = address.substring(prefix.length());
        if (parameterId == "request")
        {
            lastSentParameterValues.clear();
            lastSentModules.clear();
            lastSentSplitCounts.clear();
            sendInstanceState();
        }
        else if (parameterId == "module" && message.size() >= 1)
            applyModuleMessage(message[0]);
        else if (message.size() >= 1)
            applyParameterMessage(parameterId, message[0]);

        currentTarget = &processor;
        currentInstanceName.clear();
        return;
    }
}

void OscController::oscBundleReceived(const juce::OSCBundle& bundle)
{
    processBundle(bundle);
}

void OscController::processBundle(const juce::OSCBundle& bundle)
{
    for (const auto& element : bundle)
    {
        if (element.isMessage())
            oscMessageReceived(element.getMessage());
        else if (element.isBundle())
            processBundle(element.getBundle());
    }
}

bool OscController::readNumericArgument(const juce::OSCArgument& argument, float& value) noexcept
{
    if (argument.isFloat32())
    {
        value = argument.getFloat32();
        return std::isfinite(value);
    }

    if (argument.isInt32())
    {
        value = static_cast<float>(argument.getInt32());
        return true;
    }

    return false;
}

juce::RangedAudioParameter* OscController::findParameter(const juce::String& parameterId) noexcept
{
    const auto trimmedId = parameterId.trim();

    if (trimmedId.isEmpty())
        return nullptr;

    if (auto* parameter = targetProcessor().getValueTreeState().getParameter(trimmedId))
        return parameter;

    const auto findInModule = [] (auto* module, const juce::String& id) -> juce::RangedAudioParameter*
    {
        return module != nullptr ? module->getValueTreeState().getParameter(id) : nullptr;
    };

    size_t bandIndex = 0;
    juce::String unprefixedParameterId;
    const auto hasBandPrefix = parseBandParameterId(trimmedId, bandIndex, unprefixedParameterId);

    switch (targetProcessor().getActiveModule())
    {
        case AvaAudioProcessor::ActiveModule::eql:
            if (hasBandPrefix)
                if (auto* bank = targetProcessor().getEqlProcessorBank())
                    return findInModule(bank->getProcessor(bandIndex), unprefixedParameterId);
            return nullptr;
        case AvaAudioProcessor::ActiveModule::fft:
            if (hasBandPrefix)
                if (auto* bank = targetProcessor().getFftProcessorBank())
                    return findInModule(bank->getProcessor(bandIndex), unprefixedParameterId);
            return nullptr;
        case AvaAudioProcessor::ActiveModule::tls: return findInModule(targetProcessor().getTlsModuleProcessor(), trimmedId);
        case AvaAudioProcessor::ActiveModule::dyn: return findInModule(targetProcessor().getDynModuleProcessor(), trimmedId);
        case AvaAudioProcessor::ActiveModule::trs: return findInModule(targetProcessor().getTrsModuleProcessor(), trimmedId);
        case AvaAudioProcessor::ActiveModule::none: break;
    }

    return nullptr;
}

juce::StringArray OscController::getExclusiveListenGroup(const juce::String& parameterId) const
{
    juce::StringArray group;

    for (const auto* suffix : listenSuffixes)
    {
        if (parameterId != AvaAudioProcessor::getCrossoverParameterId(suffix))
            continue;

        for (const auto* groupSuffix : listenSuffixes)
            group.add(AvaAudioProcessor::getCrossoverParameterId(groupSuffix));

        return group;
    }

    if (getCrossoverSoloBandIndex(parameterId) >= 0)
    {
        const auto inclusiveModeProperty = crossover_ui::makeStatePropertyId(
            "crossover", "manual_solo_inclusive");
        const auto inclusiveMode = static_cast<bool>(
            targetProcessor().getValueTreeState().state.getProperty(inclusiveModeProperty, false));

        if (inclusiveMode)
            return group;

        const auto activeBandCount = targetProcessor().getCrossoverSettings().activeSplitCount + 1;
        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
            group.add(AvaAudioProcessor::getCrossoverSoloParameterId(bandIndex));

        return group;
    }

    if (targetProcessor().getActiveModule() != AvaAudioProcessor::ActiveModule::tls
        || ! parameterId.startsWith("band-"))
        return group;

    for (const auto* suffix : listenSuffixes)
    {
        const auto ending = "_" + juce::String(suffix);

        if (! parameterId.endsWith(ending))
            continue;

        const auto rangePrefix = parameterId.dropLastCharacters(ending.length());

        for (const auto* groupSuffix : listenSuffixes)
            group.add(rangePrefix + "_" + groupSuffix);

        break;
    }

    return group;
}

void OscController::sendParameterValue(const juce::String& parameterId,
                                       const float normalizedValue,
                                       const juce::RangedAudioParameter* parameter)
{
    if (! outputConnected)
        return;

    auto outgoingValue = parameter != nullptr
        ? parameter->convertFrom0to1(normalizedValue)
        : normalizedValue;

    if (dynamic_cast<const juce::AudioParameterChoice*>(parameter) != nullptr)
        outgoingValue += 1.0f;

    const auto address = makeOscAddressPrefix(currentInstanceName) + parameterId;
    if (sender.send(juce::OSCMessage(address, outgoingValue)))
        lastSentParameterValues[address] = normalizedValue;
}

bool OscController::applyParameterMessage(const juce::String& parameterId,
                                          const juce::OSCArgument& argument)
{
    if (parameterId.endsWith("-order"))
        return false;

    float value = 0.0f;

    if (! readNumericArgument(argument, value))
        return false;

    size_t eqlBandIndex = 0;
    juce::String eqlAction;
    const auto eqlFilterDeleteIndex = parseEqlFilterDeleteActionIndex(
        parameterId.substring(parameterId.indexOfChar('_') + 1));
    if (parseBandParameterId(parameterId, eqlBandIndex, eqlAction)
        && (eqlAction == "add"
            || eqlAction == "delete-all.hidden"
            || eqlAction == "preset"
            || eqlFilterDeleteIndex >= 0))
    {
        if (targetProcessor().getActiveModule() != AvaAudioProcessor::ActiveModule::eql
            || eqlBandIndex >= targetProcessor().getCrossoverSettings().activeSplitCount + 1)
            return false;

        if (auto* editor = targetProcessor().getOscActionEditor())
            return editor->handleOscEqlAction(eqlBandIndex, eqlAction, value);

        auto* bank = targetProcessor().getEqlProcessorBank();
        auto* eqlProcessor = bank != nullptr ? bank->getProcessor(eqlBandIndex) : nullptr;
        if (eqlProcessor == nullptr)
            return false;

        if (eqlAction == "add")
            return std::abs(value - 1.0f) <= parameterChangeTolerance
                && eqlProcessor->addFilter();

        if (eqlAction == "delete-all.hidden")
            return std::abs(value - 1.0f) <= parameterChangeTolerance
                && eqlProcessor->clearFilters();

        if (eqlFilterDeleteIndex >= 0)
            return std::abs(value - 1.0f) <= parameterChangeTolerance
                && eqlProcessor->removeFilter(eqlFilterDeleteIndex);

        const auto presetNumber = juce::roundToInt(value);
        const auto presetNames = eqlProcessor->getFilterPresetNames();
        return juce::isPositiveAndBelow(presetNumber - 1, presetNames.size())
            && std::abs(value - static_cast<float>(presetNumber)) <= parameterChangeTolerance
            && eqlProcessor->loadFilterPreset(presetNames[presetNumber - 1]);
    }

    if (parameterId == AvaAudioProcessor::oscGlobalClipId)
        return true;

    if (parameterId == AvaAudioProcessor::oscSoloModeId)
    {
        if (std::abs(value) > parameterChangeTolerance
            && std::abs(value - 1.0f) > parameterChangeTolerance)
            return false;

        const auto inclusiveMode = value >= 0.5f;
        auto& state = targetProcessor().getValueTreeState().state;
        state.setProperty(crossover_ui::makeStatePropertyId("crossover", "manual_solo_inclusive"),
                          inclusiveMode,
                          nullptr);

        const auto activeBandCount = targetProcessor().getCrossoverSettings().activeSplitCount + 1;
        auto soloBandToKeep = -1;

        if (! inclusiveMode)
        {
            const auto visibleBand = juce::jlimit(
                0,
                static_cast<int>(activeBandCount - 1),
                static_cast<int>(state.getProperty(
                    crossover_ui::makeStatePropertyId("crossover", "visible_band_index"), 0)));
            if (auto* visibleSolo = targetProcessor().getValueTreeState().getParameter(
                    AvaAudioProcessor::getCrossoverSoloParameterId(static_cast<size_t>(visibleBand))))
                if (visibleSolo->getValue() >= 0.5f)
                    soloBandToKeep = visibleBand;

            if (soloBandToKeep < 0)
            {
                for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
                {
                    if (auto* solo = targetProcessor().getValueTreeState().getParameter(
                            AvaAudioProcessor::getCrossoverSoloParameterId(bandIndex)))
                    {
                        if (solo->getValue() >= 0.5f)
                        {
                            soloBandToKeep = static_cast<int>(bandIndex);
                            break;
                        }
                    }
                }
            }
        }

        for (size_t bandIndex = 0; bandIndex < activeBandCount; ++bandIndex)
        {
            const auto soloId = AvaAudioProcessor::getCrossoverSoloParameterId(bandIndex);
            auto* solo = targetProcessor().getValueTreeState().getParameter(soloId);
            if (solo == nullptr)
                continue;

            const auto enabled = inclusiveMode ? solo->getValue() >= 0.5f
                                               : static_cast<int>(bandIndex) == soloBandToKeep;
            if (! inclusiveMode && ! enabled && solo->getValue() >= 0.5f)
            {
                solo->beginChangeGesture();
                solo->setValueNotifyingHost(0.0f);
                solo->endChangeGesture();
                sendParameterValue(soloId, 0.0f);
            }

            state.setProperty(crossover_ui::makeStatePropertyId(
                                  "crossover", "manual_solo." + juce::String(static_cast<int>(bandIndex))),
                              enabled,
                              nullptr);
        }

        targetProcessor().notifyHostOfStateChange();
        return true;
    }

    if (parameterId == AvaAudioProcessor::oscXovAddId
        || parameterId == AvaAudioProcessor::oscXovDelId)
    {
        if (std::abs(value - 1.0f) > parameterChangeTolerance)
            return false;

        auto* splitCount = targetProcessor().getValueTreeState().getParameter(
            AvaAudioProcessor::paramCrossoverActiveSplitCountId);
        if (splitCount == nullptr)
            return false;

        const auto currentCount = juce::roundToInt(splitCount->convertFrom0to1(splitCount->getValue()));
        const auto delta = parameterId == AvaAudioProcessor::oscXovAddId ? 1 : -1;
        const auto targetCount = juce::jlimit(0,
                                              static_cast<int>(ava::crossover::BufferRouter::numSplits),
                                              currentCount + delta);
        if (targetCount == currentCount)
            return true;

        splitCount->beginChangeGesture();
        splitCount->setValueNotifyingHost(splitCount->convertTo0to1(static_cast<float>(targetCount)));
        splitCount->endChangeGesture();
        return true;
    }

    if (parameterId == AvaAudioProcessor::paramCrossoverActiveSplitCountId)
        return false;

    const auto isGlobalAction = parameterId == AvaAudioProcessor::oscGlobalAbSlotAId
        || parameterId == AvaAudioProcessor::oscGlobalAbSwitchId
        || parameterId == AvaAudioProcessor::oscGlobalAbSlotBId
        || parameterId == AvaAudioProcessor::oscGlobalUndoId
        || parameterId == AvaAudioProcessor::oscGlobalRedoId
        || parameterId == AvaAudioProcessor::oscAddModuleId
        || parameterId == AvaAudioProcessor::oscCloseModuleId;

    if (isGlobalAction)
    {
        if (parameterId == AvaAudioProcessor::oscAddModuleId)
        {
            const auto moduleChoice = juce::roundToInt(value);
            if (moduleChoice < 1
                || moduleChoice > 5
                || std::abs(value - static_cast<float>(moduleChoice)) > parameterChangeTolerance)
                return false;
        }
        else if (std::abs(value - 1.0f) > parameterChangeTolerance)
        {
            return false;
        }

        if (auto* editor = targetProcessor().getOscActionEditor())
            return editor->handleOscGlobalAction(parameterId, value);

        if (parameterId == AvaAudioProcessor::oscAddModuleId)
        {
            static constexpr std::array modules {
                AvaAudioProcessor::ActiveModule::tls,
                AvaAudioProcessor::ActiveModule::eql,
                AvaAudioProcessor::ActiveModule::fft,
                AvaAudioProcessor::ActiveModule::dyn,
                AvaAudioProcessor::ActiveModule::trs
            };
            const auto index = juce::roundToInt(value) - 1;
            return targetProcessor().getActiveModule() == AvaAudioProcessor::ActiveModule::none
                && targetProcessor().loadModule(modules[static_cast<size_t>(index)]);
        }
        if (parameterId == AvaAudioProcessor::oscCloseModuleId)
            return targetProcessor().clearLoadedModule();

        return false;
    }

    auto* parameter = findParameter(parameterId);

    if (parameter == nullptr)
        return false;

    const auto isChoice = dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr;
    const auto parameterValue = value - (isChoice ? 1.0f : 0.0f);
    const auto& range = parameter->getNormalisableRange();
    const auto tolerance = juce::jmax(1.0e-6f, std::abs(range.end - range.start) * 1.0e-6f);
    if (parameterValue < range.start - tolerance || parameterValue > range.end + tolerance)
        return false;

    if (const auto splitIndex = getCrossoverSplitIndex(parameterId); splitIndex >= 0)
    {
        constexpr auto minimumSplitGapHz = 1.0f;
        const auto activeSplitCount = static_cast<int>(targetProcessor().getCrossoverSettings().activeSplitCount);
        if (splitIndex >= activeSplitCount)
            return false;

        auto lowerBound = range.start + (minimumSplitGapHz * static_cast<float>(splitIndex));
        auto upperBound = range.end
            - (minimumSplitGapHz * static_cast<float>(activeSplitCount - splitIndex - 1));

        if (splitIndex > 0)
        {
            const auto previousSuffix = "split-" + juce::String(splitIndex);
            if (const auto* previous = targetProcessor().getValueTreeState().getParameter(
                    AvaAudioProcessor::getCrossoverParameterId(previousSuffix.toRawUTF8())))
                lowerBound = juce::jmax(lowerBound,
                                        previous->convertFrom0to1(previous->getValue()) + minimumSplitGapHz);
        }

        if (splitIndex + 1 < activeSplitCount)
        {
            const auto nextSuffix = "split-" + juce::String(splitIndex + 2);
            if (const auto* next = targetProcessor().getValueTreeState().getParameter(
                    AvaAudioProcessor::getCrossoverParameterId(nextSuffix.toRawUTF8())))
                upperBound = juce::jmin(upperBound,
                                        next->convertFrom0to1(next->getValue()) - minimumSplitGapHz);
        }

        if (parameterValue < lowerBound - tolerance || parameterValue > upperBound + tolerance)
            return false;
    }

    const auto snappedValue = range.snapToLegalValue(parameterValue);
    const auto requiresExactDiscreteValue = dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr
        || dynamic_cast<juce::AudioParameterInt*>(parameter) != nullptr
        || dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr;
    if (requiresExactDiscreteValue && std::abs(snappedValue - parameterValue) > tolerance)
        return false;

    const auto normalizedValue = juce::jlimit(0.0f, 1.0f, parameter->convertTo0to1(snappedValue));
    const auto exclusiveGroup = normalizedValue >= 0.5f
        ? getExclusiveListenGroup(parameterId)
        : juce::StringArray {};

    if (! exclusiveGroup.isEmpty() && parameter->getValue() < 0.5f)
    {
        for (const auto& otherParameterId : exclusiveGroup)
        {
            if (otherParameterId == parameterId)
                continue;

            auto* otherParameter = findParameter(otherParameterId);

            if (otherParameter == nullptr || otherParameter->getValue() < 0.5f)
                continue;

            otherParameter->beginChangeGesture();
            otherParameter->setValueNotifyingHost(0.0f);
            otherParameter->endChangeGesture();
            sendParameterValue(otherParameterId, 0.0f);

            const auto otherSoloBandIndex = getCrossoverSoloBandIndex(otherParameterId);
            if (otherSoloBandIndex >= 0)
                targetProcessor().getValueTreeState().state.setProperty(
                    crossover_ui::makeStatePropertyId(
                        "crossover", "manual_solo." + juce::String(otherSoloBandIndex)),
                    false,
                    nullptr);
        }
    }

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalizedValue);
    parameter->endChangeGesture();

    const auto soloBandIndex = getCrossoverSoloBandIndex(parameterId);
    if (soloBandIndex >= 0)
    {
        targetProcessor().getValueTreeState().state.setProperty(
            crossover_ui::makeStatePropertyId(
                "crossover", "manual_solo." + juce::String(soloBandIndex)),
            normalizedValue >= 0.5f,
            nullptr);
        targetProcessor().notifyHostOfStateChange();
    }

    if (! exclusiveGroup.isEmpty() && normalizedValue >= 0.5f)
        sendParameterValue(parameterId, normalizedValue);

    return true;
}

bool OscController::applyModuleMessage(const juce::OSCArgument& argument)
{
    if (! argument.isString())
        return false;

    const auto moduleName = argument.getString().trim().toLowerCase();
    auto target = AvaAudioProcessor::ActiveModule::none;

    if (moduleName == AvaAudioProcessor::eqlModuleId) target = AvaAudioProcessor::ActiveModule::eql;
    else if (moduleName == AvaAudioProcessor::fftModuleId) target = AvaAudioProcessor::ActiveModule::fft;
    else if (moduleName == AvaAudioProcessor::tlsModuleId) target = AvaAudioProcessor::ActiveModule::tls;
    else if (moduleName == AvaAudioProcessor::dynModuleId) target = AvaAudioProcessor::ActiveModule::dyn;
    else if (moduleName == AvaAudioProcessor::trsModuleId) target = AvaAudioProcessor::ActiveModule::trs;
    else if (moduleName != "none" && moduleName != "off" && moduleName.isNotEmpty()) return false;

    if (targetProcessor().getActiveModule() == target)
        return true;

    if (targetProcessor().getActiveModule() != AvaAudioProcessor::ActiveModule::none
        && ! targetProcessor().clearLoadedModule())
        return false;

    return target == AvaAudioProcessor::ActiveModule::none || targetProcessor().loadModule(target);
}

void OscController::timerCallback()
{
    sendCurrentState();
}

void OscController::sendCurrentState()
{
    if (! outputConnected)
        return;

    const auto routing = ava::routing::readState(processor.getValueTreeState().state);
    juce::String routingSignature;
    for (const auto& node : routing.nodes)
        routingSignature += juce::String(node.id) + ":"
            + ava::routing::getDisplayName(routing, node.id) + ";";
    if (routingSignature != lastRoutingSignature)
    {
        lastRoutingSignature = routingSignature;
        lastSentParameterValues.clear();
        lastSentModules.clear();
        lastSentSplitCounts.clear();
    }

    for (const auto& node : routing.nodes)
    {
        auto handle = node.id == routing.rootInstanceId
            ? std::shared_ptr<AvaAudioProcessor> {}
            : processor.getRoutingInstanceHandle(node.id);
        if (node.id != routing.rootInstanceId && handle == nullptr)
            continue;
        currentTarget = handle != nullptr ? handle.get() : &processor;
        currentInstanceName = ava::routing::getDisplayName(routing, node.id);
        sendInstanceState();
    }
    currentTarget = &processor;
    currentInstanceName.clear();
}

void OscController::sendInstanceState()
{
    const auto module = juce::String(AvaAudioProcessor::stateIdForModule(targetProcessor().getActiveModule()));
    auto& lastSentModule = lastSentModules[currentInstanceName];

    if (module != lastSentModule)
    {
        sender.send(juce::OSCMessage(makeOscAddressPrefix(currentInstanceName) + "module",
                                     module.isEmpty() ? juce::String("none") : module));
        lastSentModule = module;
        const auto prefix = makeOscAddressPrefix(currentInstanceName);
        for (auto it = lastSentParameterValues.begin(); it != lastSentParameterValues.end();)
            it = it->first.startsWith(prefix) ? lastSentParameterValues.erase(it) : std::next(it);
    }

    const auto activeABSlot = targetProcessor().getABCompareActiveSlot();
    sendParameterValue(AvaAudioProcessor::oscGlobalAbSlotAId, activeABSlot == 0 ? 1.0f : 0.0f);
    sendParameterValue(AvaAudioProcessor::oscGlobalAbSlotBId, activeABSlot == 1 ? 1.0f : 0.0f);
    sendParameterValue(AvaAudioProcessor::oscGlobalClipId,
                       juce::jlimit(0.0f, 1.0f, targetProcessor().getGlobalClipIndicator()));
    sendParameterValue(AvaAudioProcessor::oscSoloModeId,
                       static_cast<bool>(targetProcessor().getValueTreeState().state.getProperty(
                           crossover_ui::makeStatePropertyId("crossover", "manual_solo_inclusive"), false))
                           ? 1.0f
                           : 0.0f);

    sendCrossoverActions();
    sendParameters(targetProcessor().getValueTreeState());

    switch (targetProcessor().getActiveModule())
    {
        case AvaAudioProcessor::ActiveModule::eql:
            if (auto* bank = targetProcessor().getEqlProcessorBank())
                for (size_t bandIndex = 0; bandIndex < bank->getCreatedRangeCount(); ++bandIndex)
                    if (auto* moduleProcessor = bank->getProcessor(bandIndex))
                    {
                        sendParameters(moduleProcessor->getValueTreeState(),
                                       "band-" + juce::String(static_cast<int>(bandIndex + 1)) + "_");

                        const auto presetNames = moduleProcessor->getFilterPresetNames();
                        const auto selectedPreset = moduleProcessor->getSelectedFilterPresetName();
                        const auto presetIndex = presetNames.indexOf(selectedPreset, true);
                        if (presetIndex >= 0)
                            sendParameterValue(AvaAudioProcessor::getEqlPresetSelectionId(bandIndex),
                                               static_cast<float>(presetIndex + 1));
                    }
            break;
        case AvaAudioProcessor::ActiveModule::fft:
            if (auto* bank = targetProcessor().getFftProcessorBank())
                for (size_t bandIndex = 0; bandIndex < bank->getCreatedRangeCount(); ++bandIndex)
                    if (auto* moduleProcessor = bank->getProcessor(bandIndex))
                        sendParameters(moduleProcessor->getValueTreeState(),
                                       "band-" + juce::String(static_cast<int>(bandIndex + 1)) + "_");
            break;
        case AvaAudioProcessor::ActiveModule::tls:
            if (auto* moduleProcessor = targetProcessor().getTlsModuleProcessor()) sendParameters(moduleProcessor->getValueTreeState());
            break;
        case AvaAudioProcessor::ActiveModule::dyn:
            if (auto* moduleProcessor = targetProcessor().getDynModuleProcessor()) sendParameters(moduleProcessor->getValueTreeState());
            break;
        case AvaAudioProcessor::ActiveModule::trs:
            if (auto* moduleProcessor = targetProcessor().getTrsModuleProcessor()) sendParameters(moduleProcessor->getValueTreeState());
            break;
        case AvaAudioProcessor::ActiveModule::none:
            break;
    }
}

void OscController::sendCrossoverActions()
{
    auto* splitCount = targetProcessor().getValueTreeState().getParameter(
        AvaAudioProcessor::paramCrossoverActiveSplitCountId);
    if (splitCount == nullptr)
        return;

    const auto currentCount = juce::roundToInt(splitCount->convertFrom0to1(splitCount->getValue()));
    auto& lastSentSplitCount = lastSentSplitCounts.try_emplace(currentInstanceName, -1).first->second;
    if (lastSentSplitCount < 0)
    {
        lastSentSplitCount = currentCount;
        return;
    }

    const auto actionId = currentCount > lastSentSplitCount
        ? AvaAudioProcessor::oscXovAddId
        : AvaAudioProcessor::oscXovDelId;
    const auto actionCount = std::abs(currentCount - lastSentSplitCount);
    lastSentSplitCount = currentCount;

    for (int actionIndex = 0; actionIndex < actionCount; ++actionIndex)
        sendParameterValue(actionId, 1.0f);
}

void OscController::sendParameters(juce::AudioProcessorValueTreeState& state,
                                   const juce::String& parameterIdPrefix)
{
    for (const auto parameterState : state.state)
    {
        const auto parameterId = parameterState.getProperty("id").toString();
        auto* parameter = state.getParameter(parameterId);

        if (parameterId.isEmpty()
            || parameter == nullptr
            || parameterId.endsWith("-order"))
            continue;

        if (&state == &targetProcessor().getValueTreeState()
            && (parameterId == AvaAudioProcessor::paramCrossoverActiveSplitCountId
                || parameterId.startsWith(AvaAudioProcessor::paramHostSlotPrefix)))
            continue;

        const auto currentValue = parameter->getValue();
        const auto oscParameterId = parameterIdPrefix + parameterId;
        const auto previous = lastSentParameterValues.find(
            makeOscAddressPrefix(currentInstanceName) + oscParameterId);

        if (previous != lastSentParameterValues.end()
            && std::abs(previous->second - currentValue) <= parameterChangeTolerance)
            continue;

        sendParameterValue(oscParameterId, currentValue, parameter);
    }
}
