#include "Processor.h"

#include "../../crossover/ParameterAccess.h"

#include <algorithm>

namespace
{
using tls::parameters::numParameterSlots;
using tls::parameters::parameterSpecs;
using tls::parameters::toIndex;
using tls::parameters::ParameterSlot;
}

void TlsModuleProcessor::cacheParameterPointers()
{
    static_assert(numParameterSlots == parameterSpecs.size());
    ava::crossover::parameters::cacheRangeParameterPointers(valueTreeState,
                                                            rawRangeParameters,
                                                            parameterSpecs);
}

tls::dsp::DspCore::Parameters TlsModuleProcessor::readCrossoverRangeParameters(const size_t rangeIndex) const
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

    tls::dsp::DspCore::Parameters parameters;
    parameters.blockOrder = getBlockOrder(rangeIndex);
    parameters.gainOrder = getGainOrder(rangeIndex);
    for(size_t i=0;i<parameters.blockHidden.size();++i)
        parameters.blockHidden[i]=loadBool(tls::parameters::blockHiddenSlots[i]);
    for(size_t i=0;i<parameters.blockBypass.size();++i)
        parameters.blockBypass[i]=loadBool(tls::parameters::blockBypassSlots[i]);
    for(size_t i=0;i<parameters.gainBypass.size();++i)
        parameters.gainBypass[i]=loadBool(tls::parameters::gainBypassSlots[i]);
    parameters.combCountBypass = loadBool(ParameterSlot::combCountBypass);
    parameters.gainMid = loadFloat(ParameterSlot::gainMid);
    parameters.gainMidMute = loadBool(ParameterSlot::gainMidMute);
    parameters.gainSide = loadFloat(ParameterSlot::gainSide);
    parameters.gainSideMute = loadBool(ParameterSlot::gainSideMute);
    parameters.gainL = loadFloat(ParameterSlot::gainL);
    parameters.gainLMute = loadBool(ParameterSlot::gainLMute);
    parameters.gainR = loadFloat(ParameterSlot::gainR);
    parameters.gainRMute = loadBool(ParameterSlot::gainRMute);
    parameters.gainLr = loadFloat(ParameterSlot::gainLr);
    parameters.gainLrMute = loadBool(ParameterSlot::gainLrMute);
    parameters.halfPositive = loadBool(ParameterSlot::halfPositive);
    parameters.halfNegative = loadBool(ParameterSlot::halfNegative);
    parameters.fullPositive = loadBool(ParameterSlot::fullPositive);
    parameters.fullNegative = loadBool(ParameterSlot::fullNegative);
    parameters.left = loadFloat(ParameterSlot::left);
    parameters.right = loadFloat(ParameterSlot::right);
    parameters.law = loadFloat(ParameterSlot::law);
    parameters.impact = loadFloat(ParameterSlot::impact);
    parameters.impactToRight = loadBool(ParameterSlot::impactDirection);
    parameters.mid = loadFloat(ParameterSlot::mid);
    parameters.side = loadFloat(ParameterSlot::side);
    parameters.degree = loadFloat(ParameterSlot::degree);
    parameters.flipRight = loadBool(ParameterSlot::flipRight);
    parameters.listenLc = loadBool(ParameterSlot::listenLc);
    parameters.listenRc = loadBool(ParameterSlot::listenRc);
    parameters.listenMc = loadBool(ParameterSlot::listenMc);
    parameters.listenSc = loadBool(ParameterSlot::listenSc);
    parameters.listenLl = loadBool(ParameterSlot::listenLl);
    parameters.listenRr = loadBool(ParameterSlot::listenRr);
    parameters.listenMsPlus3 = loadBool(ParameterSlot::listenMsPlus3);
    parameters.listenSs = loadBool(ParameterSlot::listenSs);
    parameters.stereoDelayMs = loadFloat(ParameterSlot::stereoDelay);
    parameters.leftDelayMs = loadFloat(ParameterSlot::leftDelay);
    parameters.rightDelayMs = loadFloat(ParameterSlot::rightDelay);
    parameters.stereoPhase = loadFloat(ParameterSlot::stereoPhase);
    parameters.leftPhase = loadFloat(ParameterSlot::leftPhase);
    parameters.rightPhase = loadFloat(ParameterSlot::rightPhase);
    parameters.combCount = juce::roundToInt(loadFloat(ParameterSlot::combCount));
    parameters.combShift = loadFloat(ParameterSlot::combShift);
    parameters.combShiftBypass = loadBool(ParameterSlot::combShiftBypass);

    return parameters;
}

bool TlsModuleProcessor::syncParameters(const bool force)
{
    return ava::crossover::parameters::syncRangeParameters(
        parametersDirty,
        force,
        currentRangeParameters,
        [this] (const size_t rangeIndex) { return readCrossoverRangeParameters(rangeIndex); },
        processorBank);
}
