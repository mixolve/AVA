#pragma once

#include "../shared/DspUtilities.h"
#include "ThresholdHysteresis.h"
#include "AutoRelease.h"

#include <JuceHeader.h>

namespace dyn::transust
{
class Envelope
{
public:
    struct Parameters
    {
        float delayMs = 0.0f;
        float attackMs = 0.0f;
        float attackCurve = 0.0f;
        float holdMs = 0.0f;
        float autoRelease = 0.0f, releaseMemoryMs = 200.0f, releaseDepth = 0.0f, releaseDepthDb = 12.0f;
        float releaseMs = 0.0f;
        float releaseCurve = 0.0f;
        float thresholdHysteresisDb = 0.0f;
        float retriggerMs = 60.0f;
        bool oneShot = false;
    };

    void prepare(double sampleRate);
    void reset();
    void setParameters(const Parameters& newParameters);
    float processAmount(float detectorLevel, float thresholdDb, float kneeDb, float triggerLevel = -1.0f) noexcept;

private:
    struct DerivedParameters
    {
        float fastReleaseCoefficient = 0.0f;
        float normalizedReleaseCurve = 0.0f;
        int delaySamples = 0;
        int attackSamples = 0;
        float normalizedAttackCurve = 0.0f;
        int holdSamples = 0;
        int retriggerSamples = 0;
        double autoRelease = 0.0, releaseMemoryCoefficient = 0.0, releaseDepth = 0.0;
        int releaseSamples = 1;
    };

    struct DetectorState
    {
        detector::ReleaseMemory releaseMemory;
        float fastEnvelope = 0.0f;
        float transientEnvelope = 0.0f;
        float heldTransientAmount = 0.0f;
        float releaseStartAmount = 0.0f;
        float attackStartAmount = 0.0f;
        int delaySamplesRemaining = 0;
        int attackSamplesElapsed = 0;
        int attackSamplesTotal = 0;
        bool attackActive = false;
        int holdSamplesRemaining = 0;
        int releaseSamplesRemaining = 0;
        int releaseSamplesTotal = 0;
        int samplesSinceTrigger = 0;
        bool wasAboveThreshold = false;
        detector::ThresholdHysteresis thresholdHysteresis;
    };

    static float calculateThresholdAmount(float levelDb, float thresholdDb, float kneeDb) noexcept;
    static float makeReleaseCoefficient(float timeMs, double sampleRate) noexcept;
    void updateDerivedParameters();
    void resetDetector() noexcept;
    float processDetectorSample(float level, float thresholdDb, float kneeDb, float triggerLevel) noexcept;

    Parameters parameters;
    DerivedParameters derived;
    DetectorState detector;
    double currentSampleRate = 44100.0;
};
}
