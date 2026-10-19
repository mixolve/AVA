#include "SampleProcessor.h"

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
                       const double shape,
                       const bool mirrored)
{
    const auto floorDb = amplitudeToDb(floorThreshold);
    const auto thresholdDb = amplitudeToDb(threshold);
    const auto spanDb = thresholdDb - floorDb;

    if (spanDb <= epsilon)
        return threshold;

    const auto ratio = juce::jlimit(0.0, 1.0, (amplitudeToDb(env) - floorDb) / spanDb);
    const auto shapedRatio = mirrored ? 1.0 - std::pow(1.0 - ratio, shape)
                                      : std::pow(ratio, shape);
    return dbToAmp(floorDb + spanDb * shapedRatio);
}
}

double DspCore::satShape(const double value) const
{
    if (derived.clipKneeDb <= epsilon) return juce::jlimit(-1.0, 1.0, value);
    const auto level = std::abs(value);
    if (level <= derived.clipLower) return value;
    const auto sign = value < 0.0 ? -1.0 : 1.0;
    if (level >= derived.clipUpper) return sign;
    // Shape the transition in dB so its audible width follows CLIP-KNEE.
    // The quadratic joins unity gain and the ceiling with continuous slopes.
    const auto levelDb = amplitudeToDb(level);
    const auto distanceDb = levelDb + derived.clipKneeDb * 0.5;
    const auto reductionDb = distanceDb * distanceDb / (2.0 * derived.clipKneeDb);
    return sign * dbToAmp(levelDb - reductionDb);
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
    const auto shape = 1.0 + 42.0 * amount * amount;
    const auto clampedFloor = juce::jlimit(epsilon, threshold, floorThreshold);
    const auto shapedFull = tensionTargetDb(env, threshold, clampedFloor, shape, tension < 0.0);

    const auto shapedGate = env <= clampedFloor ? 0.0 : shapedFull;

    return shapedGate + (shapedFull - shapedGate) * floorHysteresis;
}

void DspCore::prepare(const double sampleRate, const int, const int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;

    for (auto& core : transustCores) core.prepare(currentSampleRate);
    resizeLookaheadBuffers();
    // Allocate the maximum RMS history once; window changes reuse this history.
    const auto rmsWindowSamples = std::max<size_t>(1, static_cast<size_t>(std::ceil(currentSampleRate)));
    for (auto& channelPower : adaptiveRmsPower)
        channelPower.assign(rmsWindowSamples, 0.0);
    for (auto& channelPower : baseRmsPower)
        channelPower.assign(rmsWindowSamples, 0.0);
    for (auto& channelPower : detectorRmsPower)
        channelPower.assign(rmsWindowSamples, 0.0);
    // A bounded monotonic queue tracks the carrier peak without allocations in processSample.
    const auto peakCapacity = static_cast<size_t>(std::ceil(currentSampleRate * 0.05)) + 2;
    baseSignalPeak.queue.resize(peakCapacity);
    cleanSignalPeak.queue.resize(peakCapacity);
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
    parameters = newParameters;
    updateDerivedParameters();
}

int DspCore::getLatencySamples() const noexcept
{
    return derived.latencySamples;
}

}
