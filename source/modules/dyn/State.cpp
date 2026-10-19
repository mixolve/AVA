#include "Processor.h"
#include "../shared/StateValidation.h"

#include "../../crossover/ParameterIds.h"
#include "../../crossover/UiStateSchema.h"

#include <array>
#include <cmath>
#include <optional>

void DynModuleProcessor::getStateInformation(juce::MemoryBlock& destData) const
{
    if (auto stateXml = const_cast<juce::AudioProcessorValueTreeState&>(valueTreeState).copyState().createXml())
        juce::AudioProcessor::copyXmlToBinary(*stateXml, destData);
}

namespace
{
bool approximatelyEqual(const float left, const float right) noexcept
{
    return std::abs(left - right) <= 1.0e-4f;
}

bool hasCurrentDynInvariants(const juce::ValueTree& state)
{
    using ava::crossover::parameters::makeRangeParameterId;
    using dyn::parameters::ParameterSlot;
    using dyn::parameters::parameterSpecs;
    using dyn::parameters::toIndex;

    const auto read = [&state](const size_t rangeIndex, const ParameterSlot slot) -> std::optional<float>
    {
        return ava::modules::state::readParameterPlainValue(
            state, makeRangeParameterId(rangeIndex, parameterSpecs[toIndex(slot)].suffix));
    };

    for (const auto slot : dyn::parameters::linkedParameterSlots)
    {
        const auto reference = read(0, slot);

        if (!reference.has_value())
            return false;

        for (size_t rangeIndex = 1; rangeIndex < dyn::dsp::ProcessorBank::numRanges; ++rangeIndex)
        {
            const auto candidate = read(rangeIndex, slot);

            if (!candidate.has_value() || !approximatelyEqual(*candidate, *reference))
                return false;
        }
    }

    return true;
}
}

bool DynModuleProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    auto xmlState = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);

    if (xmlState == nullptr)
        return false;

    auto restoredState = juce::ValueTree::fromXml(*xmlState);

    if (!ava::modules::state::hasExactParameterState(restoredState, valueTreeState) ||
        !crossover_ui::hasCurrentStateProperties(restoredState, "dyn") || !hasCurrentDynInvariants(restoredState))
        return false;

    setParameterListenersEnabled(false);
    valueTreeState.replaceState(restoredState);
    cacheParameterPointers();
    setParameterListenersEnabled(true);
    markParametersDirty();
    syncParameters(true);
    return true;
}
