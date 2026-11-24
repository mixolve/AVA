#include "SampleProcessor.h"
#include "TensionTransfer.h"

#include <algorithm>
#include <cmath>

namespace dyn::dsp
{
namespace
{
double amplitudeToDb(const double amplitude)
{
    return 20.0 * std::log10(std::max(amplitude, epsilon));
}

double tensionTargetDb(const double env,
                       const double threshold,
                       const double floorThreshold,
                       const double amount,
                       const bool mirrored)
{
    const auto floorDb = amplitudeToDb(floorThreshold);
    const auto thresholdDb = amplitudeToDb(threshold);
    const auto spanDb = thresholdDb - floorDb;

    if (spanDb <= epsilon)
        return threshold;

    const auto ratio = juce::jlimit(0.0, 1.0, (amplitudeToDb(env) - floorDb) / spanDb);
    const auto shapedRatio = dyn_tension::shapePosition(ratio, amount, mirrored);
    return dbToAmp(floorDb + spanDb * shapedRatio);
}
}

double DspCore::satShape(const double value, const double referenceLevel) const
{
    if (derived.clipKneeDb <= epsilon)
        return juce::jlimit(-1.0, 1.0, value);

    const auto softMagnitude = [this] (const double level)
    {
        if (level <= derived.clipLower) return level;
        if (level >= derived.clipUpper) return 1.0;
        const auto levelDb = amplitudeToDb(level);
        const auto distanceDb = levelDb + derived.clipKneeDb * 0.5;
        const auto reductionDb = distanceDb * distanceDb / (2.0 * derived.clipKneeDb);
        return dbToAmp(levelDb - reductionDb);
    };

    const auto sign = value < 0.0 ? -1.0 : 1.0;
    const auto shaped = softMagnitude(std::abs(value));
    if (!derived.clipEql)
        return sign * shaped;

    // EQL is peak-reference makeup, not another clipping curve. Calibrate
    // against a 0-dBFS input sample (after user INPUT gain), so at the
    // reference amplitude the soft-knee peak matches the hard-clip peak.
    // It is memoryless: no extra envelope, fixed time window or latency.
    // The hard ceiling is only a safety cap for samples ABOVE the reference;
    // it must not flatten the knee at the ordinary clip threshold.
    const auto reference = std::max(epsilon, std::abs(referenceLevel));
    const auto referenceOutput = std::min(1.0, reference);
    const auto referenceSoft = softMagnitude(reference);
    const auto compensation = referenceSoft > epsilon
        ? referenceOutput / referenceSoft : 1.0;
    return sign * std::min(1.0, shaped * compensation);
}

double DspCore::tensionTarget(const double env,
                              const double threshold,
                              const double floorThreshold,
                              const double floorHysteresis,
                              const double tension)
{
    if (std::abs(tension) <= epsilon || threshold <= epsilon)
        return env;

    if (env >= threshold)
        return env;

    const auto amount = std::abs(tension) * 0.01;
    const auto clampedFloor = juce::jlimit(epsilon, threshold, floorThreshold);
    if (threshold <= clampedFloor * (1.0 + 1.0e-6))
        return env;
    const auto shapedFull = tensionTargetDb(env, threshold, clampedFloor, amount, tension < 0.0);

    const auto floorFadeCeiling = std::min(threshold, clampedFloor * dbToAmp(3.0));
    auto floorFade = 1.0;
    if (env <= clampedFloor)
        floorFade = 0.0;
    else if (env < floorFadeCeiling)
    {
        const auto floorFadeSpan = amplitudeToDb(floorFadeCeiling) - amplitudeToDb(clampedFloor);
        const auto u = juce::jlimit(0.0, 1.0,
            (amplitudeToDb(env) - amplitudeToDb(clampedFloor)) / floorFadeSpan);
        floorFade = u * u * (3.0 - 2.0 * u);
    }
    const auto shapedGate = shapedFull * floorFade;

    return shapedGate + (shapedFull - shapedGate) * floorHysteresis;
}

void DspCore::prepare(const double sampleRate, const int, const int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (auto& core : transientSustainCores) core.prepare(currentSampleRate);
    for (auto& window : compressorHoldWindows) window.prepare(currentSampleRate);
    resizeLookaheadBuffers();
    lookaheadGain.prepare(currentSampleRate);
    tensionPreviewWindow.prepare(currentSampleRate, 0.030);
    for (auto& shaper : lookaheadTension) shaper.prepare(currentSampleRate);
    // Allocate the maximum RMS history once; window changes reuse this history.
    const auto adaptiveWindowSamples = std::max<size_t>(1, static_cast<size_t>(std::ceil(currentSampleRate * 10.0)));
    for (auto& history : adaptiveRmsCumulativePowerHistory)
        history.assign(adaptiveWindowSamples, 0.0);
    const auto rmsWindowSamples = std::max<size_t>(1, static_cast<size_t>(std::ceil(currentSampleRate)));
    for (auto& channelPower : baseRmsPower)
        channelPower.assign(rmsWindowSamples, 0.0);
    for (auto& channelPower : detectorRmsPower)
        channelPower.assign(rmsWindowSamples, 0.0);
    clearState();
    updateDerivedParameters();
}

void DspCore::reset()
{
    clearState();
    updateDerivedParameters();
}

void DspCore::setParameters(const Parameters& newParameters)
{
    const auto changed = [](float a, float b) { return !juce::approximatelyEqual(a, b); };
    if (changed(parameters.threshold, newParameters.threshold)
        || changed(parameters.ratio, newParameters.ratio)
        || changed(parameters.compKnee, newParameters.compKnee)
        || changed(parameters.feedback, newParameters.feedback)
        || changed(parameters.input, newParameters.input)
        || changed(parameters.peakCrestDb, newParameters.peakCrestDb)
        || changed(parameters.peakRms, newParameters.peakRms)
        || changed(parameters.rmsTime, newParameters.rmsTime)
        || changed(parameters.adaptive, newParameters.adaptive)
        || changed(parameters.adaptiveOffset, newParameters.adaptiveOffset))
        for (auto& attack : cleanAttackStates) attack.settled = false;
    parameters = newParameters;
    updateDerivedParameters();
}

int DspCore::getLatencySamples() const noexcept
{
    return derived.latencySamples;
}

}
