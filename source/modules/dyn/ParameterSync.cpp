#include "../shared/TimeUnits.h"
#include "Processor.h"

#include "../../crossover/ParameterAccess.h"

#include <algorithm>
#include <cmath>

namespace
{
using dyn::parameters::numParameterSlots;
using dyn::parameters::parameterSpecs;
using dyn::parameters::toIndex;
using dyn::parameters::ParameterSlot;
}

void DynModuleProcessor::cacheParameterPointers()
{
    static_assert(numParameterSlots == parameterSpecs.size());
    ava::crossover::parameters::cacheRangeParameterPointers(valueTreeState,
                                                            rawRangeParameters,
                                                            parameterSpecs);
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        for (size_t slot = 0; slot < numParameterSlots; ++slot)
            if (dyn::parameters::hasBypass(parameterSpecs[slot]))
            {
                rawRangeBypasses[rangeIndex][slot] = valueTreeState.getRawParameterValue(
                    ava::crossover::parameters::makeRangeParameterId(rangeIndex,
                        dyn::parameters::bypassSuffix(parameterSpecs[slot]).toRawUTF8()));
                jassert(rawRangeBypasses[rangeIndex][slot] != nullptr);
            }
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        for (size_t linkIndex = 0; linkIndex < dyn::parameters::linkedParameterSlots.size(); ++linkIndex)
        {
            const auto slot = dyn::parameters::linkedParameterSlots[linkIndex];
            linkedParameters[rangeIndex][linkIndex] = valueTreeState.getParameter(
                ava::crossover::parameters::makeRangeParameterId(rangeIndex, parameterSpecs[toIndex(slot)].suffix));
            jassert(linkedParameters[rangeIndex][linkIndex] != nullptr);
            linkedParameters[rangeIndex][linkIndex + dyn::parameters::linkedParameterSlots.size()] = valueTreeState.getParameter(
                ava::crossover::parameters::makeRangeParameterId(rangeIndex,
                    dyn::parameters::bypassSuffix(parameterSpecs[toIndex(slot)]).toRawUTF8()));
            jassert(linkedParameters[rangeIndex][linkIndex + dyn::parameters::linkedParameterSlots.size()] != nullptr);
        }

}

dyn::dsp::DspCore::Parameters DynModuleProcessor::readCrossoverRangeParameters(const size_t rangeIndex, const double tempoBpm) const
{
    const auto loadFloat = [this, rangeIndex] (const ParameterSlot slot)
    {
        return loadParameter(rangeIndex, slot);
    };

    const auto loadBool = [&loadFloat] (const ParameterSlot slot)
    {
        return loadFloat(slot) >= 0.5f;
    };

    dyn::dsp::DspCore::Parameters parameters;
    parameters.morph = loadFloat(ParameterSlot::morph);
    parameters.ratio = loadFloat(ParameterSlot::ratio);
    parameters.feedback = loadFloat(ParameterSlot::feedback);
    parameters.peakRms = loadFloat(ParameterSlot::peakRms);
    parameters.peakCrestDb = loadFloat(ParameterSlot::peakCrest);
    parameters.rmsTime = loadFloat(ParameterSlot::rmsTime);
    parameters.compKnee = loadFloat(ParameterSlot::compKnee);
    parameters.clipKnee = loadFloat(ParameterSlot::clipKnee);
    parameters.clipEql = loadBool(ParameterSlot::clipEql);
    parameters.lookaheadMs = loadFloat(ParameterSlot::lookahead);
    parameters.lookaheadCurve = loadFloat(ParameterSlot::lookaheadCurve);
    parameters.tensionFloor = loadFloat(ParameterSlot::tensionFloor);
    parameters.tensionHysteresis = loadFloat(ParameterSlot::tensionHysteresis);
    parameters.releaseCurve = loadFloat(ParameterSlot::releaseCurve);
    parameters.rangeCurve = loadFloat(ParameterSlot::rangeCurve);
    parameters.adaptiveTime = loadFloat(ParameterSlot::adaptiveTime);
    parameters.adaptiveOffset = loadFloat(ParameterSlot::adaptiveOffset);
    parameters.threshold = loadFloat(ParameterSlot::threshold);
    parameters.thresholdHysteresisDb = loadFloat(ParameterSlot::thresholdHysteresis);
    parameters.adaptive = loadFloat(ParameterSlot::adaptive);
    parameters.tension = loadFloat(ParameterSlot::tension);
    parameters.attackMs = loadFloat(ParameterSlot::processorAttack);
    parameters.attackCurve = loadFloat(ParameterSlot::attackCurve);
    parameters.holdMs = loadFloat(ParameterSlot::processorHold);
    parameters.autoRelease = loadFloat(ParameterSlot::autoRelease);
    parameters.releaseMemoryMs = loadFloat(ParameterSlot::releaseMemory);
    parameters.releaseDepth = loadFloat(ParameterSlot::releaseDepth);
    parameters.releasePeakMs = loadFloat(ParameterSlot::processorReleasePeak);
    parameters.releaseRmsMs = loadFloat(ParameterSlot::processorReleaseRms);
    parameters.input = loadFloat(ParameterSlot::inputGain);
    parameters.makeUp = loadFloat(ParameterSlot::makeUp);
    parameters.mix = loadFloat(ParameterSlot::mix);
    parameters.range = loadFloat(ParameterSlot::range);
    parameters.output = loadFloat(ParameterSlot::output);
    parameters.sidechainListen = loadBool(ParameterSlot::sidechainListen);
    parameters.useSidechainInput = juce::roundToInt(loadFloat(ParameterSlot::sidechainChannel)) != processingInputChannel;
    const auto noteMilliseconds = [this, rangeIndex, tempoBpm, &loadFloat]
        (ParameterSlot time, ParameterSlot mode, ParameterSlot note, float milliseconds)
    {
        if (isParameterBypassed(rangeIndex, time)) return milliseconds;
        if (loadFloat(mode) < 0.5f) return milliseconds;
        return float(ava::modules::parameters::noteDurationMilliseconds(
            juce::roundToInt(loadFloat(note)), juce::roundToInt(loadFloat(mode)), tempoBpm));
    };
    parameters.attackMs = noteMilliseconds(ParameterSlot::processorAttack, ParameterSlot::attackMode, ParameterSlot::attackNote, parameters.attackMs);
    parameters.holdMs = noteMilliseconds(ParameterSlot::processorHold, ParameterSlot::holdMode, ParameterSlot::holdNote, parameters.holdMs);
    parameters.releasePeakMs = noteMilliseconds(ParameterSlot::processorReleasePeak, ParameterSlot::releasePeakMode, ParameterSlot::releasePeakNote, parameters.releasePeakMs);
    parameters.releaseRmsMs = noteMilliseconds(ParameterSlot::processorReleaseRms, ParameterSlot::releaseRmsMode, ParameterSlot::releaseRmsNote, parameters.releaseRmsMs);
    parameters.delta = loadBool(ParameterSlot::delta);
    parameters.transientSustain = isTransientSustainRange(rangeIndex);
    parameters.transientGain = loadFloat(ParameterSlot::transientGain);
    parameters.sustainGain = loadFloat(ParameterSlot::sustainGain);
    parameters.transientMute = loadBool(ParameterSlot::transientMute);
    parameters.sustainMute = loadBool(ParameterSlot::sustainMute);
    parameters.retriggerMs = loadFloat(ParameterSlot::retrigger);
    parameters.oneShot = loadBool(ParameterSlot::oneShot);

    return parameters;
}

double DynModuleProcessor::readHostBpm() const noexcept
{
    if (auto* playHead = ownerProcessor.getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm(); bpm && std::isfinite(*bpm) && *bpm > 0)
                return juce::jlimit(1.0, 1000.0, *bpm);
    return hostTempoBpm.load();
}

bool DynModuleProcessor::syncParameters(const bool force, const bool readHostTempo)
{
    // The host permits transport queries only during audio processing.
    if (readHostTempo) hostTempoBpm.store(readHostBpm());
    const auto bpm = getTempoBpm();
    const auto tempoChanged = !juce::approximatelyEqual(bpm, cachedTempoBpm);
    cachedTempoBpm = bpm;
    return ava::crossover::parameters::syncRangeParameters(
        parametersDirty,
        force || tempoChanged,
        currentRangeParameters,
        [this, bpm] (const size_t rangeIndex) { return readCrossoverRangeParameters(rangeIndex, bpm); },
        processorBank);
}

void DynModuleProcessor::setParameterListenersEnabled(const bool enabled)
{
    ava::crossover::parameters::setRangeParameterListenersEnabled(valueTreeState, *this,
                                                                  dyn::parameters::parameterSpecs, numRanges, enabled);
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        for (const auto& spec : dyn::parameters::parameterSpecs)
            if (dyn::parameters::hasBypass(spec))
            {
                const auto id = ava::crossover::parameters::makeRangeParameterId(rangeIndex,
                    dyn::parameters::bypassSuffix(spec).toRawUTF8());
                if (enabled) valueTreeState.addParameterListener(id, this);
                else valueTreeState.removeParameterListener(id, this);
            }
}

void DynModuleProcessor::parameterChanged(const juce::String& parameterID, const float newValue)
{
    if (linkedParameterUpdateInProgress.exchange(true, std::memory_order_acq_rel))
    {
        markParametersDirty();
        return;
    }

    for (const auto& range : linkedParameters)
        for (size_t linkIndex = 0; linkIndex < range.size(); ++linkIndex)
        {
            auto* source = range[linkIndex];
            if (source == nullptr || source->paramID != parameterID)
                continue;

            const auto normalizedValue = source->convertTo0to1(newValue);
            for (const auto& targets : linkedParameters)
                if (auto* target = targets[linkIndex]; target != nullptr
                    && std::abs(target->getValue() - normalizedValue) > 1.0e-6f)
                    target->setValueNotifyingHost(normalizedValue);

            linkedParameterUpdateInProgress.store(false, std::memory_order_release);
            markParametersDirty();
            return;
        }

    linkedParameterUpdateInProgress.store(false, std::memory_order_release);
    markParametersDirty();
}
