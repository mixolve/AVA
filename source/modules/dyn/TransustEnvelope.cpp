#include "TransustEnvelope.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dyn::transust
{

namespace
{

float shapedAttackProgress(const double progress, const float curve)
{
    // Match the shared DYN ATTACK-CURVE scale and its LIN midpoint.
    const auto amount = juce::jlimit(-1.0, 1.0, static_cast<double>(curve));
    if (amount == 0.0) return static_cast<float>(progress);
    constexpr double maximumExponent = 4.605170185988091;
    const auto exponent = 4.0 * std::atanh(std::abs(amount) * std::tanh(maximumExponent * 0.25));
    const auto normalisation = std::expm1(exponent);
    return static_cast<float>(amount > 0.0
        ? 1.0 - std::expm1(exponent * (1.0 - progress)) / normalisation
        : std::expm1(exponent * progress) / normalisation);
}
}

void Envelope::prepare(const double sampleRate)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    updateDerivedParameters();
    reset();
}

void Envelope::reset()
{
    resetDetector();
}

void Envelope::setParameters(const Parameters& newParameters)
{
    if (newParameters.oneShot && ! parameters.oneShot)
        resetDetector(); // Arm the first event without the previous cycle's timers.
    else if (! newParameters.oneShot && parameters.oneShot)
        detector.wasAboveThreshold = false;

    parameters = newParameters;
    updateDerivedParameters();
}

float Envelope::calculateThresholdAmount(const float levelDb, const float thresholdDb, const float kneeDb) noexcept
{
    if (kneeDb <= 0.0f)
        return levelDb >= thresholdDb ? 1.0f : 0.0f;

    const auto kneeStartDb = thresholdDb - kneeDb;

    if (levelDb <= kneeStartDb)
        return 0.0f;

    if (levelDb >= thresholdDb)
        return 1.0f;

    const auto normalized = juce::jlimit(0.0f, 1.0f, (levelDb - kneeStartDb) / kneeDb);
    return normalized * normalized * (3.0f - (2.0f * normalized));
}

float Envelope::makeReleaseCoefficient(const float timeMs, const double sampleRate) noexcept
{
    const auto timeSeconds = juce::jmax(0.001f, timeMs * 0.001f);
    const auto samples = juce::jmax(1.0, static_cast<double>(timeSeconds) * sampleRate);
    return static_cast<float>(std::exp(-1.0 / samples));
}

void Envelope::updateDerivedParameters()
{
    const auto holdMs = juce::jmax(0.0f, parameters.holdMs);
    const auto releaseMs = juce::jmax(0.0f, parameters.releaseMs);

    derived.fastReleaseCoefficient = makeReleaseCoefficient(5.0f, currentSampleRate);
    derived.normalizedReleaseCurve = juce::jlimit(-1.0f, 1.0f, parameters.releaseCurve * 0.01f);
    derived.delaySamples = juce::jmax(0, static_cast<int>(std::round(juce::jmax(0.0f, parameters.delayMs) * 0.001 * currentSampleRate)));
    derived.attackSamples = juce::jmax(0, static_cast<int>(std::round(juce::jmax(0.0f, parameters.attackMs) * 0.001 * currentSampleRate)));
    derived.normalizedAttackCurve = juce::jlimit(-1.0f, 1.0f, parameters.attackCurve * 0.01f);
    derived.holdSamples = juce::jmax(0, static_cast<int>(std::round(holdMs * 0.001 * currentSampleRate)));
    const auto retriggerMs = juce::jlimit(1.0f, 5000.0f, parameters.retriggerMs);
    derived.retriggerSamples = juce::jmax(1, static_cast<int>(std::round(retriggerMs * 0.001 * currentSampleRate)));
    derived.autoRelease = juce::jlimit(0.0, 4.0, static_cast<double>(parameters.autoRelease) * 0.01);
    derived.releaseDepth = juce::jlimit(0.0, 1.0, static_cast<double>(parameters.releaseDepth) * 0.01);
    derived.releaseMemoryCoefficient = std::exp(-1.0 / (currentSampleRate * juce::jlimit(10.0, 2000.0, static_cast<double>(parameters.releaseMemoryMs)) * 0.001));
    derived.releaseSamples = juce::jmax(1, static_cast<int>(std::round(releaseMs * 0.001 * currentSampleRate)));
}

void Envelope::resetDetector() noexcept
{
    detector = {};
    detector.samplesSinceTrigger = std::numeric_limits<int>::max() / 2;
}

float Envelope::processAmount(const float detectorLevel, const float thresholdDb, const float kneeDb, const float triggerLevel) noexcept
{
    return processDetectorSample(detectorLevel, thresholdDb, kneeDb, triggerLevel);
}

float Envelope::processDetectorSample(const float level, const float thresholdDb, const float kneeDb, const float triggerLevel) noexcept
{
    detector.fastEnvelope = level >= detector.fastEnvelope
        ? level
        : level + ((detector.fastEnvelope - level) * derived.fastReleaseCoefficient);

    const auto levelDb = juce::Decibels::gainToDecibels(detector.fastEnvelope, -120.0f);
    auto thresholdAmount = calculateThresholdAmount(levelDb, thresholdDb, kneeDb);
    const auto eventLevel = triggerLevel >= 0.0f ? triggerLevel : detector.fastEnvelope;
    const auto eventDb = juce::Decibels::gainToDecibels(eventLevel, -120.0f);
    const auto eventAmount = calculateThresholdAmount(eventDb, thresholdDb, kneeDb);
    // Sample-grid peak variation must not turn a steady carrier into new events.
    auto eventAboveThreshold = eventAmount > 1.0e-4f
        || (detector.wasAboveThreshold && eventDb >= thresholdDb - kneeDb - 0.1f);
    if (parameters.thresholdHysteresisDb > 0.0f)
    {
        thresholdAmount = detector.thresholdHysteresis.process(thresholdAmount,
            eventDb - (thresholdDb - kneeDb), parameters.thresholdHysteresisDb);
        eventAboveThreshold = detector.thresholdHysteresis.active;
    }
    else
        detector.thresholdHysteresis = {};
    const auto risingTrigger = eventAboveThreshold && ! detector.wasAboveThreshold;
    const auto aboveThreshold = thresholdAmount > 1.0e-4f;
    detector.releaseMemory.advance(aboveThreshold, derived.releaseMemoryCoefficient, derived.autoRelease,
        derived.releaseDepth, std::clamp(static_cast<double>(thresholdAmount), 0.0, 1.0) * parameters.releaseDepthDb / 12.0);
    const auto retriggerElapsed = detector.samplesSinceTrigger >= derived.delaySamples + derived.attackSamples
        + derived.holdSamples + derived.retriggerSamples;
    const auto shouldTrigger = (parameters.oneShot ? risingTrigger : aboveThreshold)
        && ! detector.attackActive && retriggerElapsed;

    if (shouldTrigger)
    {
        detector.heldTransientAmount = parameters.oneShot ? juce::jmax(thresholdAmount, eventAmount) : thresholdAmount;
        detector.attackStartAmount = detector.transientEnvelope;
        detector.delaySamplesRemaining = derived.delaySamples;
        detector.attackSamplesElapsed = 0;
        detector.attackSamplesTotal = derived.attackSamples;
        detector.attackActive = derived.delaySamples > 0 || derived.attackSamples > 0;
        if (! detector.attackActive)
            detector.transientEnvelope = juce::jmax(detector.transientEnvelope, detector.heldTransientAmount);
        detector.releaseStartAmount = detector.transientEnvelope;
        detector.holdSamplesRemaining = derived.holdSamples;
        detector.releaseSamplesRemaining = 0;
        detector.releaseSamplesTotal = 0;
        detector.samplesSinceTrigger = 0;
    }
    else
    {
        detector.samplesSinceTrigger = detector.samplesSinceTrigger < (std::numeric_limits<int>::max() / 4)
            ? detector.samplesSinceTrigger + 1
            : detector.samplesSinceTrigger;

        if (! detector.attackActive && detector.holdSamplesRemaining > 0)
        {
            --detector.holdSamplesRemaining;
            detector.heldTransientAmount = juce::jmax(detector.heldTransientAmount, thresholdAmount);
            detector.transientEnvelope = detector.heldTransientAmount;
            detector.releaseStartAmount = detector.transientEnvelope;
            detector.releaseSamplesRemaining = 0;
            detector.releaseSamplesTotal = 0;
        }
        else if (! detector.attackActive)
        {
            if (detector.releaseSamplesTotal <= 0)
            {
                detector.releaseStartAmount = detector.transientEnvelope;
                detector.releaseSamplesTotal = derived.autoRelease > 0.0
                    ? static_cast<int>(std::round(derived.releaseSamples * detector.releaseMemory.multiplier(derived.autoRelease)))
                    : derived.releaseSamples;
                detector.releaseSamplesRemaining = detector.releaseSamplesTotal;
            }

            if (detector.releaseSamplesRemaining > 0)
            {
                const auto completedSamples = detector.releaseSamplesTotal - detector.releaseSamplesRemaining + 1;
                const auto progress = juce::jlimit(0.0f,
                                                   1.0f,
                                                   static_cast<float>(completedSamples) / static_cast<float>(detector.releaseSamplesTotal));
                const auto shapedProgress = derived.normalizedReleaseCurve >= 0.0f
                    ? std::pow(progress, 1.0f + (derived.normalizedReleaseCurve * 3.0f))
                    : 1.0f - std::pow(1.0f - progress, 1.0f + (-derived.normalizedReleaseCurve * 3.0f));

                detector.transientEnvelope = detector.releaseStartAmount * (1.0f - shapedProgress);
                --detector.releaseSamplesRemaining;
            }
            else
            {
                detector.transientEnvelope = 0.0f;
            }
        }
    }

    if (detector.attackActive)
    {
        // Keep the captured trigger even if a short transient ends during DELAY.
        detector.heldTransientAmount = juce::jmax(detector.heldTransientAmount, thresholdAmount);
        if (detector.delaySamplesRemaining > 0)
            --detector.delaySamplesRemaining;
        else
        {
            const auto target = juce::jmax(detector.attackStartAmount, detector.heldTransientAmount);
            const auto progress = detector.attackSamplesTotal > 0
                ? juce::jlimit(0.0, 1.0, static_cast<double>(++detector.attackSamplesElapsed) / detector.attackSamplesTotal)
                : 1.0;
            detector.transientEnvelope = detector.attackStartAmount + (target - detector.attackStartAmount)
                * shapedAttackProgress(progress, derived.normalizedAttackCurve);
            if (progress >= 1.0)
            {
                detector.attackActive = false;
                detector.heldTransientAmount = target;
                detector.releaseStartAmount = detector.transientEnvelope;
            }
        }
    }

    if (! detector.attackActive && detector.transientEnvelope < 1.0e-4f)
    {
        detector.transientEnvelope = 0.0f;
        detector.heldTransientAmount = 0.0f;
    }

    detector.wasAboveThreshold = eventAboveThreshold;
    return juce::jlimit(0.0f, 1.0f, detector.transientEnvelope);
}

}
