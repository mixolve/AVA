#pragma once

#include "../shared/DspUtilities.h"

#include <JuceHeader.h>

#include <vector>

namespace trs::dsp
{
class DspCore
{
public:
    struct StereoSample
    {
        double left = 0.0;
        double right = 0.0;
    };

    struct Parameters
    {
        bool transientEnabled = true;
        bool sustainEnabled = true;
        float transientGainDb = 0.0f;
        float sustainGainDb = 0.0f;
        float holdMs = 0.0f;
        float releaseMs = 10.0f;
        float releaseCurve = 0.0f;
        float thresholdDb = -48.0f;
        float kneeDb = 0.0f;
        float retriggerMs = 1.0f;
        bool oneShot = false;
        float lookaheadMs = 1.0f;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParameters(const Parameters& newParameters);
    StereoSample processSample(double leftInput, double rightInput);
    static int getMaximumLatencySamples(double sampleRate) noexcept;
    int getLatencySamples() const noexcept;
    bool isNeutral() const noexcept;

private:
    struct DerivedParameters
    {
        float fastReleaseCoefficient = 0.0f;
        float normalizedReleaseCurve = 0.0f;
        int holdSamples = 0;
        int retriggerSamples = 0;
        int releaseSamples = 1;
        int latencySamples = 0;
        float transientGain = 1.0f;
        float sustainGain = 1.0f;
    };

    struct DetectorState
    {
        float fastEnvelope = 0.0f;
        float transientEnvelope = 0.0f;
        float heldTransientAmount = 0.0f;
        float releaseStartAmount = 0.0f;
        int holdSamplesRemaining = 0;
        int releaseSamplesRemaining = 0;
        int releaseSamplesTotal = 0;
        int samplesSinceTrigger = 0;
        bool wasAboveThreshold = false;
    };

    static float calculateThresholdAmount(float levelDb, float thresholdDb, float kneeDb) noexcept;
    static float makeReleaseCoefficient(float timeMs, double sampleRate) noexcept;
    void updateDerivedParameters();
    void clearDelayBuffers() noexcept;
    void resetDetector() noexcept;
    float processDetectorSample(float level) noexcept;

    Parameters parameters;
    DerivedParameters derived;
    DetectorState detector;
    std::vector<float> delayLeft;
    std::vector<float> delayRight;
    double currentSampleRate = 44100.0;
    int delayBufferLength = 1;
    int delayWriteIndex = 0;
};
}
