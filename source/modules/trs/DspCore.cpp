#include "DspCore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace trs::dsp
{
using ava::modules::dsp::wrapIndex;

namespace
{
constexpr auto gainMinDb = -48.0f;
constexpr auto gainMaxDb = 48.0f;
}

void DspCore::prepare(const double sampleRate, int, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    delayBufferLength = juce::jmax(1, getMaximumLatencySamples(currentSampleRate) + 1);
    delayLeft.assign(static_cast<size_t>(delayBufferLength), 0.0f);
    delayRight.assign(static_cast<size_t>(delayBufferLength), 0.0f);
    updateDerivedParameters();
    reset();
}

void DspCore::reset()
{
    clearDelayBuffers();
    resetDetector();
}

void DspCore::setParameters(const Parameters& newParameters)
{
    parameters = newParameters;
    updateDerivedParameters();
}

DspCore::StereoSample DspCore::processSample(const double leftInput, const double rightInput)
{
    if (delayLeft.empty() || delayRight.empty())
        return { leftInput, rightInput };

    const auto detectorLevel = static_cast<float>(juce::jmax(std::abs(leftInput), std::abs(rightInput)));
    const auto transientAmount = processDetectorSample(detectorLevel);
    const auto readIndex = wrapIndex(delayWriteIndex - derived.latencySamples, delayBufferLength);

    delayLeft[static_cast<size_t>(delayWriteIndex)] = static_cast<float>(leftInput);
    delayRight[static_cast<size_t>(delayWriteIndex)] = static_cast<float>(rightInput);

    const auto delayedLeft = delayLeft[static_cast<size_t>(readIndex)];
    const auto delayedRight = delayRight[static_cast<size_t>(readIndex)];
    const auto transientGain = transientAmount * derived.transientGain;
    const auto sustainGain = (1.0f - transientAmount) * derived.sustainGain;
    const auto gain = transientGain + sustainGain;

    delayWriteIndex = wrapIndex(delayWriteIndex + 1, delayBufferLength);
    return { delayedLeft * gain, delayedRight * gain };
}

int DspCore::getMaximumLatencySamples(const double sampleRate) noexcept
{
    const auto safeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    return juce::jmax(0, static_cast<int>(std::ceil(safeSampleRate * 0.02)));
}

int DspCore::getLatencySamples() const noexcept
{
    return derived.latencySamples;
}

float DspCore::calculateThresholdAmount(const float levelDb, const float thresholdDb, const float kneeDb) noexcept
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

float DspCore::makeReleaseCoefficient(const float timeMs, const double sampleRate) noexcept
{
    const auto timeSeconds = juce::jmax(0.001f, timeMs * 0.001f);
    const auto samples = juce::jmax(1.0, static_cast<double>(timeSeconds) * sampleRate);
    return static_cast<float>(std::exp(-1.0 / samples));
}

void DspCore::updateDerivedParameters()
{
    const auto transientGainDb = juce::jlimit(gainMinDb, gainMaxDb, parameters.transientGainDb);
    const auto sustainGainDb = juce::jlimit(gainMinDb, gainMaxDb, parameters.sustainGainDb);
    const auto holdMs = juce::jlimit(0.0f, 200.0f, parameters.holdMs);
    const auto releaseMs = juce::jlimit(1.0f, 500.0f, parameters.releaseMs);
    const auto lookaheadMs = juce::jlimit(0.0f, 20.0f, parameters.lookaheadMs);

    derived.fastReleaseCoefficient = makeReleaseCoefficient(5.0f, currentSampleRate);
    derived.normalizedReleaseCurve = juce::jlimit(-1.0f, 1.0f, parameters.releaseCurve * 0.01f);
    derived.holdSamples = juce::jmax(0, static_cast<int>(std::round(holdMs * 0.001 * currentSampleRate)));
    const auto retriggerMs = juce::jlimit(1.0f, 5000.0f, parameters.retriggerMs);
    derived.retriggerSamples = juce::jmax(1, static_cast<int>(std::round(retriggerMs * 0.001 * currentSampleRate)));
    derived.releaseSamples = juce::jmax(1, static_cast<int>(std::round(releaseMs * 0.001 * currentSampleRate)));
    derived.latencySamples = juce::jlimit(0,
                                          getMaximumLatencySamples(currentSampleRate),
                                          static_cast<int>(std::round(lookaheadMs * 0.001 * currentSampleRate)));
    derived.transientGain = parameters.transientEnabled ? juce::Decibels::decibelsToGain(transientGainDb) : 0.0f;
    derived.sustainGain = parameters.sustainEnabled ? juce::Decibels::decibelsToGain(sustainGainDb) : 0.0f;
}

void DspCore::clearDelayBuffers() noexcept
{
    delayWriteIndex = 0;
    std::fill(delayLeft.begin(), delayLeft.end(), 0.0f);
    std::fill(delayRight.begin(), delayRight.end(), 0.0f);
}

void DspCore::resetDetector() noexcept
{
    detector = {};
    detector.samplesSinceTrigger = std::numeric_limits<int>::max() / 2;
}

float DspCore::processDetectorSample(const float level) noexcept
{
    detector.fastEnvelope = level >= detector.fastEnvelope
        ? level
        : level + ((detector.fastEnvelope - level) * derived.fastReleaseCoefficient);

    const auto levelDb = juce::Decibels::gainToDecibels(detector.fastEnvelope, -120.0f);
    const auto thresholdDb = juce::jlimit(-48.0f, 0.0f, parameters.thresholdDb);
    const auto kneeDb = juce::jlimit(0.0f, 24.0f, parameters.kneeDb);
    const auto thresholdAmount = calculateThresholdAmount(levelDb, thresholdDb, kneeDb);
    const auto aboveThreshold = thresholdAmount > 1.0e-4f;
    const auto thresholdRisingEdge = aboveThreshold && ! detector.wasAboveThreshold;
    const auto retriggerElapsed = detector.samplesSinceTrigger >= derived.holdSamples + derived.retriggerSamples;
    const auto shouldTrigger = aboveThreshold
        && retriggerElapsed
        && (! parameters.oneShot || thresholdRisingEdge);

    if (shouldTrigger)
    {
        detector.heldTransientAmount = thresholdAmount;
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

        if (detector.holdSamplesRemaining > 0)
        {
            --detector.holdSamplesRemaining;
            detector.heldTransientAmount = juce::jmax(detector.heldTransientAmount, thresholdAmount);
            detector.transientEnvelope = detector.heldTransientAmount;
            detector.releaseStartAmount = detector.transientEnvelope;
            detector.releaseSamplesRemaining = 0;
            detector.releaseSamplesTotal = 0;
        }
        else
        {
            if (detector.releaseSamplesTotal <= 0)
            {
                detector.releaseStartAmount = detector.transientEnvelope;
                detector.releaseSamplesTotal = derived.releaseSamples;
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

    if (detector.transientEnvelope < 1.0e-4f)
    {
        detector.transientEnvelope = 0.0f;
        detector.heldTransientAmount = 0.0f;
    }

    detector.wasAboveThreshold = aboveThreshold;
    return juce::jlimit(0.0f, 1.0f, detector.transientEnvelope);
}

bool DspCore::isNeutral() const noexcept
{
    return false;
}
}
