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
        for (size_t linkIndex = 0; linkIndex < dyn::parameters::linkedParameterSlots.size(); ++linkIndex)
        {
            const auto slot = dyn::parameters::linkedParameterSlots[linkIndex];
            linkedParameters[rangeIndex][linkIndex] = valueTreeState.getParameter(
                ava::crossover::parameters::makeRangeParameterId(rangeIndex, parameterSpecs[toIndex(slot)].suffix));
            jassert(linkedParameters[rangeIndex][linkIndex] != nullptr);
        }

}

dyn::dsp::DspCore::Parameters DynModuleProcessor::readCrossoverRangeParameters(const size_t rangeIndex, const double hostBpm) const
{
    const auto loadFloat = [this, rangeIndex] (const ParameterSlot slot)
    {
        if (const auto* value = rawRangeParameters[rangeIndex][toIndex(slot)])
            return value->load();

        jassertfalse;
        return parameterSpecs[toIndex(slot)].defaultValue;
    };

    const auto loadBool = [&loadFloat] (const ParameterSlot slot)
    {
        return loadFloat(slot) >= 0.5f;
    };

    dyn::dsp::DspCore::Parameters parameters;
    parameters.morph = loadFloat(ParameterSlot::morph);
    parameters.ratio = loadFloat(ParameterSlot::ratio);
    parameters.progression = loadFloat(ParameterSlot::progression);
    parameters.progressionSpan = loadFloat(ParameterSlot::progressionSpan);
    parameters.feedback = loadFloat(ParameterSlot::feedback);
    parameters.peakRms = loadFloat(ParameterSlot::peakRms);
    parameters.peakCrestDb = loadFloat(ParameterSlot::peakCrest);
    parameters.rmsTime = loadFloat(ParameterSlot::rmsTime);
    parameters.compKnee = loadFloat(ParameterSlot::compKnee);
    parameters.clipKnee = loadFloat(ParameterSlot::clipKnee);
    parameters.lookaheadMs = loadFloat(ParameterSlot::lookahead);
    parameters.tensionFloor = loadFloat(ParameterSlot::tensionFloor);
    parameters.tensionHysteresis = loadFloat(ParameterSlot::tensionHysteresis);
    parameters.releaseCurve = loadFloat(ParameterSlot::releaseCurve);
    parameters.releaseType = loadFloat(ParameterSlot::releaseType);
    parameters.rangeCurve = loadFloat(ParameterSlot::rangeCurve);
    parameters.adaptiveTime = loadFloat(ParameterSlot::adaptiveTime);
    parameters.adaptiveOffset = loadFloat(ParameterSlot::adaptiveOffset);
    parameters.adaptiveAttack = loadFloat(ParameterSlot::adaptiveAttack);
    parameters.adaptiveHold = loadFloat(ParameterSlot::adaptiveHold);
    parameters.adaptiveRelease = loadFloat(ParameterSlot::adaptiveRelease);
    parameters.threshold = loadFloat(ParameterSlot::threshold);
    parameters.thresholdHysteresisDb = loadFloat(ParameterSlot::thresholdHysteresis);
    parameters.adaptive = loadFloat(ParameterSlot::adaptive);
    parameters.tension = loadFloat(ParameterSlot::tension);
    parameters.delayMs = loadFloat(ParameterSlot::processorDelay);
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
    const auto noteMilliseconds = [hostBpm, &loadFloat] (ParameterSlot mode, ParameterSlot note, float milliseconds)
    {
        if (loadFloat(mode) < 0.5f) return milliseconds;
        const auto index = static_cast<size_t>(juce::jlimit(0, 11, juce::roundToInt(loadFloat(note))));
        return static_cast<float>(240000.0 / hostBpm * dyn::parameters::timeNoteWholeFractions[index]);
    };
    parameters.attackMs = noteMilliseconds(ParameterSlot::attackMode, ParameterSlot::attackNote, parameters.attackMs);
    parameters.holdMs = noteMilliseconds(ParameterSlot::holdMode, ParameterSlot::holdNote, parameters.holdMs);
    parameters.releasePeakMs = noteMilliseconds(ParameterSlot::releasePeakMode, ParameterSlot::releasePeakNote, parameters.releasePeakMs);
    parameters.releaseRmsMs = noteMilliseconds(ParameterSlot::releaseRmsMode, ParameterSlot::releaseRmsNote, parameters.releaseRmsMs);
    parameters.delta = loadBool(ParameterSlot::delta);
    parameters.transust = isTransustRange(rangeIndex);
    parameters.transientGain = loadFloat(ParameterSlot::transientGain);
    parameters.sustainGain = loadFloat(ParameterSlot::sustainGain);
    parameters.transientMute = loadBool(ParameterSlot::transientMute);
    parameters.sustainMute = loadBool(ParameterSlot::sustainMute);
    parameters.retriggerMs = loadFloat(ParameterSlot::retrigger);
    parameters.oneShot = loadBool(ParameterSlot::oneShot);

    return parameters;
}

double DynModuleProcessor::getHostBpm() const noexcept
{
    if (auto* playHead = ownerProcessor.getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm(); bpm && std::isfinite(*bpm) && *bpm > 0)
                return juce::jlimit(1.0, 1000.0, *bpm);
    return 120.0;
}

bool DynModuleProcessor::syncParameters(const bool force, const bool readHostTempo)
{
    // The host permits transport queries only during audio processing.
    const auto bpm = readHostTempo ? getHostBpm() : lastHostBpm;
    const auto tempoChanged = ! juce::approximatelyEqual(bpm, lastHostBpm);
    lastHostBpm = bpm;
    return ava::crossover::parameters::syncRangeParameters(
        parametersDirty,
        force || tempoChanged,
        currentRangeParameters,
        [this, bpm] (const size_t rangeIndex) { return readCrossoverRangeParameters(rangeIndex, bpm); },
        processorBank);
}
