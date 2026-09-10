#include "Processor.h"

#include "../../crossover/ParameterAccess.h"

#include <array>
#include <cmath>
#include <optional>

void DynAudioProcessor::setRangeParameterValue(const size_t rangeIndex,
                                                const dyn::parameters::ParameterSlot targetSlot,
                                                const float targetValue)
{
    using ava::crossover::parameters::makeRangeParameterId;
    using dyn::parameters::parameterSpecs;
    using dyn::parameters::toIndex;

    const auto targetIndex = toIndex(targetSlot);
    auto* target = dynamic_cast<juce::RangedAudioParameter*>(valueTreeState.getParameter(
        makeRangeParameterId(rangeIndex, parameterSpecs[targetIndex].suffix)));

    if (target == nullptr)
        return;

    const auto normalizedValue = target->convertTo0to1(targetValue);

    if (std::abs(target->getValue() - normalizedValue) <= 1.0e-6f)
        return;

    target->setValueNotifyingHost(normalizedValue);
}

float DynAudioProcessor::readRangeParameterValue(const size_t rangeIndex,
                                                  const dyn::parameters::ParameterSlot slot) const noexcept
{
    const auto* value = rawRangeParameters[rangeIndex][dyn::parameters::toIndex(slot)];
    return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f;
}

void DynAudioProcessor::syncAllFieldParameters(const size_t rangeIndex,
                                                const dyn::parameters::ParameterSlot leftUp,
                                                const dyn::parameters::ParameterSlot leftDown,
                                                const dyn::parameters::ParameterSlot rightUp,
                                                const dyn::parameters::ParameterSlot rightDown)
{
    const auto value = readRangeParameterValue(rangeIndex, leftUp);
    setRangeParameterValue(rangeIndex, leftUp, value);
    setRangeParameterValue(rangeIndex, leftDown, value);
    setRangeParameterValue(rangeIndex, rightUp, value);
    setRangeParameterValue(rangeIndex, rightDown, value);
}

void DynAudioProcessor::syncUpDownParameterPairs(const size_t rangeIndex,
                                                  const dyn::parameters::ParameterSlot leftUp,
                                                  const dyn::parameters::ParameterSlot leftDown,
                                                  const dyn::parameters::ParameterSlot rightUp,
                                                  const dyn::parameters::ParameterSlot rightDown)
{
    const auto leftValue = readRangeParameterValue(rangeIndex, leftUp);
    const auto rightValue = readRangeParameterValue(rangeIndex, rightUp);
    setRangeParameterValue(rangeIndex, leftUp, leftValue);
    setRangeParameterValue(rangeIndex, leftDown, leftValue);
    setRangeParameterValue(rangeIndex, rightUp, rightValue);
    setRangeParameterValue(rangeIndex, rightDown, rightValue);
}

void DynAudioProcessor::setParameterListenersEnabled(const bool enabled)
{
    ava::crossover::parameters::setRangeParameterListenersEnabled(valueTreeState,
                                                                  *this,
                                                                  dyn::parameters::parameterSpecs,
                                                                  numRanges,
                                                                  enabled);
}

void DynAudioProcessor::parameterChanged(const juce::String& parameterID, float)
{
    using ava::crossover::parameters::makeRangeParameterId;
    using dyn::parameters::parameterSpecs;
    using dyn::parameters::ParameterSlot;
    using dyn::parameters::toIndex;

    if (linkedParameterPropagationInProgress.exchange(true, std::memory_order_acq_rel))
    {
        markParametersDirty();
        return;
    }

    const auto parseSlotForCrossoverRange = [&] (const size_t rangeIndex, const juce::String& id) -> std::optional<ParameterSlot>
    {
        for (size_t slotIndex = 0; slotIndex < numParameterSlots; ++slotIndex)
        {
            const auto slot = static_cast<ParameterSlot>(slotIndex);

            if (id == makeRangeParameterId(rangeIndex, parameterSpecs[slotIndex].suffix))
                return slot;
        }

        return std::nullopt;
    };

    const auto branchIndexFromSlot = [] (const ParameterSlot slot) -> int
    {
        if (slot == ParameterSlot::leftUpThreshold || slot == ParameterSlot::leftUpAdaptive || slot == ParameterSlot::leftUpTension || slot == ParameterSlot::leftUpRelease || slot == ParameterSlot::leftUpOutput)
            return 0;

        if (slot == ParameterSlot::leftDownThreshold || slot == ParameterSlot::leftDownAdaptive || slot == ParameterSlot::leftDownTension || slot == ParameterSlot::leftDownRelease || slot == ParameterSlot::leftDownOutput)
            return 1;

        if (slot == ParameterSlot::rightUpThreshold || slot == ParameterSlot::rightUpAdaptive || slot == ParameterSlot::rightUpTension || slot == ParameterSlot::rightUpRelease || slot == ParameterSlot::rightUpOutput)
            return 2;

        if (slot == ParameterSlot::rightDownThreshold || slot == ParameterSlot::rightDownAdaptive || slot == ParameterSlot::rightDownTension || slot == ParameterSlot::rightDownRelease || slot == ParameterSlot::rightDownOutput)
            return 3;

        return -1;
    };

    const auto fieldSlotsFor = [] (const ParameterSlot slot) -> std::array<ParameterSlot, 4>
    {
        if (slot == ParameterSlot::leftUpThreshold || slot == ParameterSlot::leftDownThreshold || slot == ParameterSlot::rightUpThreshold || slot == ParameterSlot::rightDownThreshold)
            return { ParameterSlot::leftUpThreshold, ParameterSlot::leftDownThreshold, ParameterSlot::rightUpThreshold, ParameterSlot::rightDownThreshold };

        if (slot == ParameterSlot::leftUpAdaptive || slot == ParameterSlot::leftDownAdaptive || slot == ParameterSlot::rightUpAdaptive || slot == ParameterSlot::rightDownAdaptive)
            return { ParameterSlot::leftUpAdaptive, ParameterSlot::leftDownAdaptive, ParameterSlot::rightUpAdaptive, ParameterSlot::rightDownAdaptive };

        if (slot == ParameterSlot::leftUpTension || slot == ParameterSlot::leftDownTension || slot == ParameterSlot::rightUpTension || slot == ParameterSlot::rightDownTension)
            return { ParameterSlot::leftUpTension, ParameterSlot::leftDownTension, ParameterSlot::rightUpTension, ParameterSlot::rightDownTension };

        if (slot == ParameterSlot::leftUpRelease || slot == ParameterSlot::leftDownRelease || slot == ParameterSlot::rightUpRelease || slot == ParameterSlot::rightDownRelease)
            return { ParameterSlot::leftUpRelease, ParameterSlot::leftDownRelease, ParameterSlot::rightUpRelease, ParameterSlot::rightDownRelease };

        if (slot == ParameterSlot::leftUpOutput || slot == ParameterSlot::leftDownOutput || slot == ParameterSlot::rightUpOutput || slot == ParameterSlot::rightDownOutput)
            return { ParameterSlot::leftUpOutput, ParameterSlot::leftDownOutput, ParameterSlot::rightUpOutput, ParameterSlot::rightDownOutput };

        return { slot, slot, slot, slot };
    };

    const auto outSlotForThresholdSlot = [] (const ParameterSlot slot) -> std::optional<ParameterSlot>
    {
        if (slot == ParameterSlot::leftUpThreshold)
            return ParameterSlot::leftUpOutput;

        if (slot == ParameterSlot::leftDownThreshold)
            return ParameterSlot::leftDownOutput;

        if (slot == ParameterSlot::rightUpThreshold)
            return ParameterSlot::rightUpOutput;

        if (slot == ParameterSlot::rightDownThreshold)
            return ParameterSlot::rightDownOutput;

        return std::nullopt;
    };

    const auto isThresholdSlot = [] (const ParameterSlot slot)
    {
        return slot == ParameterSlot::leftUpThreshold
            || slot == ParameterSlot::leftDownThreshold
            || slot == ParameterSlot::rightUpThreshold
            || slot == ParameterSlot::rightDownThreshold;
    };

    const auto thresholdScopeFor = [&branchIndexFromSlot] (const ParameterSlot sourceSlot,
                                                           const bool linkUpDownActive,
                                                           const bool linkLeftRightActive)
    {
        std::array<ParameterSlot, 4> scope { sourceSlot, sourceSlot, sourceSlot, sourceSlot };
        size_t scopeSize = 1;

        if (linkLeftRightActive)
        {
            scope = { ParameterSlot::leftUpThreshold, ParameterSlot::leftDownThreshold, ParameterSlot::rightUpThreshold, ParameterSlot::rightDownThreshold };
            scopeSize = 4;
            return std::pair { scope, scopeSize };
        }

        if (linkUpDownActive)
        {
            const auto sourceBranch = branchIndexFromSlot(sourceSlot);

            if (sourceBranch < 0)
                return std::pair { scope, scopeSize };

            if (sourceBranch < 2)
                scope = { ParameterSlot::leftUpThreshold, ParameterSlot::leftDownThreshold, sourceSlot, sourceSlot };
            else
                scope = { ParameterSlot::rightUpThreshold, ParameterSlot::rightDownThreshold, sourceSlot, sourceSlot };

            scopeSize = 2;
            return std::pair { scope, scopeSize };
        }

        return std::pair { scope, scopeSize };
    };

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto linkLeftRightActive = rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkLeftRight)] != nullptr
            && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkLeftRight)]->load(std::memory_order_relaxed) >= 0.5f;
        auto linkUpDownActive = rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkUpDown)] != nullptr
            && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkUpDown)]->load(std::memory_order_relaxed) >= 0.5f;
        const auto linkOppositeActive = rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkOpposite)] != nullptr
            && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkOpposite)]->load(std::memory_order_relaxed) >= 0.5f;

        const auto slot = parseSlotForCrossoverRange(rangeIndex, parameterID);

        if (! slot.has_value())
            continue;

        const auto sourceSlot = *slot;

        const auto isGlobalMainSlot = [] (const ParameterSlot slotToCheck)
        {
            return slotToCheck == ParameterSlot::morph
                || slotToCheck == ParameterSlot::ratio
                || slotToCheck == ParameterSlot::knee
                || slotToCheck == ParameterSlot::peakHoldMs
                || slotToCheck == ParameterSlot::lookahead
                || slotToCheck == ParameterSlot::tensionFloor
                || slotToCheck == ParameterSlot::tensionHysteresis
                || slotToCheck == ParameterSlot::releaseForm
                || slotToCheck == ParameterSlot::releaseCurve
                || slotToCheck == ParameterSlot::adaptiveOffset
                || slotToCheck == ParameterSlot::adaptiveAttack
                || slotToCheck == ParameterSlot::adaptiveHold
                || slotToCheck == ParameterSlot::adaptiveRelease;
        };

        if (isGlobalMainSlot(sourceSlot))
        {
            const auto* source = rawRangeParameters[rangeIndex][toIndex(sourceSlot)];

            if (source != nullptr)
            {
                const auto sourceValue = source->load(std::memory_order_relaxed);

                for (size_t targetRange = 0; targetRange < numRanges; ++targetRange)
                    setRangeParameterValue(targetRange, sourceSlot, sourceValue);

                if (sourceSlot == ParameterSlot::releaseForm && sourceValue < 0.5f)
                {
                    for (size_t targetRange = 0; targetRange < numRanges; ++targetRange)
                        setRangeParameterValue(targetRange, ParameterSlot::releaseCurve, 0.0f);
                }
            }

            continue;
        }

        if (sourceSlot == ParameterSlot::linkUpDown && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkUpDown)] != nullptr)
        {
            if (rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkUpDown)]->load(std::memory_order_relaxed) >= 0.5f
                && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkLeftRight)] != nullptr)
                setRangeParameterValue(rangeIndex, ParameterSlot::linkLeftRight, 0.0f);
        }
        else if (sourceSlot == ParameterSlot::linkLeftRight && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkLeftRight)] != nullptr)
        {
            if (rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkLeftRight)]->load(std::memory_order_relaxed) >= 0.5f
                && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkUpDown)] != nullptr)
                setRangeParameterValue(rangeIndex, ParameterSlot::linkUpDown, 0.0f);
        }

        linkLeftRightActive = rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkLeftRight)] != nullptr
            && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkLeftRight)]->load(std::memory_order_relaxed) >= 0.5f;
        linkUpDownActive = rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkUpDown)] != nullptr
            && rawRangeParameters[rangeIndex][toIndex(ParameterSlot::linkUpDown)]->load(std::memory_order_relaxed) >= 0.5f;

        if (sourceSlot == ParameterSlot::linkLeftRight && linkLeftRightActive)
        {
            syncAllFieldParameters(rangeIndex, ParameterSlot::leftUpThreshold, ParameterSlot::leftDownThreshold, ParameterSlot::rightUpThreshold, ParameterSlot::rightDownThreshold);
            syncAllFieldParameters(rangeIndex, ParameterSlot::leftUpAdaptive, ParameterSlot::leftDownAdaptive, ParameterSlot::rightUpAdaptive, ParameterSlot::rightDownAdaptive);
            syncAllFieldParameters(rangeIndex, ParameterSlot::leftUpTension, ParameterSlot::leftDownTension, ParameterSlot::rightUpTension, ParameterSlot::rightDownTension);
            syncAllFieldParameters(rangeIndex, ParameterSlot::leftUpRelease, ParameterSlot::leftDownRelease, ParameterSlot::rightUpRelease, ParameterSlot::rightDownRelease);
            syncAllFieldParameters(rangeIndex, ParameterSlot::leftUpOutput, ParameterSlot::leftDownOutput, ParameterSlot::rightUpOutput, ParameterSlot::rightDownOutput);
            continue;
        }

        if (sourceSlot == ParameterSlot::linkUpDown && linkUpDownActive)
        {
            syncUpDownParameterPairs(rangeIndex, ParameterSlot::leftUpThreshold, ParameterSlot::leftDownThreshold, ParameterSlot::rightUpThreshold, ParameterSlot::rightDownThreshold);
            syncUpDownParameterPairs(rangeIndex, ParameterSlot::leftUpAdaptive, ParameterSlot::leftDownAdaptive, ParameterSlot::rightUpAdaptive, ParameterSlot::rightDownAdaptive);
            syncUpDownParameterPairs(rangeIndex, ParameterSlot::leftUpTension, ParameterSlot::leftDownTension, ParameterSlot::rightUpTension, ParameterSlot::rightDownTension);
            syncUpDownParameterPairs(rangeIndex, ParameterSlot::leftUpRelease, ParameterSlot::leftDownRelease, ParameterSlot::rightUpRelease, ParameterSlot::rightDownRelease);
            syncUpDownParameterPairs(rangeIndex, ParameterSlot::leftUpOutput, ParameterSlot::leftDownOutput, ParameterSlot::rightUpOutput, ParameterSlot::rightDownOutput);
            continue;
        }

        if (isThresholdSlot(sourceSlot))
        {
            const auto* source = rawRangeParameters[rangeIndex][toIndex(sourceSlot)];

            if (source == nullptr)
                continue;

            const auto sourceValue = source->load(std::memory_order_relaxed);
            const auto [thresholdSlots, thresholdSlotCount] = thresholdScopeFor(sourceSlot, linkUpDownActive, linkLeftRightActive);

            for (size_t thresholdIndex = 0; thresholdIndex < thresholdSlotCount; ++thresholdIndex)
            {
                const auto thresholdSlot = thresholdSlots[thresholdIndex];
                setRangeParameterValue(rangeIndex, thresholdSlot, sourceValue);
            }

            if (linkOppositeActive)
            {
                for (size_t thresholdIndex = 0; thresholdIndex < thresholdSlotCount; ++thresholdIndex)
                {
                    const auto thresholdSlot = thresholdSlots[thresholdIndex];
                    const auto* thresholdParam = rawRangeParameters[rangeIndex][toIndex(thresholdSlot)];

                    if (thresholdParam == nullptr)
                        continue;

                    const auto compensatedOut = juce::jlimit(-96.0f, 96.0f, -thresholdParam->load(std::memory_order_relaxed));
                    const auto outSlot = outSlotForThresholdSlot(thresholdSlot);

                    if (outSlot.has_value())
                        setRangeParameterValue(rangeIndex, *outSlot, compensatedOut);
                }
            }

            continue;
        }

        const auto sourceBranch = branchIndexFromSlot(sourceSlot);

        if (sourceBranch < 0)
            continue;

        const auto fieldSlots = fieldSlotsFor(sourceSlot);
        const auto* source = rawRangeParameters[rangeIndex][toIndex(sourceSlot)];

        if (source == nullptr)
            continue;

        const auto sourceValue = source->load(std::memory_order_relaxed);

        for (const auto targetSlot : fieldSlots)
        {
            const auto targetBranch = branchIndexFromSlot(targetSlot);

            if (targetBranch < 0)
                continue;

            const auto sameSide = (sourceBranch < 2) == (targetBranch < 2);
            const auto connected = sourceBranch == targetBranch
                || linkLeftRightActive
                || (linkUpDownActive && sameSide);

            if (connected)
                setRangeParameterValue(rangeIndex, targetSlot, sourceValue);
        }
    }

    linkedParameterPropagationInProgress.store(false, std::memory_order_release);
    markParametersDirty();
}
