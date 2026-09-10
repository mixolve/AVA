#pragma once

#include "../DspUtilities.h"
#include "../SampleRangeBank.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace dyn::dsp
{
using ava::modules::dsp::dbToAmp;
using ava::modules::dsp::epsilon;
using ava::modules::dsp::roundToParameterStep;
using ava::modules::dsp::wrapIndex;

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
        float leftUpThreshold = 0.0f;
        float leftUpAdaptive = 0.0f;
        float leftUpTension = 0.0f;
        float leftUpRelease = 0.0f;
        float leftUpOutput = 0.0f;
        float leftDownThreshold = 0.0f;
        float leftDownAdaptive = 0.0f;
        float leftDownTension = 0.0f;
        float leftDownRelease = 0.0f;
        float leftDownOutput = 0.0f;
        float rightUpThreshold = 0.0f;
        float rightUpAdaptive = 0.0f;
        float rightUpTension = 0.0f;
        float rightUpRelease = 0.0f;
        float rightUpOutput = 0.0f;
        float rightDownThreshold = 0.0f;
        float rightDownAdaptive = 0.0f;
        float rightDownTension = 0.0f;
        float rightDownRelease = 0.0f;
        float rightDownOutput = 0.0f;
        float morph = 0.0f;
        float ratio = 100.0f;
        float knee = 0.0f;
        float peakHoldMs = 0.0f;
        float lookaheadMs = 0.0f;
        float tensionFloor = -96.0f;
        float tensionHysteresis = 0.0f;
        int releaseForm = 0;
        float releaseCurve = 0.0f;
        float adaptiveOffset = 0.0f;
        float adaptiveAttack = 30.0f;
        float adaptiveHold = 0.0f;
        float adaptiveRelease = 300.0f;
        bool delta = false;
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
        static constexpr size_t numBranches = 4;

        std::array<double, numBranches> manualThresholds { 1.0, 1.0, 1.0, 1.0 };
        std::array<double, numBranches> adaptiveAmounts { 0.0, 0.0, 0.0, 0.0 };
        std::array<double, numBranches> tensions { 0.0, 0.0, 0.0, 0.0 };
        std::array<int, numBranches> releaseSamples { 0, 0, 0, 0 };
        int peakHoldSamples = 0;
        std::array<double, numBranches> branchOutGains { 1.0, 1.0, 1.0, 1.0 };
        double morph = 0.0;
        double ratio = 100.0;
        double clipKneeDb = 0.0;
        double tensionFloor = 0.0;
        double tensionHysteresis = 0.0;
        double tensionSmoothingCoefficient = 0.0;
        bool releaseLogarithmic = false;
        double releaseCurve = 0.0;
        double adaptiveOffsetDb = 0.0;
        double adaptiveAttackCoefficient = 0.0;
        double adaptiveReleaseCoefficient = 0.0;
        int adaptiveHoldSamples = 0;
        bool delta = false;
        int lookaheadSamples = 0;
        int totalLookaheadSamples = 0;
        int bufferSize = 1;
        int dryBufferSize = 1;
        int latencySamples = 0;
    };

    static double safeAbs(double value);
    static double clamp1(double value);
    static double satShape(double value, double kneeDb);
    static double tensionTarget(double env, double threshold, double floorThreshold, double floorHysteresis, double tension);
    struct ReleaseState
    {
        double start = 0.0;
        double target = 0.0;
        int elapsedSamples = 0;
        int durationSamples = 0;
        bool active = false;
    };

    static double releaseTowards(ReleaseState& state,
                                 double current,
                                 double target,
                                 int durationSamples,
                                 bool logarithmic,
                                 double curve);

    void resizeLookaheadBuffers();
    void clearState();
    void updateDerivedParameters();
    Parameters parameters;
    DerivedParameters derived;

    enum BranchIndex : size_t
    {
        branchLeftUp = 0,
        branchLeftDown,
        branchRightUp,
        branchRightDown,
        branchCount
    };

    static constexpr size_t channelCount = 2;

    double currentSampleRate = 44100.0;
    int maxBuf = 1;

    std::array<double, branchCount> envBase { 0.0, 0.0, 0.0, 0.0 };
    std::array<double, branchCount> baseGainState { 1.0, 1.0, 1.0, 1.0 };
    std::array<double, branchCount> smoothedTensions { 0.0, 0.0, 0.0, 0.0 };
    bool tensionSmoothingInitialised = false;
    std::array<double, channelCount> cleanEnvPeak { 0.0, 0.0 };
    std::array<double, channelCount> cleanGainState { 1.0, 1.0 };
    std::array<double, channelCount> cleanHalfPeak { 0.0, 0.0 };
    std::array<bool, channelCount> cleanInputPositive { true, true };
    std::array<int, channelCount> cleanHoldSamples { 0, 0 };
    std::array<ReleaseState, branchCount> baseReleaseStates;
    std::array<ReleaseState, channelCount> cleanReleaseStates;
    std::array<double, branchCount> adaptiveReferenceDb { -96.0, -96.0, -96.0, -96.0 };
    std::array<int, branchCount> adaptiveHoldSamplesRemaining { 0, 0, 0, 0 };
    int bufPos = 0;
    int bufPosDry = 0;

    std::array<std::vector<double>, branchCount> dmBase;
    std::vector<double> dryL;
    std::vector<double> dryR;
};

using ProcessorBank = ava::modules::SampleRangeBank<DspCore>;
} // namespace dyn::dsp
