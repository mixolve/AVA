#include "Processor.h"
#include "../shared/StateValidation.h"

#include "../../crossover/ParameterIds.h"
#include "../../crossover/UiStateSchema.h"

#include <array>
#include <cmath>
#include <optional>

void DynModuleProcessor::getStateInformation(juce::MemoryBlock& destData) const
{
    auto state = const_cast<juce::AudioProcessorValueTreeState&>(valueTreeState).copyState();
    if (auto stateXml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary(*stateXml, destData);
}

namespace
{

juce::ValueTree addMissingNewParameterDefaults(const juce::ValueTree& state)
{
    // Do not modify the caller's ValueTree: this validator is also invoked
    // while validating the outer plugin state before its restore begins.
    auto result = state.createCopy();
    for (size_t range = 0; range < dyn::dsp::ProcessorBank::numRanges; ++range)
    {
        // CLIP-EQL defaults to off when absent from the saved state.
        for (const char* const suffix : {"clip-qual"})
        {
            const auto id = ava::crossover::parameters::makeRangeParameterId(range, suffix);
            bool exists = false;
            for (const auto child : result)
                if (child.hasType("PARAM") && child.getProperty("id").toString() == id)
                {
                    exists = true;
                    break;
                }
            if (!exists && result.isValid())
            {
                juce::ValueTree parameter("PARAM");
                parameter.setProperty("id", id, nullptr);
                parameter.setProperty("value", 0.0f, nullptr);
                result.addChild(parameter, -1, nullptr);
            }
        }
    }
    return result;
}

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

        const auto bypassSuffix = dyn::parameters::bypassSuffix(parameterSpecs[toIndex(slot)]);
        const auto referenceBypass = ava::modules::state::readParameterPlainValue(
            state, makeRangeParameterId(0, bypassSuffix.toRawUTF8()));
        if (!referenceBypass.has_value()) return false;
        for (size_t rangeIndex = 1; rangeIndex < dyn::dsp::ProcessorBank::numRanges; ++rangeIndex)
        {
            const auto candidate = ava::modules::state::readParameterPlainValue(
                state, makeRangeParameterId(rangeIndex, bypassSuffix.toRawUTF8()));
            if (!candidate.has_value() || !approximatelyEqual(*candidate, *referenceBypass)) return false;
        }
    }

    return true;
}
}

bool DynModuleProcessor::isCurrentState(const juce::ValueTree& state,
                                        juce::AudioProcessorValueTreeState& parameters)
{
    // Accept v26 states missing only the newly introduced embedded switch.
    const auto current = ava::modules::state::completeMissingParameterDefaults(
        addMissingNewParameterDefaults(state), parameters);
    return ava::modules::state::hasValidParameterState(current, parameters)
        && crossover_ui::hasCurrentStateProperties(current, "dyn")
        && hasCurrentDynInvariants(current);
}

bool DynModuleProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    auto xmlState = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);

    if (xmlState == nullptr)
        return false;

    auto restoredState = ava::modules::state::completeMissingParameterDefaults(
        addMissingNewParameterDefaults(juce::ValueTree::fromXml(*xmlState)), valueTreeState);

    if (!isCurrentState(restoredState, valueTreeState))
        return false;

    setParameterListenersEnabled(false);
    valueTreeState.replaceState(restoredState);
    cacheParameterPointers();
    setParameterListenersEnabled(true);
    markParametersDirty();
    syncParameters(true);
    return true;
}
