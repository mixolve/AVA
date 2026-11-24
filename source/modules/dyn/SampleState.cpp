#include "SampleProcessor.h"
#include "TensionTransfer.h"
#include "ClipKneeScale.h"
#include "Parameters.h"

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

    for (auto& core : transientSustainCores) core.reset();
    for (auto& window : compressorHoldWindows) window.reset();
    lookaheadGain.reset();
    tensionPreviewWindow.reset();
    for (auto& shaper : lookaheadTension) shaper.reset();
    attackSourceGate.reset();
    attackSourceQuiet = false;
    envBase = {};
    cleanEnvPeak = { 0.0, 0.0 };
    cleanGainState = { 1.0, 1.0 };
    cleanGainTarget = { 1.0, 1.0 };
    cleanAttackStates = {};
    baseReleaseStates = {};
    cleanReleaseStates = {};
    releaseMemories.fill({});
    cleanThresholdHysteresis = {};
    tensionThresholdHysteresis = {};
    adaptiveReferenceDb = { -96.0, -96.0, -96.0, -96.0 };
    // The cumulative ring is logically empty once the counter is reset.
    // Avoid clearing a 10-second buffer inside the audio callback.
    for (auto& channelPower : detectorRmsPower) std::fill(channelPower.begin(), channelPower.end(), 0.0);
    baseRmsPowerSum = {};
    for (auto& history : baseRmsPower) std::fill(history.begin(), history.end(), 0.0);
    detectorRmsPowerSum = {};
    detectorRmsPosition = detectorRmsSampleCount = 0;
    adaptiveRmsCumulativePower = { 0.0, 0.0 };
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

    derived.morph = roundToParameterStep(parameters.morph) * 0.01;
    derived.ratio = juce::jlimit(1.0, 100.0, roundToParameterStep(parameters.ratio));
    derived.releaseGainExponent = derived.ratio >= 99.995 ? 1.0 : 1.0 - 1.0 / derived.ratio;
    derived.compKneeDb = juce::jlimit(0.0, 96.0, roundToParameterStep(parameters.compKnee));
    const auto clipKneeControlDb = juce::jlimit(0.0, 96.0, roundToParameterStep(parameters.clipKnee));
    derived.clipKneeDb = parameters.clipEql
        ? dyn_clip_knee::scaleEqualizedKneeDb(clipKneeControlDb)
        : clipKneeControlDb;
    derived.clipLower = dbToAmp(-derived.clipKneeDb * 0.5);
    derived.clipUpper = dbToAmp(derived.clipKneeDb * 0.5);
    // EQL anchors the soft-knee transfer at the existing clip ceiling.
    // There is no post-clipping makeup gain to overshoot the threshold.
    derived.clipEql = parameters.clipEql;

    const auto lookaheadSamples = static_cast<int>(std::round(
        std::max(0.0, roundToParameterStep(parameters.lookaheadMs)) * 0.001 * sampleRate));

    derived.tensionFloor = dbToAmp(roundToParameterStep(parameters.tensionFloor));
    derived.tensionHysteresis = dyn_tension::scaleHysteresis(
        roundToParameterStep(parameters.tensionHysteresis) * 0.01);
    derived.releaseCurve = roundToParameterStep(parameters.releaseCurve) * 0.01;
    const auto rmsCapacity = adaptiveRmsCumulativePowerHistory[0].size();
    const auto requestedRmsSamples = static_cast<size_t>(std::max(1.0, std::round(
        sampleRate * juce::jlimit(50.0, 10000.0, static_cast<double>(parameters.adaptiveTime)) * 0.001)));
    // Cumulative energies allow instant window changes without rescanning
    // up to 10 seconds of samples on the realtime audio thread.
    adaptiveRmsWindowSamples = std::min(requestedRmsSamples, std::max<size_t>(1, rmsCapacity));
    derived.feedback = juce::jlimit(0.0, 1.0, static_cast<double>(parameters.feedback) * 0.01);
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
    derived.delta = parameters.delta;

    derived.lookaheadSamples = juce::jlimit(0, std::max(0, maxBuf - 2), lookaheadSamples);
    derived.lookaheadCurve = juce::jlimit(-1.0, 1.0, roundToParameterStep(parameters.lookaheadCurve) * 0.01);
    // These are physical storage capacities, not the user's delay setting.
    derived.bufferSize = maxBuf;
    derived.dryBufferSize = maxBuf;
    derived.latencySamples = derived.lookaheadSamples;

    derived.transientGain = ava::modules::dsp::mutedGainToAmp(parameters.transientGain, parameters.transientMute);
    derived.sustainGain = ava::modules::dsp::mutedGainToAmp(parameters.sustainGain, parameters.sustainMute);
    dyn::transientSustain::Envelope::Parameters transientSustainParameters;
    transientSustainParameters.attackMs = parameters.attackMs;
    transientSustainParameters.attackCurve = parameters.attackCurve;
    transientSustainParameters.holdMs = parameters.holdMs;
    transientSustainParameters.releaseCurve = parameters.releaseCurve;
    transientSustainParameters.autoRelease = parameters.autoRelease;
    transientSustainParameters.releaseMemoryMs = parameters.releaseMemoryMs;
    transientSustainParameters.releaseDepth = parameters.releaseDepth;
    transientSustainParameters.releaseDepthDb = static_cast<float>(std::abs(20.0 * std::log10(std::max(epsilon, derived.transientGain))
        - 20.0 * std::log10(std::max(epsilon, derived.sustainGain))));
    transientSustainParameters.retriggerMs = parameters.retriggerMs;
    transientSustainParameters.thresholdHysteresisDb = static_cast<float>(derived.thresholdHysteresisDb);
    transientSustainParameters.oneShot = parameters.oneShot;
    for (size_t path = 0; path < detector::count; ++path)
    {
        transientSustainParameters.releaseMs = path == detector::peak ? parameters.releasePeakMs : parameters.releaseRmsMs;
        transientSustainCores[path].setParameters(transientSustainParameters);
    }

    bufPos = wrapIndex(bufPos, maxBuf);
    bufPosDry = wrapIndex(bufPosDry, maxBuf);
}

}
