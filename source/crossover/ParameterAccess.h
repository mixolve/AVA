#pragma once

#include "ParameterIds.h"

#include <JuceHeader.h>

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
} // namespace ava::crossover::parameters
