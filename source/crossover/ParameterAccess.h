#pragma once

#include "ParameterIds.h"

#include <JuceHeader.h>

#include <atomic>
#include <utility>

namespace ava::crossover::parameters
{
inline constexpr float minimumSplitGapHz = 1.0f;

template <typename MakeId>
juce::Range<float> splitFrequencyBounds(juce::AudioProcessorValueTreeState& state,
    const size_t splitIndex, const size_t activeCount, MakeId makeId)
{
    auto* parameter = state.getParameter(makeId(("split-" + juce::String(int(splitIndex + 1))).toRawUTF8()));
    if (parameter == nullptr || splitIndex >= activeCount) return {};
    const auto& range = parameter->getNormalisableRange();
    const auto absoluteLow = range.start + minimumSplitGapHz * float(splitIndex);
    const auto absoluteHigh = range.end - minimumSplitGapHz * float(activeCount - splitIndex - 1);
    auto low = absoluteLow, high = absoluteHigh;
    if (splitIndex > 0)
        if (auto* previous = state.getParameter(makeId(("split-" + juce::String(int(splitIndex))).toRawUTF8())))
            low = juce::jlimit(absoluteLow, absoluteHigh,
                previous->convertFrom0to1(previous->getValue()) + minimumSplitGapHz);
    if (splitIndex + 1 < activeCount)
        if (auto* next = state.getParameter(makeId(("split-" + juce::String(int(splitIndex + 2))).toRawUTF8())))
            high = juce::jlimit(absoluteLow, absoluteHigh,
                next->convertFrom0to1(next->getValue()) - minimumSplitGapHz);
    return { low, juce::jmax(low, high) };
}

template <typename PointerGrid, typename ParameterSpecs>
void cacheRangeParameterPointers(juce::AudioProcessorValueTreeState& state,
                                 PointerGrid& pointers,
                                 const ParameterSpecs& specs)
{
    for (size_t rangeIndex = 0; rangeIndex < pointers.size(); ++rangeIndex)
    {
        for (size_t parameterIndex = 0; parameterIndex < specs.size(); ++parameterIndex)
        {
            pointers[rangeIndex][parameterIndex] = state.getRawParameterValue(
                makeRangeParameterId(rangeIndex, specs[parameterIndex].suffix));
            jassert(pointers[rangeIndex][parameterIndex] != nullptr);
        }
    }
}

template <typename ParameterSpecs>
void setRangeParameterListenersEnabled(juce::AudioProcessorValueTreeState& state,
                                       juce::AudioProcessorValueTreeState::Listener& listener,
                                       const ParameterSpecs& specs,
                                       const size_t rangeCount,
                                       const bool enabled)
{
    for (size_t rangeIndex = 0; rangeIndex < rangeCount; ++rangeIndex)
    {
        for (const auto& spec : specs)
        {
            const auto parameterId = makeRangeParameterId(rangeIndex, spec.suffix);

            if (enabled)
                state.addParameterListener(parameterId, &listener);
            else
                state.removeParameterListener(parameterId, &listener);
        }
    }
}

template <typename RangeParameters, typename ReadRangeParameters, typename ProcessorBank>
bool syncRangeParameters(std::atomic<bool>& dirty,
                         const bool force,
                         RangeParameters& currentParameters,
                         ReadRangeParameters&& readRangeParameters,
                         ProcessorBank& processorBank)
{
    if (! force && ! dirty.exchange(false, std::memory_order_acq_rel))
        return false;

    if (force)
        dirty.store(false, std::memory_order_release);

    for (size_t rangeIndex = 0; rangeIndex < currentParameters.size(); ++rangeIndex)
        currentParameters[rangeIndex] = readRangeParameters(rangeIndex);

    processorBank.setRangeParameters(currentParameters);
    return true;
}
}
