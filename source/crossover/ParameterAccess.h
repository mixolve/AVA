#pragma once

#include "ParameterIds.h"

#include <JuceHeader.h>

#include <atomic>
#include <utility>

namespace ava::crossover::parameters
{
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
