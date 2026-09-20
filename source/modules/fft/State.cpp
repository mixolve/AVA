#include "Processor.h"
#include "../shared/StateUtilities.h"

#include <cmath>

namespace
{
juce::ValueTree buildCombinedFftState(juce::AudioProcessorValueTreeState& parameters,
                                      const juce::ValueTree& analyserState)
{
    auto state = parameters.copyState();
    state.appendChild(analyserState.createCopy(), nullptr);
    return state;
}

constexpr std::array<const char*, 5> dualMonoLinkedParameterIds {
    FftModuleProcessor::paramDualMonoLeftThresholdId,
    FftModuleProcessor::paramDualMonoRightThresholdId,
    FftModuleProcessor::paramDualMonoLeftAdaptiveId,
    FftModuleProcessor::paramDualMonoRightAdaptiveId,
    FftModuleProcessor::paramDualMonoLinkId
};

bool hasCurrentDualMonoLinkState(const juce::ValueTree& state)
{
    const auto link = ava::modules::state::readParameterPlainValue(state, FftModuleProcessor::paramDualMonoLinkId);

    if (! link.has_value())
        return false;

    if (*link < 0.5f)
        return true;

    const auto leftThreshold = ava::modules::state::readParameterPlainValue(state, FftModuleProcessor::paramDualMonoLeftThresholdId);
    const auto rightThreshold = ava::modules::state::readParameterPlainValue(state, FftModuleProcessor::paramDualMonoRightThresholdId);
    const auto leftAdaptive = ava::modules::state::readParameterPlainValue(state, FftModuleProcessor::paramDualMonoLeftAdaptiveId);
    const auto rightAdaptive = ava::modules::state::readParameterPlainValue(state, FftModuleProcessor::paramDualMonoRightAdaptiveId);

    if (! leftThreshold.has_value() || ! rightThreshold.has_value()
        || ! leftAdaptive.has_value() || ! rightAdaptive.has_value())
        return false;

    return std::abs(*leftThreshold - *rightThreshold) <= 1.0e-6f
        && std::abs(*leftAdaptive - *rightAdaptive) <= 1.0e-6f;
}

}

void FftModuleProcessor::setDualMonoLinkListenersEnabled(const bool enabled)
{
    for (const auto* parameterId : dualMonoLinkedParameterIds)
    {
        if (enabled)
            parameters.addParameterListener(parameterId, this);
        else
            parameters.removeParameterListener(parameterId, this);
    }
}

void FftModuleProcessor::parameterChanged(const juce::String& parameterID, float)
{
    if (linkedDualMonoPropagationInProgress.exchange(true, std::memory_order_acq_rel))
        return;

    const auto linkActive = dualMonoLinkParam != nullptr
        && dualMonoLinkParam->load(std::memory_order_relaxed) >= 0.5f;

    const auto mirrorParameter = [this] (const char* sourceParameterId, const char* targetParameterId)
    {
        const auto* source = parameters.getRawParameterValue(sourceParameterId);

        if (source == nullptr)
            return;

        auto* target = parameters.getParameter(targetParameterId);

        if (target == nullptr)
            return;

        const auto targetValue = source->load(std::memory_order_relaxed);
        const auto normalizedValue = target->convertTo0to1(targetValue);

        if (std::abs(target->getValue() - normalizedValue) <= 1.0e-6f)
            return;

        target->setValueNotifyingHost(normalizedValue);
    };

    if (linkActive)
    {
        if (parameterID == paramDualMonoLinkId)
        {
            mirrorParameter(paramDualMonoLeftThresholdId, paramDualMonoRightThresholdId);
            mirrorParameter(paramDualMonoLeftAdaptiveId, paramDualMonoRightAdaptiveId);
        }
        else if (parameterID == paramDualMonoLeftThresholdId)
            mirrorParameter(paramDualMonoLeftThresholdId, paramDualMonoRightThresholdId);
        else if (parameterID == paramDualMonoRightThresholdId)
            mirrorParameter(paramDualMonoRightThresholdId, paramDualMonoLeftThresholdId);
        else if (parameterID == paramDualMonoLeftAdaptiveId)
            mirrorParameter(paramDualMonoLeftAdaptiveId, paramDualMonoRightAdaptiveId);
        else if (parameterID == paramDualMonoRightAdaptiveId)
            mirrorParameter(paramDualMonoRightAdaptiveId, paramDualMonoLeftAdaptiveId);
    }

    linkedDualMonoPropagationInProgress.store(false, std::memory_order_release);
}

juce::String FftModuleProcessor::getStateXmlString() const
{
    auto state = buildCombinedFftState(const_cast<juce::AudioProcessorValueTreeState&>(parameters),
                                       createAnalyserStateSnapshot());

    if (auto stateXml = state.createXml())
        return stateXml->toString();

    return {};
}

bool FftModuleProcessor::setStateFromXmlString(const juce::String& stateXmlString)
{
    if (stateXmlString.trim().isEmpty())
        return false;

    auto stateXml = juce::parseXML(stateXmlString);

    if (stateXml == nullptr)
        return false;

    auto state = juce::ValueTree::fromXml(*stateXml);

    if (! isCurrentState(state))
        return false;

    auto restoredAnalyserState = state.getChildWithName(analyserState.getType());
    state.removeChild(state.indexOf(restoredAnalyserState), nullptr);
    setDualMonoLinkListenersEnabled(false);
    parameters.replaceState(state);
    setDualMonoLinkListenersEnabled(true);
    applyAnalyserState(restoredAnalyserState);
    refreshLatencyState();
    return true;
}

void FftModuleProcessor::copyGainReductionData(std::array<float, analyserScopeSize>& leftDestination,
                                               std::array<float, analyserScopeSize>& rightDestination) const
{
    dynamicProcessors[static_cast<size_t>(activeDynamicProcessorIndex)]
        .copyReductionScope(leftDestination, rightDestination);
}

bool FftModuleProcessor::isCorrelationMode() const noexcept
{
    return dynamicModeParam != nullptr
        && dynamicModeParam->load(std::memory_order_relaxed) >= 0.5f;
}

bool FftModuleProcessor::isUpwardMode() const noexcept
{
    return dynamicDirectionParam != nullptr
        && dynamicDirectionParam->load(std::memory_order_relaxed) >= 0.5f;
}

void FftModuleProcessor::getReductionDisplayBounds(float& minimum, float& maximum) const noexcept
{
    if (isCorrelationMode())
    {
        const auto first = correlationReductionLowValue.load(std::memory_order_relaxed);
        const auto second = correlationReductionHighValue.load(std::memory_order_relaxed);
        minimum = juce::jmin(first, second);
        maximum = juce::jmax(first, second);
    }
    else
    {
        const auto first = std::abs(spectralReductionHighValue.load(std::memory_order_relaxed));
        const auto second = std::abs(spectralReductionLowValue.load(std::memory_order_relaxed));
        minimum = juce::jmin(first, second);
        maximum = juce::jmax(first, second);
    }

    maximum = juce::jmax(minimum + 0.01f, maximum);
}

juce::AudioProcessorValueTreeState& FftModuleProcessor::getValueTreeState() noexcept
{
    return parameters;
}

const juce::AudioProcessorValueTreeState& FftModuleProcessor::getValueTreeState() const noexcept
{
    return parameters;
}

juce::ValueTree FftModuleProcessor::createAnalyserStateSnapshot() const
{
    auto state = juce::ValueTree(analyserState.getType());
    state.setProperty(paramTimeId, analyserTimeValue.load(std::memory_order_relaxed), nullptr);
    state.setProperty(paramSpectralReductionHighId, spectralReductionHighValue.load(std::memory_order_relaxed), nullptr);
    state.setProperty(paramSpectralReductionLowId, spectralReductionLowValue.load(std::memory_order_relaxed), nullptr);
    state.setProperty(paramCorrelationReductionHighId, correlationReductionHighValue.load(std::memory_order_relaxed), nullptr);
    state.setProperty(paramCorrelationReductionLowId, correlationReductionLowValue.load(std::memory_order_relaxed), nullptr);
    return state;
}

bool FftModuleProcessor::isCurrentState(const juce::ValueTree& state) const noexcept
{
    if (state.getNumProperties() != 0)
        return false;

    if (! ava::modules::state::hasExactParameterState(
            state,
            const_cast<juce::AudioProcessorValueTreeState&>(parameters),
            analyserState.getType()))
        return false;

    if (! hasCurrentDualMonoLinkState(state))
        return false;

    const auto restoredAnalyserState = state.getChildWithName(analyserState.getType());

    if (! restoredAnalyserState.isValid()
        || restoredAnalyserState.getNumChildren() != 0
        || restoredAnalyserState.getNumProperties() != 5)
        return false;

    struct PropertyRange
    {
        const char* id;
        float minimum;
        float maximum;
    };

    constexpr std::array<PropertyRange, 5> ranges {{
        { paramTimeId, 0.0f, 1000.0f },
        { paramSpectralReductionHighId, -99.0f, 0.0f },
        { paramSpectralReductionLowId, -99.0f, 0.0f },
        { paramCorrelationReductionHighId, 0.0f, 4.0f },
        { paramCorrelationReductionLowId, 0.0f, 4.0f }
    }};

    for (const auto& range : ranges)
    {
        if (! restoredAnalyserState.hasProperty(range.id))
            return false;

        const auto value = ava::modules::state::parseExactFiniteNumber(
            restoredAnalyserState.getProperty(range.id));

        if (! value.has_value()
            || *value < static_cast<double>(range.minimum)
            || *value > static_cast<double>(range.maximum))
            return false;
    }

    for (int propertyIndex = 0; propertyIndex < restoredAnalyserState.getNumProperties(); ++propertyIndex)
    {
        const auto property = restoredAnalyserState.getPropertyName(propertyIndex);
        auto known = false;

        for (const auto& range : ranges)
            known = known || property == juce::Identifier(range.id);

        if (! known)
            return false;
    }

    return true;
}

float FftModuleProcessor::getAnalyserParameterValue(const juce::String& parameterId) const noexcept
{
    if (parameterId == paramTimeId)
        return analyserTimeValue.load(std::memory_order_relaxed);

    if (parameterId == paramSpectralReductionHighId)
        return spectralReductionHighValue.load(std::memory_order_relaxed);

    if (parameterId == paramSpectralReductionLowId)
        return spectralReductionLowValue.load(std::memory_order_relaxed);

    if (parameterId == paramCorrelationReductionHighId)
        return correlationReductionHighValue.load(std::memory_order_relaxed);

    if (parameterId == paramCorrelationReductionLowId)
        return correlationReductionLowValue.load(std::memory_order_relaxed);

    return 0.0f;
}

void FftModuleProcessor::setAnalyserParameterValue(const juce::String& parameterId, float value)
{
    if (parameterId == paramTimeId)
    {
        const auto clamped = juce::jlimit(0.0f, 1000.0f, value);
        analyserTimeValue.store(clamped, std::memory_order_relaxed);
    }

    if (parameterId == paramSpectralReductionHighId)
    {
        spectralReductionHighValue.store(juce::jlimit(-99.0f, 0.0f, value), std::memory_order_relaxed);
        return;
    }

    if (parameterId == paramSpectralReductionLowId)
    {
        spectralReductionLowValue.store(juce::jlimit(-99.0f, 0.0f, value), std::memory_order_relaxed);
        return;
    }

    if (parameterId == paramCorrelationReductionHighId)
    {
        correlationReductionHighValue.store(juce::jlimit(0.0f, 4.0f, value), std::memory_order_relaxed);
        return;
    }

    if (parameterId == paramCorrelationReductionLowId)
        correlationReductionLowValue.store(juce::jlimit(0.0f, 4.0f, value), std::memory_order_relaxed);
}

void FftModuleProcessor::resetAnalyserState()
{
    setAnalyserParameterValue(paramTimeId, 50.0f);
    setAnalyserParameterValue(paramSpectralReductionHighId, 0.0f);
    setAnalyserParameterValue(paramSpectralReductionLowId, -36.0f);
    setAnalyserParameterValue(paramCorrelationReductionHighId, 2.2f);
    setAnalyserParameterValue(paramCorrelationReductionLowId, 0.0f);
}

void FftModuleProcessor::applyAnalyserState(juce::ValueTree state)
{
    setAnalyserParameterValue(paramTimeId, static_cast<float>(state.getProperty(paramTimeId)));
    setAnalyserParameterValue(paramSpectralReductionHighId,
                              static_cast<float>(state.getProperty(paramSpectralReductionHighId)));
    setAnalyserParameterValue(paramSpectralReductionLowId,
                              static_cast<float>(state.getProperty(paramSpectralReductionLowId)));
    setAnalyserParameterValue(paramCorrelationReductionHighId,
                              static_cast<float>(state.getProperty(paramCorrelationReductionHighId)));
    setAnalyserParameterValue(paramCorrelationReductionLowId,
                              static_cast<float>(state.getProperty(paramCorrelationReductionLowId)));
}
