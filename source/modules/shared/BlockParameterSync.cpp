#include "BlockChainProcessor.h"

#include "../../crossover/ParameterAccess.h"

#include <algorithm>

namespace
{
using ava::modules::chain::parameters::numParameterSlots;
using ava::modules::chain::parameters::parameterSpecs;
using ava::modules::chain::parameters::toIndex;
using ava::modules::chain::parameters::ParameterSlot;
}

void BlockChainProcessor::cacheParameterPointers()
{
    static_assert(numParameterSlots == parameterSpecs.size());
    ava::crossover::parameters::cacheRangeParameterPointers(valueTreeState,
                                                            rawRangeParameters,
                                                            parameterSpecs);
    for (size_t range = 0; range < numRanges; ++range)
    {
        auto& link = delayLinks[range];
        for (size_t side = 0; side < 2; ++side)
        {
            const auto slot = side == 0 ? ParameterSlot::leftDelay : ParameterSlot::rightDelay;
            link.parameters[side] = valueTreeState.getParameter(ava::crossover::parameters::makeRangeParameterId(
                range, parameterSpecs[toIndex(slot)].suffix));
            link.previous[side].store(rawRangeParameters[range][toIndex(slot)]->load());
        }
    }
}

ava::modules::chain::dsp::DspCore::Parameters BlockChainProcessor::readCrossoverRangeParameters(const size_t rangeIndex) const
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

    ava::modules::chain::dsp::DspCore::Parameters parameters;
    parameters.gainOrder = getGainOrder(rangeIndex);
    parameters.listen.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[0]);
    parameters.listen.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[0]);
    parameters.gain.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[1]);
    parameters.gain.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[1]);
    parameters.delay.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[2]);
    parameters.delay.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[2]);
    parameters.phase.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[3]);
    parameters.phase.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[3]);
    parameters.panorama.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[4]);
    parameters.panorama.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[4]);
    parameters.shear.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[5]);
    parameters.shear.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[5]);
    parameters.balance.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[6]);
    parameters.balance.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[6]);
    parameters.ortho.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[7]);
    parameters.ortho.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[7]);
    parameters.rect.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[8]);
    parameters.rect.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[8]);
    parameters.comb.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[9]);
    parameters.comb.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[9]);
    parameters.shift.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[10]);
    parameters.shift.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[10]);
    parameters.varispeed.hidden = loadBool(ava::modules::chain::parameters::blockHiddenSlots[11]);
    parameters.varispeed.bypassed = loadBool(ava::modules::chain::parameters::blockBypassSlots[11]);
    for(size_t i=0;i<parameters.gain.gainBypass.size();++i)
        parameters.gain.gainBypass[i]=loadBool(ava::modules::chain::parameters::gainBypassSlots[i]);
    parameters.comb.combCountBypass = loadBool(ParameterSlot::combCountBypass);
    parameters.gain.gainMid = loadFloat(ParameterSlot::gainMid);
    parameters.gain.gainMidMute = loadBool(ParameterSlot::gainMidMute);
    parameters.gain.gainSide = loadFloat(ParameterSlot::gainSide);
    parameters.gain.gainSideMute = loadBool(ParameterSlot::gainSideMute);
    parameters.gain.gainL = loadFloat(ParameterSlot::gainL);
    parameters.gain.gainLMute = loadBool(ParameterSlot::gainLMute);
    parameters.gain.gainR = loadFloat(ParameterSlot::gainR);
    parameters.gain.gainRMute = loadBool(ParameterSlot::gainRMute);
    parameters.gain.gainLr = loadFloat(ParameterSlot::gainLr);
    parameters.gain.gainLrMute = loadBool(ParameterSlot::gainLrMute);
    parameters.rect.halfPositive = loadBool(ParameterSlot::halfPositive);
    parameters.rect.halfNegative = loadBool(ParameterSlot::halfNegative);
    parameters.rect.fullPositive = loadBool(ParameterSlot::fullPositive);
    parameters.rect.fullNegative = loadBool(ParameterSlot::fullNegative);
    parameters.panorama.left = loadFloat(ParameterSlot::left);
    parameters.panorama.right = loadFloat(ParameterSlot::right);
    parameters.panorama.law = loadFloat(ParameterSlot::law);
    parameters.shear.impact = loadFloat(ParameterSlot::impact);
    parameters.shear.impactToRight = loadBool(ParameterSlot::impactDirection);
    parameters.balance.mid = loadFloat(ParameterSlot::mid);
    parameters.balance.side = loadFloat(ParameterSlot::side);
    parameters.ortho.degree = loadFloat(ParameterSlot::degree);
    parameters.ortho.flipRight = loadBool(ParameterSlot::flipRight);
    parameters.ortho.flipRightBypassed = loadBool(ParameterSlot::flipRightBypass);
    parameters.listen.listenLc = loadBool(ParameterSlot::listenLc);
    parameters.listen.listenRc = loadBool(ParameterSlot::listenRc);
    parameters.listen.listenMc = loadBool(ParameterSlot::listenMc);
    parameters.listen.listenSc = loadBool(ParameterSlot::listenSc);
    parameters.listen.listenLl = loadBool(ParameterSlot::listenLl);
    parameters.listen.listenRr = loadBool(ParameterSlot::listenRr);
    parameters.listen.listenMsPlus3 = loadBool(ParameterSlot::listenMsPlus3);
    parameters.listen.listenSs = loadBool(ParameterSlot::listenSs);
    parameters.delay.stereoDelayMs = loadFloat(ParameterSlot::stereoDelay);
    parameters.delay.leftDelayMs = loadFloat(ParameterSlot::leftDelay);
    parameters.delay.rightDelayMs = loadFloat(ParameterSlot::rightDelay);
    parameters.phase.stereoPhase = loadFloat(ParameterSlot::stereoPhase);
    parameters.phase.leftPhase = loadFloat(ParameterSlot::leftPhase);
    parameters.phase.rightPhase = loadFloat(ParameterSlot::rightPhase);
    parameters.comb.combCount = juce::roundToInt(loadFloat(ParameterSlot::combCount));
    parameters.comb.combShift = loadFloat(ParameterSlot::combShift);
    parameters.comb.combShiftBypass = loadBool(ParameterSlot::combShiftBypass);
    parameters.comb.combMix = loadFloat(ParameterSlot::combMix);
    parameters.shift.shiftPitch = loadFloat(ParameterSlot::shiftPitch);
    parameters.shift.shiftFrequency = loadFloat(ParameterSlot::shiftFrequency);
    parameters.shift.shiftOptions.soloOrig = loadBool(ParameterSlot::shiftSoloOrig);
    parameters.shift.shiftOptions.soloPitched = loadBool(ParameterSlot::shiftSoloPitched);
    parameters.shift.shiftOptions.window = juce::roundToInt(loadFloat(ParameterSlot::shiftWindow));
    parameters.shift.shiftOptions.pitchMode = juce::roundToInt(loadFloat(ParameterSlot::shiftPitchMode));
    parameters.shift.shiftOptions.detector = juce::roundToInt(loadFloat(ParameterSlot::shiftDetector));
    parameters.shift.shiftOptions.transients = juce::roundToInt(loadFloat(ParameterSlot::shiftTransients));
    parameters.shift.shiftOptions.independentPhase = loadBool(ParameterSlot::shiftIndependentPhase);
    constexpr std::array<int, 5> shiftOrders {1, 2, 4, 6, 8};
    parameters.shift.shiftOptions.order = shiftOrders[size_t(juce::jlimit(0, 4, juce::roundToInt(loadFloat(ParameterSlot::shiftOrder))))];
    parameters.shift.shiftOptions.q = loadFloat(ParameterSlot::shiftQ);
    parameters.shift.shiftOptions.origGainDb = loadFloat(ParameterSlot::shiftGainOrig);
    parameters.shift.shiftOptions.pitchedGainDb = loadFloat(ParameterSlot::shiftGainPitched);
    parameters.shift.shiftOptions.origGainBypassed = loadBool(ParameterSlot::shiftGainOrigBypass);
    parameters.shift.shiftOptions.pitchedGainBypassed = loadBool(ParameterSlot::shiftGainPitchedBypass);

    parameters.varispeed.varispeed.rate = loadFloat(ParameterSlot::varispeedRate);
    parameters.varispeed.varispeed.thresholdDb = loadFloat(ParameterSlot::varispeedThreshold);
    parameters.varispeed.varispeed.retriggerMs = loadFloat(ParameterSlot::varispeedRetrigger);
    parameters.varispeed.varispeed.crossfadeMs = loadFloat(ParameterSlot::varispeedCrossfade);
    parameters.varispeed.varispeed.preservePitch = loadBool(ParameterSlot::varispeedPreserve);
    parameters.varispeed.varispeed.quality = juce::roundToInt(loadFloat(ParameterSlot::varispeedQuality));

    parameters.sibil.hidden=loadBool(ParameterSlot::sibilHidden);
    parameters.sibil.bypassed=loadBool(ParameterSlot::sibilBypass);
    parameters.sibil.soloS=loadBool(ParameterSlot::sibilSoloS);
    parameters.sibil.soloRest=loadBool(ParameterSlot::sibilSoloRest);
    parameters.sibil.level=loadFloat(ParameterSlot::sibilLevel);
    parameters.sibil.brightness=loadFloat(ParameterSlot::sibilBrightness);
    parameters.sibil.noise=loadFloat(ParameterSlot::sibilNoise);
    parameters.sibil.fade=loadFloat(ParameterSlot::sibilFade);
    parameters.sibil.edgeDetect=loadFloat(ParameterSlot::sibilEdgeDetect);
    parameters.sibil.gainS=loadFloat(ParameterSlot::sibilGainS);
    parameters.sibil.gainRest=loadFloat(ParameterSlot::sibilGainRest);
    return parameters;
}

bool BlockChainProcessor::syncParameters(const bool force)
{
    return ava::crossover::parameters::syncRangeParameters(
        parametersDirty,
        force,
        currentRangeParameters,
        [this] (const size_t rangeIndex) { return readCrossoverRangeParameters(rangeIndex); },
        processorBank);
}
