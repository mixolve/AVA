#include "OscController.h"
#include "Processor.h"

#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../crossover/ParameterAccess.h"
#include "../crossover/UiStateSchema.h"
#include "../routing/State.h"

#include <cmath>

namespace
{
constexpr auto outputRefreshRateHz = 30;
constexpr auto parameterChangeTolerance = 1.0e-6f;

bool isValidPort(const int port) noexcept
{
    return port >= 1 && port <= 65535;
}

bool isPortAvailable(const int port)
{
    juce::DatagramSocket socket;
    return socket.bindToPort(port);
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
            sendInstanceState();
        }
        else if (message.size() >= 1)
        {
            currentAssignedParameters = targetProcessor().getAssignedOscParameters();
            applyParameterMessage(getOscSourceId(parameterId, currentAssignedParameters), message[0]);
        }

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
    return targetProcessor().findHostSlotTarget(parameterId);
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

    const auto metadata = std::find_if(currentAssignedParameters.begin(), currentAssignedParameters.end(),
        [&parameterId] (const auto& info) { return info.sourceId == parameterId; });
    if (metadata == currentAssignedParameters.end()) return;
    if (metadata->parameterRange.has_value()) outgoingValue = juce::jlimit(
        metadata->parameterRange->getStart(), metadata->parameterRange->getEnd(), outgoingValue);
    outgoingValue *= metadata->valueScale;
    const auto address = makeAssignedOscAddress(currentInstanceName, *metadata);
    if (sender.send(juce::OSCMessage(address, outgoingValue)))
        lastSentParameterValues[address] = normalizedValue;
}

bool OscController::applyParameterMessage(const juce::String& parameterId,
                                          const juce::OSCArgument& argument)
{
    float value = 0.0f;

    if (! readNumericArgument(argument, value))
        return false;

    auto* parameter = findParameter(parameterId);

    if (parameter == nullptr)
        return false;

    const auto metadata = std::find_if(currentAssignedParameters.begin(), currentAssignedParameters.end(),
        [&parameterId] (const auto& info) { return info.sourceId == parameterId; });
    if (metadata == currentAssignedParameters.end()) return false;
    const auto isChoice = dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr;
    const auto parameterValue = value / metadata->valueScale
        - (isChoice ? 1.0f : 0.0f);
    if (metadata->parameterRange.has_value()
        && (parameterValue < metadata->parameterRange->getStart() - 1.0e-6f
            || parameterValue > metadata->parameterRange->getEnd() + 1.0e-6f))
        return false;
    const auto& range = parameter->getNormalisableRange();
    const auto tolerance = juce::jmax(1.0e-6f, std::abs(range.end - range.start) * 1.0e-6f);
    if (parameterValue < range.start - tolerance || parameterValue > range.end + tolerance)
        return false;

    if (const auto splitIndex = getCrossoverSplitIndex(parameterId); splitIndex >= 0)
    {
        const auto activeSplitCount = targetProcessor().getCrossoverSettings().activeSplitCount;
        if (size_t(splitIndex) >= activeSplitCount) return false;
        const auto bounds = ava::crossover::parameters::splitFrequencyBounds(
            targetProcessor().getValueTreeState(), size_t(splitIndex), activeSplitCount,
            [] (const char* suffix) { return AvaAudioProcessor::getCrossoverParameterId(suffix); });
        if (parameterValue < bounds.getStart() - tolerance || parameterValue > bounds.getEnd() + tolerance)
            return false;
    }

    const auto snappedValue = range.snapToLegalValue(parameterValue);
    const auto requiresExactDiscreteValue = dynamic_cast<juce::AudioParameterBool*>(parameter) != nullptr
        || dynamic_cast<juce::AudioParameterInt*>(parameter) != nullptr
        || dynamic_cast<juce::AudioParameterChoice*>(parameter) != nullptr;
    if (requiresExactDiscreteValue && std::abs(snappedValue - parameterValue) > tolerance)
        return false;

    const auto normalizedValue = juce::jlimit(0.0f, 1.0f, parameter->convertTo0to1(snappedValue));
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(normalizedValue);
    parameter->endChangeGesture();

    sendParameterValue(parameterId, parameter->getValue(), parameter);

    return true;
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
        lastAssignedParameters.clear();
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
    currentAssignedParameters = targetProcessor().getAssignedOscParameters();
    auto& previous = lastAssignedParameters[currentInstanceName];
    if (currentAssignedParameters != previous)
    {
        const auto prefix = makeOscAddressPrefix(currentInstanceName);
        for (auto it = lastSentParameterValues.begin(); it != lastSentParameterValues.end();)
            it = it->first.startsWith(prefix) ? lastSentParameterValues.erase(it) : std::next(it);
        previous = currentAssignedParameters;
    }
    for (const auto& info : currentAssignedParameters)
    {
        auto* parameter = findParameter(info.sourceId);
        if (parameter == nullptr) continue;
        const auto value = parameter->getValue();
        const auto address = makeAssignedOscAddress(currentInstanceName, info);
        const auto sent = lastSentParameterValues.find(address);
        if (sent != lastSentParameterValues.end() && std::abs(sent->second - value) <= parameterChangeTolerance) continue;
        sendParameterValue(info.sourceId, value, parameter);
    }
}
