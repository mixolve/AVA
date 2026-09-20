#include "DspCore.h"

#include <algorithm>
#include <cmath>

namespace dyn::dsp
{
namespace
{
constexpr double maxLookaheadMs = 24.0;

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

double DspCore::safeAbs(const double value)
{
    return std::abs(value);
}

double DspCore::clamp1(const double value)
{
    return juce::jlimit(-1.0, 1.0, value);
}

double DspCore::satShape(const double value, const double kneeDb)
{
    if (kneeDb <= epsilon)
        return clamp1(value);

    const auto absoluteValue = safeAbs(value);
    const auto sign = value < 0.0 ? -1.0 : 1.0;

    auto knee = 1.0 - (kneeDb / 24.0) * (2.0 / 3.0);
    knee = juce::jlimit(1.0 / 3.0, 1.0, knee);

    if (absoluteValue <= knee)
        return value;

    if (absoluteValue >= 2.0 - knee)
        return sign;

    const auto delta = absoluteValue - knee;
    return sign * (absoluteValue - (delta * delta) / (4.0 * (1.0 - knee)));
}

double DspCore::tensionTarget(const double env,
                              const double threshold,
                              const double floorThreshold,
                              const double floorHysteresis,
                              const double tension)
{
    if (safeAbs(tension) <= epsilon || threshold <= epsilon)
        return env;

    if (env >= threshold)
        return env;

    const auto amount = safeAbs(tension) * 0.01;
    const auto shape = 1.0 + 42.0 * amount * amount;
    const auto clampedFloor = juce::jlimit(epsilon, threshold, floorThreshold);
    const auto shapedFull = tensionTargetDb(env, threshold, clampedFloor, shape, tension < 0.0);

    const auto shapedGate = env <= clampedFloor ? 0.0 : shapedFull;

    return shapedGate + (shapedFull - shapedGate) * floorHysteresis;
}

void DspCore::prepare(const double sampleRate, const int, const int)
{
    currentSampleRate = sampleRate;

    resizeLookaheadBuffers();
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

int DspCore::getMaximumLatencySamples(const double sampleRate) noexcept
{
    const auto safeSampleRate = std::max(1.0, sampleRate);
    const auto maxBufferSize = std::max(1, static_cast<int>(std::ceil(safeSampleRate * maxLookaheadMs * 0.001)) + 2);
    return std::max(0, maxBufferSize - 2);
}

bool DspCore::isNeutral() const noexcept
{
    return false;
}
}
