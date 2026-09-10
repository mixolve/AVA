#include "Processor.h"

#include "../../crossover/ParameterAccess.h"

#include <algorithm>

namespace
{
using tls::parameters::numParameterSlots;
using tls::parameters::parameterSpecs;
using tls::parameters::toIndex;
using tls::parameters::ParameterSlot;
} // namespace

void TlsAudioProcessor::cacheParameterPointers()
{
    static_assert(numParameterSlots == parameterSpecs.size());
    ava::crossover::parameters::cacheRangeParameterPointers(valueTreeState,
                                                            rawRangeParameters,
                                                            parameterSpecs);
}

tls::dsp::DspCore::Parameters TlsAudioProcessor::readCrossoverRangeParameters(const size_t rangeIndex) const
{
    const auto loadFloat = [this, rangeIndex] (const ParameterSlot slot)
    {
        if (const auto* value = rawRangeParameters[rangeIndex][toIndex(slot)])
            return value->load();

        jassertfalse;
        return 0.0f;
    };
    const auto loadBool = [&loadFloat] (const ParameterSlot slot)
    {
        return loadFloat(slot) >= 0.5f;
    };

    tls::dsp::DspCore::Parameters parameters;
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
    parameters.gainLOrder = juce::roundToInt(loadFloat(ParameterSlot::gainLOrder));
    parameters.gainROrder = juce::roundToInt(loadFloat(ParameterSlot::gainROrder));
    parameters.gainMidOrder = juce::roundToInt(loadFloat(ParameterSlot::gainMidOrder));
    parameters.gainSideOrder = juce::roundToInt(loadFloat(ParameterSlot::gainSideOrder));
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
    parameters.listenSs = loadBool(ParameterSlot::listenSs);
    parameters.stereoDelayMs = loadFloat(ParameterSlot::stereoDelay);
    parameters.leftDelayMs = loadFloat(ParameterSlot::leftDelay);
    parameters.rightDelayMs = loadFloat(ParameterSlot::rightDelay);
    parameters.leftPhase = loadFloat(ParameterSlot::leftPhase);
    parameters.rightPhase = loadFloat(ParameterSlot::rightPhase);

    return parameters;
}

bool TlsAudioProcessor::syncParameters(const bool force)
{
    if (! force && ! parametersDirty.exchange(false, std::memory_order_acq_rel))
        return false;

    if (force)
        parametersDirty.store(false, std::memory_order_release);

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        currentRangeParameters[rangeIndex] = readCrossoverRangeParameters(rangeIndex);

    processorBank.setRangeParameters(currentRangeParameters);
    const auto rangeLatencies = processorBank.getRangeLatencies();
    moduleLatencySamples.store(*std::max_element(rangeLatencies.begin(), rangeLatencies.end()),
                               std::memory_order_release);
    return true;
}
