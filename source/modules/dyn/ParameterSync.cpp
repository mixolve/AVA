#include "Processor.h"

#include "../../crossover/ParameterAccess.h"

#include <algorithm>

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
}

dyn::dsp::DspCore::Parameters DynModuleProcessor::readCrossoverRangeParameters(const size_t rangeIndex) const
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
    parameters.knee = loadFloat(ParameterSlot::knee);
    parameters.peakHoldMs = loadFloat(ParameterSlot::peakHoldMs);
    parameters.lookaheadMs = loadFloat(ParameterSlot::lookahead);
    parameters.tensionFloor = loadFloat(ParameterSlot::tensionFloor);
    parameters.tensionHysteresis = loadFloat(ParameterSlot::tensionHysteresis);
    parameters.releaseForm = juce::roundToInt(loadFloat(ParameterSlot::releaseForm));
    parameters.releaseCurve = loadFloat(ParameterSlot::releaseCurve);
    parameters.adaptiveOffset = loadFloat(ParameterSlot::adaptiveOffset);
    parameters.adaptiveAttack = loadFloat(ParameterSlot::adaptiveAttack);
    parameters.adaptiveHold = loadFloat(ParameterSlot::adaptiveHold);
    parameters.adaptiveRelease = loadFloat(ParameterSlot::adaptiveRelease);
    parameters.leftUpThreshold = loadFloat(ParameterSlot::leftUpThreshold);
    parameters.leftUpAdaptive = loadFloat(ParameterSlot::leftUpAdaptive);
    parameters.leftUpTension = loadFloat(ParameterSlot::leftUpTension);
    parameters.leftUpRelease = loadFloat(ParameterSlot::leftUpRelease);
    parameters.leftUpOutput = loadFloat(ParameterSlot::leftUpOutput);
    parameters.leftDownThreshold = loadFloat(ParameterSlot::leftDownThreshold);
    parameters.leftDownAdaptive = loadFloat(ParameterSlot::leftDownAdaptive);
    parameters.leftDownTension = loadFloat(ParameterSlot::leftDownTension);
    parameters.leftDownRelease = loadFloat(ParameterSlot::leftDownRelease);
    parameters.leftDownOutput = loadFloat(ParameterSlot::leftDownOutput);
    parameters.rightUpThreshold = loadFloat(ParameterSlot::rightUpThreshold);
    parameters.rightUpAdaptive = loadFloat(ParameterSlot::rightUpAdaptive);
    parameters.rightUpTension = loadFloat(ParameterSlot::rightUpTension);
    parameters.rightUpRelease = loadFloat(ParameterSlot::rightUpRelease);
    parameters.rightUpOutput = loadFloat(ParameterSlot::rightUpOutput);
    parameters.rightDownThreshold = loadFloat(ParameterSlot::rightDownThreshold);
    parameters.rightDownAdaptive = loadFloat(ParameterSlot::rightDownAdaptive);
    parameters.rightDownTension = loadFloat(ParameterSlot::rightDownTension);
    parameters.rightDownRelease = loadFloat(ParameterSlot::rightDownRelease);
    parameters.rightDownOutput = loadFloat(ParameterSlot::rightDownOutput);
    parameters.delta = loadBool(ParameterSlot::delta);

    return parameters;
}

bool DynModuleProcessor::syncParameters(const bool force)
{
    return ava::crossover::parameters::syncRangeParameters(
        parametersDirty,
        force,
        currentRangeParameters,
        [this] (const size_t rangeIndex) { return readCrossoverRangeParameters(rangeIndex); },
        processorBank);
}
