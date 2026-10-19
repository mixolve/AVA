#include "SampleProcessor.h"

#include <algorithm>
#include <cmath>

namespace dyn::dsp
{
namespace
{
constexpr double maxLookaheadMs = 30.0;
}

void DspCore::resizeLookaheadBuffers()
{
    maxBuf = std::max(1, static_cast<int>(std::ceil(currentSampleRate * maxLookaheadMs * 0.001)) + 2);

    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
        dmBase[branchIndex].assign(static_cast<size_t>(maxBuf), 0.0);

    lookaheadGain.queue.resize(static_cast<size_t>(maxBuf));
    dryL.assign(static_cast<size_t>(maxBuf), 0.0);
    dryR.assign(static_cast<size_t>(maxBuf), 0.0);
    sidechainListenL.assign(static_cast<size_t>(maxBuf), 0.0);
    sidechainListenR.assign(static_cast<size_t>(maxBuf), 0.0);
}

void DspCore::clearState()
{
    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
        std::fill(dmBase[branchIndex].begin(), dmBase[branchIndex].end(), 0.0);

    std::fill(dryL.begin(), dryL.end(), 0.0);
    std::fill(dryR.begin(), dryR.end(), 0.0);
    std::fill(sidechainListenL.begin(), sidechainListenL.end(), 0.0);
    std::fill(sidechainListenR.begin(), sidechainListenR.end(), 0.0);

    for (auto& core : transustCores) core.reset();
    envBase = {};
    baseGainState = { 1.0, 1.0, 1.0, 1.0 };
    smoothedTensions = { 0.0, 0.0, 0.0, 0.0 };
    tensionSmoothingInitialised = false;
    feedbackInitialised = false;
    smoothedFeedback = 0.0;
    for (auto* peak : { &baseSignalPeak, &cleanSignalPeak })
    {
        peak->head = peak->count = 0;
        peak->window = std::max<size_t>(1, static_cast<size_t>(std::round(currentSampleRate * 0.001)));
        peak->sample = peak->lastCrossing = 0;
        peak->positive = true;
        peak->hasCrossing = false;
    }
    lookaheadGain.head = lookaheadGain.count = 0;
    lookaheadGain.sample = 0;
    cleanEnvPeak = { 0.0, 0.0 };
    cleanGainState = { 1.0, 1.0 };
    cleanGainTarget = { 1.0, 1.0 };
    cleanAttackStates = {};
    cleanHalfPeak = { 0.0, 0.0 };
    cleanInputPositive = { true, true };
    baseReleaseStates = {};
    cleanReleaseStates = {};
    releaseMemories.fill({});
    cleanThresholdHysteresis = {};
    tensionThresholdHysteresis = {};
    adaptiveReferenceDb = { -96.0, -96.0, -96.0, -96.0 };
    adaptiveHoldSamplesRemaining = { 0, 0, 0, 0 };
    for (auto& channelPower : adaptiveRmsPower)
        std::fill(channelPower.begin(), channelPower.end(), 0.0);
    for (auto& channelPower : detectorRmsPower) std::fill(channelPower.begin(), channelPower.end(), 0.0);
    baseRmsPowerSum = {};
    for (auto& history : baseRmsPower) std::fill(history.begin(), history.end(), 0.0);
    detectorRmsPowerSum = {};
    detectorRmsPosition = detectorRmsSampleCount = 0;
    adaptiveRmsPowerSum = { 0.0, 0.0 };
    adaptiveRmsPosition = 0;
    adaptiveRmsSampleCount = 0;
    bufPos = 0;
    bufPosDry = 0;
}

void DspCore::updateDerivedParameters()
{
    const auto sampleRate = std::max(1.0, currentSampleRate);

    derived.manualThresholds.fill(dbToAmp(roundToParameterStep(parameters.threshold)));
    derived.adaptiveAmounts.fill(roundToParameterStep(parameters.adaptive) * 0.01);
    derived.tensions.fill(roundToParameterStep(parameters.tension));
    const auto toReleaseSamples = [sampleRate] (const double milliseconds)
    {
        return milliseconds <= 0.0 ? 0 : static_cast<int>(std::round(milliseconds * 0.001 * sampleRate));
    };
    derived.autoRelease = juce::jlimit(0.0, 4.0, roundToParameterStep(parameters.autoRelease) * 0.01);
    derived.releaseDepth = juce::jlimit(0.0, 1.0, roundToParameterStep(parameters.releaseDepth) * 0.01);
    derived.releaseMemoryCoefficient = std::exp(-1.0 / (sampleRate * juce::jlimit(10.0, 2000.0, static_cast<double>(parameters.releaseMemoryMs)) * 0.001));
    derived.releaseSamples = { toReleaseSamples(roundToParameterStep(parameters.releasePeakMs)),
                               toReleaseSamples(roundToParameterStep(parameters.releaseRmsMs)) };
    derived.delaySamples = toReleaseSamples(roundToParameterStep(parameters.delayMs));
    derived.attackSamples = toReleaseSamples(roundToParameterStep(parameters.attackMs));
    derived.attackCurve = roundToParameterStep(parameters.attackCurve) * 0.01;
    derived.holdSamples = toReleaseSamples(roundToParameterStep(parameters.holdMs));
    derived.inputGain = dbToAmp(roundToParameterStep(parameters.input));
    derived.makeUpGain = dbToAmp(roundToParameterStep(parameters.makeUp));
    derived.mix = juce::jlimit(0.0, 1.0, roundToParameterStep(parameters.mix) * 0.01);
    derived.outputGain = dbToAmp(roundToParameterStep(parameters.output));
    derived.rangeDb = juce::jlimit(0.0, 96.0, roundToParameterStep(parameters.range));
    derived.minimumGain = dbToAmp(-derived.rangeDb);
    derived.rangeCurve = juce::jlimit(0.0, 1.0, roundToParameterStep(parameters.rangeCurve) * 0.01);
    derived.releaseType = juce::jlimit(0.0, 1.0, roundToParameterStep(parameters.releaseType) * 0.01);

    derived.morph = roundToParameterStep(parameters.morph) * 0.01;
    derived.progression = juce::jlimit(-1.0, 1.0, roundToParameterStep(parameters.progression) * 0.01);
    derived.progressionSpan = juce::jlimit(0.1, 96.0, roundToParameterStep(parameters.progressionSpan));
    derived.ratio = juce::jlimit(1.0, 100.0, roundToParameterStep(parameters.ratio));
    derived.releaseGainExponent = derived.ratio >= 99.995 ? 1.0 : 1.0 - 1.0 / derived.ratio;
    derived.compKneeDb = juce::jlimit(0.0, 96.0, roundToParameterStep(parameters.compKnee));
    derived.clipKneeDb = juce::jlimit(0.0, 96.0, roundToParameterStep(parameters.clipKnee));
    derived.clipLower = dbToAmp(-derived.clipKneeDb * 0.5);
    derived.clipUpper = dbToAmp(derived.clipKneeDb * 0.5);

    const auto lookaheadSamples = static_cast<int>(std::round(
        std::max(0.0, roundToParameterStep(parameters.lookaheadMs)) * 0.001 * sampleRate));

    derived.tensionFloor = dbToAmp(roundToParameterStep(parameters.tensionFloor));
    derived.tensionHysteresis = roundToParameterStep(parameters.tensionHysteresis) * 0.01;
    derived.tensionSmoothingCoefficient = std::exp(-1.0 / (sampleRate * 0.005));
    derived.releaseCurve = roundToParameterStep(parameters.releaseCurve) * 0.01;
    const auto rmsCapacity = adaptiveRmsPower[0].size();
    const auto requestedRmsSamples = static_cast<size_t>(std::max(1.0, std::round(
        sampleRate * juce::jlimit(1.0, 1000.0, static_cast<double>(parameters.adaptiveTime)) * 0.001)));
    const auto rmsWindowSamples = std::min(requestedRmsSamples, std::max<size_t>(1, rmsCapacity));
    if (rmsWindowSamples != adaptiveRmsWindowSamples)
    {
        adaptiveRmsWindowSamples = rmsWindowSamples;
        adaptiveRmsPowerSum = { 0.0, 0.0 };
        const auto historySamples = std::min(adaptiveRmsSampleCount, rmsWindowSamples);
        for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex)
            for (size_t sample = 0; sample < historySamples; ++sample)
                adaptiveRmsPowerSum[channelIndex] += adaptiveRmsPower[channelIndex][
                    (adaptiveRmsPosition + rmsCapacity - historySamples + sample) % rmsCapacity];
    }
    derived.feedback = juce::jlimit(0.0, 1.0, static_cast<double>(parameters.feedback) * 0.01);
    derived.feedbackSmoothingCoefficient = std::exp(-1.0 / (sampleRate * 0.005));
    derived.thresholdHysteresisDb = std::max(0.0, roundToParameterStep(parameters.thresholdHysteresisDb));
    derived.rmsMix = juce::jlimit(0.0, 1.0, roundToParameterStep(parameters.peakRms) * 0.01);
    derived.peakCrestDb = detector::effectiveCrest(static_cast<double>(parameters.peakCrestDb), derived.rmsMix);
    derived.peakScale = detector::peakScale(derived.peakCrestDb);
    const auto detectorCapacity = detectorRmsPower[0].size();
    const auto requestedDetectorSamples = static_cast<size_t>(std::max(1.0, std::round(
        sampleRate * juce::jlimit(1.0, 1000.0, static_cast<double>(parameters.rmsTime)) * 0.001)));
    const auto detectorWindow = std::min(requestedDetectorSamples, std::max<size_t>(1, detectorCapacity));
    if (detectorWindow != detectorRmsWindowSamples)
    {
        detectorRmsWindowSamples = detectorWindow;
        detectorRmsPowerSum = {};
        baseRmsPowerSum = {};
        const auto historySamples = std::min(detectorRmsSampleCount, detectorWindow);
        for (size_t channel = 0; channel < channelCount; ++channel)
            for (size_t sample = 0; sample < historySamples; ++sample)
            {
                const auto index = (detectorRmsPosition + detectorCapacity - historySamples + sample) % detectorCapacity;
                detectorRmsPowerSum[channel] += detectorRmsPower[channel][index];
                baseRmsPowerSum[channel] += baseRmsPower[channel][index];
            }
    }
    derived.adaptiveOffsetDb = roundToParameterStep(parameters.adaptiveOffset);
    const auto smoothingCoefficient = [&toReleaseSamples] (const double milliseconds)
    {
        const auto samples = toReleaseSamples(milliseconds);
        return samples <= 0 ? 0.0 : std::exp(-1.0 / static_cast<double>(samples));
    };
    derived.adaptiveAttackCoefficient = smoothingCoefficient(roundToParameterStep(parameters.adaptiveAttack));
    derived.adaptiveReleaseCoefficient = smoothingCoefficient(roundToParameterStep(parameters.adaptiveRelease));
    derived.adaptiveHoldSamples = toReleaseSamples(roundToParameterStep(parameters.adaptiveHold));
    derived.delta = parameters.delta;

    const auto previousLookahead = derived.lookaheadSamples;
    derived.lookaheadSamples = juce::jlimit(
        0,
        std::max(0, maxBuf - 2),
        lookaheadSamples);
    if (previousLookahead != derived.lookaheadSamples)
    {
        lookaheadGain.head = lookaheadGain.count = 0;
        lookaheadGain.sample = 0;
    }
    derived.bufferSize = derived.lookaheadSamples + 1;
    derived.dryBufferSize = derived.lookaheadSamples + 1;
    derived.latencySamples = derived.lookaheadSamples;

    derived.transientGain = parameters.transientMute ? 0.0 : dbToAmp(parameters.transientGain);
    derived.sustainGain = parameters.sustainMute ? 0.0 : dbToAmp(parameters.sustainGain);
    dyn::transust::Envelope::Parameters transustParameters;
    transustParameters.delayMs = parameters.delayMs;
    transustParameters.attackMs = parameters.attackMs;
    transustParameters.attackCurve = parameters.attackCurve;
    transustParameters.holdMs = parameters.holdMs;
    transustParameters.releaseCurve = parameters.releaseCurve;
    transustParameters.autoRelease = parameters.autoRelease;
    transustParameters.releaseMemoryMs = parameters.releaseMemoryMs;
    transustParameters.releaseDepth = parameters.releaseDepth;
    transustParameters.releaseDepthDb = static_cast<float>(std::abs(20.0 * std::log10(std::max(epsilon, derived.transientGain))
        - 20.0 * std::log10(std::max(epsilon, derived.sustainGain))));
    transustParameters.retriggerMs = parameters.retriggerMs;
    transustParameters.thresholdHysteresisDb = static_cast<float>(derived.thresholdHysteresisDb);
    transustParameters.oneShot = parameters.oneShot;
    for (size_t path = 0; path < detector::count; ++path)
    {
        transustParameters.releaseMs = path == detector::peak ? parameters.releasePeakMs : parameters.releaseRmsMs;
        transustCores[path].setParameters(transustParameters);
    }

    bufPos = wrapIndex(bufPos, derived.bufferSize);
    bufPosDry = wrapIndex(bufPosDry, derived.dryBufferSize);
}

}
