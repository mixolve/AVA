#include "Processor.h"
#include "../shared/StateUtilities.h"

#include "../../crossover/ParameterIds.h"
#include "../../crossover/UiState.h"

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
    using dyn::parameters::parameterSpecs;
    using dyn::parameters::ParameterSlot;
    using dyn::parameters::toIndex;

    const auto read = [&state] (const size_t rangeIndex, const ParameterSlot slot) -> std::optional<float>
    {
        return ava::modules::state::readParameterPlainValue(
            state,
            makeRangeParameterId(rangeIndex, parameterSpecs[toIndex(slot)].suffix));
    };

    constexpr std::array globalSlots {
        ParameterSlot::morph,
        ParameterSlot::ratio,
        ParameterSlot::knee,
        ParameterSlot::peakHoldMs,
        ParameterSlot::lookahead,
        ParameterSlot::tensionFloor,
        ParameterSlot::tensionHysteresis,
        ParameterSlot::releaseForm,
        ParameterSlot::adaptiveOffset,
        ParameterSlot::adaptiveAttack,
        ParameterSlot::adaptiveHold,
        ParameterSlot::adaptiveRelease
    };

    for (const auto slot : globalSlots)
    {
        const auto reference = read(0, slot);

        if (! reference.has_value())
            return false;

        for (size_t rangeIndex = 1; rangeIndex < dyn::dsp::ProcessorBank::numRanges; ++rangeIndex)
        {
            const auto candidate = read(rangeIndex, slot);

            if (! candidate.has_value() || ! approximatelyEqual(*candidate, *reference))
                return false;
        }
    }

    const auto releaseForm = read(0, ParameterSlot::releaseForm);
    const auto releaseCurve = read(0, ParameterSlot::releaseCurve);

    if (! releaseForm.has_value() || ! releaseCurve.has_value())
        return false;

    for (size_t rangeIndex = 0; rangeIndex < dyn::dsp::ProcessorBank::numRanges; ++rangeIndex)
    {
        const auto candidate = read(rangeIndex, ParameterSlot::releaseCurve);

        if (! candidate.has_value())
            return false;

        if (*releaseForm < 0.5f)
        {
            if (! approximatelyEqual(*candidate, 0.0f))
                return false;
        }
        else if (! approximatelyEqual(*candidate, *releaseCurve))
        {
            return false;
        }
    }

    constexpr std::array fieldGroups {
        std::array { ParameterSlot::leftUpThreshold, ParameterSlot::leftDownThreshold, ParameterSlot::rightUpThreshold, ParameterSlot::rightDownThreshold },
        std::array { ParameterSlot::leftUpAdaptive, ParameterSlot::leftDownAdaptive, ParameterSlot::rightUpAdaptive, ParameterSlot::rightDownAdaptive },
        std::array { ParameterSlot::leftUpTension, ParameterSlot::leftDownTension, ParameterSlot::rightUpTension, ParameterSlot::rightDownTension },
        std::array { ParameterSlot::leftUpRelease, ParameterSlot::leftDownRelease, ParameterSlot::rightUpRelease, ParameterSlot::rightDownRelease },
        std::array { ParameterSlot::leftUpOutput, ParameterSlot::leftDownOutput, ParameterSlot::rightUpOutput, ParameterSlot::rightDownOutput }
    };

    for (size_t rangeIndex = 0; rangeIndex < dyn::dsp::ProcessorBank::numRanges; ++rangeIndex)
    {
        const auto linkLeftRight = read(rangeIndex, ParameterSlot::linkLeftRight);
        const auto linkUpDown = read(rangeIndex, ParameterSlot::linkUpDown);

        if (! linkLeftRight.has_value() || ! linkUpDown.has_value())
            return false;

        const auto linkLeftRightOn = *linkLeftRight >= 0.5f;
        const auto linkUpDownOn = *linkUpDown >= 0.5f;

        if (linkLeftRightOn && linkUpDownOn)
            return false;

        for (const auto& group : fieldGroups)
        {
            const auto leftUp = read(rangeIndex, group[0]);
            const auto leftDown = read(rangeIndex, group[1]);
            const auto rightUp = read(rangeIndex, group[2]);
            const auto rightDown = read(rangeIndex, group[3]);

            if (! leftUp.has_value() || ! leftDown.has_value() || ! rightUp.has_value() || ! rightDown.has_value())
                return false;

            if (linkLeftRightOn
                && (! approximatelyEqual(*leftDown, *leftUp)
                    || ! approximatelyEqual(*rightUp, *leftUp)
                    || ! approximatelyEqual(*rightDown, *leftUp)))
                return false;

            if (linkUpDownOn
                && (! approximatelyEqual(*leftDown, *leftUp)
                    || ! approximatelyEqual(*rightDown, *rightUp)))
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

    if (! ava::modules::state::hasExactParameterState(restoredState, valueTreeState)
        || ! crossover_ui::hasCurrentStateProperties(restoredState, "dyn")
        || ! hasCurrentDynInvariants(restoredState))
        return false;

    setParameterListenersEnabled(false);
    valueTreeState.replaceState(restoredState);
    cacheParameterPointers();
    setParameterListenersEnabled(true);
    markParametersDirty();
    syncParameters(true);
    return true;
}
