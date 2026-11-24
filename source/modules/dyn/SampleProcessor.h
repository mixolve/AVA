#pragma once

#include "../shared/DspUtilities.h"
#include "TransientSustainEnvelope.h"
#include "PeakCrest.h"
#include "AutoRelease.h"
#include "ThresholdHysteresis.h"
#include "HoldWindow.h"
#include "LookaheadGainShaper.h"
#include "LookaheadTensionShaper.h"
#include "AttackSourceGate.h"
#include "../shared/SampleRangeBank.h"

#include <JuceHeader.h>

#include <array>
#include <vector>
#include <functional>

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
        float threshold = 0.0f, thresholdHysteresisDb = 0.0f;
        float adaptive = 0.0f;
        float tension = 0.0f;
        float attackMs = 0.0f;
        float attackCurve = 0.0f;
        float holdMs = 0.0f;
        float autoRelease = 0.0f, releaseMemoryMs = 200.0f, releaseDepth = 0.0f;
        float releasePeakMs = 10.0f, releaseRmsMs = 150.0f;
        float input = 0.0f;
        float makeUp = 0.0f;
        float mix = 100.0f;
        float output = 0.0f;
        float range = 96.0f;
        float morph = 100.0f;
        float ratio = 100.0f;
        float peakRms = 0.0f;
        float peakCrestDb = 0.0f, rmsTime = 50.0f;
        float feedback = 0.0f;
        float compKnee = 0.0f, clipKnee = 0.0f;
        bool clipEql = false;
        float lookaheadMs = 0.0f;
        float lookaheadCurve = 0.0f;
        float tensionFloor = -96.0f;
        float tensionHysteresis = 0.0f;
        float releaseCurve = 0.0f;
        float rangeCurve = 0.0f;
        float adaptiveTime = 50.0f;
        float adaptiveOffset = 0.0f;
        bool sidechainListen = false;
        bool useSidechainInput = false;
        bool delta = false;
        bool transientSustain = false, transientMute = false, sustainMute = false, oneShot = false;
        float transientGain = 0.0f, sustainGain = 0.0f, retriggerMs = 60.0f;
        bool operator==(const Parameters& other) const noexcept
        {
            return juce::approximatelyEqual(threshold, other.threshold)
                && juce::approximatelyEqual(thresholdHysteresisDb, other.thresholdHysteresisDb)
                && juce::approximatelyEqual(adaptive, other.adaptive)
                && juce::approximatelyEqual(tension, other.tension)
                && juce::approximatelyEqual(attackMs, other.attackMs)
                && juce::approximatelyEqual(attackCurve, other.attackCurve)
                && juce::approximatelyEqual(holdMs, other.holdMs)
                && juce::approximatelyEqual(autoRelease, other.autoRelease)
                && juce::approximatelyEqual(releaseMemoryMs, other.releaseMemoryMs)
                && juce::approximatelyEqual(releaseDepth, other.releaseDepth)
                && juce::approximatelyEqual(releasePeakMs, other.releasePeakMs)
                && juce::approximatelyEqual(releaseRmsMs, other.releaseRmsMs)
                && juce::approximatelyEqual(input, other.input)
                && juce::approximatelyEqual(makeUp, other.makeUp)
                && juce::approximatelyEqual(mix, other.mix)
                && juce::approximatelyEqual(range, other.range)
                && juce::approximatelyEqual(output, other.output)
                && juce::approximatelyEqual(morph, other.morph)
                && juce::approximatelyEqual(ratio, other.ratio)
                && juce::approximatelyEqual(peakRms, other.peakRms)
                && juce::approximatelyEqual(peakCrestDb, other.peakCrestDb)
                && juce::approximatelyEqual(rmsTime, other.rmsTime)
                && juce::approximatelyEqual(feedback, other.feedback)
                && juce::approximatelyEqual(compKnee, other.compKnee)
                && juce::approximatelyEqual(clipKnee, other.clipKnee)
                && clipEql == other.clipEql
                && juce::approximatelyEqual(lookaheadMs, other.lookaheadMs)
                && juce::approximatelyEqual(lookaheadCurve, other.lookaheadCurve)
                && juce::approximatelyEqual(tensionFloor, other.tensionFloor)
                && juce::approximatelyEqual(tensionHysteresis, other.tensionHysteresis)
                && juce::approximatelyEqual(releaseCurve, other.releaseCurve)
                && juce::approximatelyEqual(rangeCurve, other.rangeCurve)
                && juce::approximatelyEqual(adaptiveTime, other.adaptiveTime)
                && juce::approximatelyEqual(adaptiveOffset, other.adaptiveOffset)
                && sidechainListen == other.sidechainListen
                && useSidechainInput == other.useSidechainInput
                && delta == other.delta
                && transientSustain == other.transientSustain && transientMute == other.transientMute
                && sustainMute == other.sustainMute && oneShot == other.oneShot
                && juce::approximatelyEqual(transientGain, other.transientGain)
                && juce::approximatelyEqual(sustainGain, other.sustainGain)
                && juce::approximatelyEqual(retriggerMs, other.retriggerMs);
        }
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParameters(const Parameters& newParameters);
    StereoSample processSample(double leftInput, double rightInput,
                               double sidechainLeft = 0.0, double sidechainRight = 0.0);
    int getLatencySamples() const noexcept;

private:
    struct DerivedParameters
    {
        static constexpr size_t numBranches = 4;

        std::array<double, numBranches> manualThresholds { 1.0, 1.0, 1.0, 1.0 };
        std::array<double, numBranches> adaptiveAmounts { 0.0, 0.0, 0.0, 0.0 };
        std::array<double, numBranches> tensions { 0.0, 0.0, 0.0, 0.0 };
        std::array<int, detector::count> releaseSamples {};
        int holdSamples = 0;
        int attackSamples = 0;
        double attackCurve = 0.0;
        double makeUpGain = 1.0;
        double mix = 1.0;
        double outputGain = 1.0;
        double minimumGain = 0.0;
        double inputGain = 1.0;
        double morph = 1.0;
        double ratio = 100.0;
        double rmsMix = 0.0;
        double peakCrestDb = 0.0, peakScale = 1.0, thresholdHysteresisDb = 0.0;
        double transientGain = 1.0, sustainGain = 1.0;
        double feedback = 0.0;
        double autoRelease = 0.0, releaseMemoryCoefficient = 0.0, releaseDepth = 0.0;
        double releaseGainExponent = 1.0;
        double compKneeDb = 0.0;
        double clipKneeDb = 0.0, clipLower = 1.0, clipUpper = 1.0;
        bool clipEql = false;
        double tensionFloor = 0.0;
        double tensionHysteresis = 0.0;
        double releaseCurve = 0.0;
        double rangeCurve = 0.0, rangeDb = 96.0;
        double adaptiveOffsetDb = 0.0;
        bool delta = false;
        int lookaheadSamples = 0;
        double lookaheadCurve = 0.0;
        int bufferSize = 1;
        int dryBufferSize = 1;
        int latencySamples = 0;
    };

    double satShape(double value, double referenceLevel) const;
    static double tensionTarget(double env, double threshold, double floorThreshold, double floorHysteresis, double tension);
    struct ReleaseState
    {
        double start = 0.0;
        double target = 0.0;
        int elapsedSamples = 0;
        int durationSamples = 0;
        // Elapsed HOLD time, not a countdown frozen at the beginning of a cycle.
        // This lets editing HOLD in real time extend/shorten the current hold.
        int holdElapsedSamples = 0;
        bool holdCompleted = false;
        bool active = false;
    };

    struct AttackState
    {
        double startGain = 1.0;
        // Most compression requested during this Attack. Do not substitute
        // the sample-varying detector target into the running interpolation.
        double targetGain = 1.0;
        int elapsedSamples = 0;
        int durationSamples = 0;
        bool active = false;
        bool settled = false;
    };

    double releaseTowards(ReleaseState& state,
                                 double current,
                                 double target,
                                 int durationSamples,
                                 double curve,
                                 double gainExponent = 0.0,
                                 int holdSamples = 0,
                                 double kneeDb = 0.0,
                                 double minimumGain = 0.0,
                                 double gainThreshold = 0.0,
                                 bool followTarget = false);

    void resizeLookaheadBuffers();
    void clearState();
    void updateDerivedParameters();
    double getEffectiveThreshold(size_t branchIndex) const noexcept;
    Parameters parameters;
    DerivedParameters derived;
    std::array<dyn::transientSustain::Envelope, detector::count> transientSustainCores;
    struct TensionFrame
    {
        std::array<double, DerivedParameters::numBranches> gainTargets {};
        std::array<double, DerivedParameters::numBranches> playbackGains {};
        std::array<double, DerivedParameters::numBranches> thresholds {};
    };
    TensionFrame evaluateTension(double envelope, double adaptiveRmsDb);
    StereoSample processTransientSustainSample(double keyL, double keyR, double adaptiveLevelDb,
                                      double signalPeak, const TensionFrame& tension);
    StereoSample finishSample(double wetL, double wetR);

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

    std::array<double, detector::count> envBase {};
    std::array<detector::ReleaseMemory, detector::count> releaseMemories;
    void updateCompressorGain(size_t detectorIndex, double detectorLevel, double threshold,
                              int releaseSamples, double gainExponent, double feedbackGainScale);
    std::array<HoldWindow, detector::count> compressorHoldWindows;
    LookaheadGainShaper lookaheadGain;
    // Maximum of the detector over LOOKAHEAD: stable across carrier cycles.
    HoldWindow tensionPreviewWindow;
    std::array<LookaheadTensionShaper, DerivedParameters::numBranches> lookaheadTension;
    dyn::ballistics::AttackSourceGate attackSourceGate;
    bool attackSourceQuiet = false;
    std::array<double, detector::count> cleanEnvPeak { 0.0, 0.0 };
    std::array<double, detector::count> cleanGainState { 1.0, 1.0 };
    std::array<double, detector::count> cleanGainTarget { 1.0, 1.0 };
    std::array<AttackState, detector::count> cleanAttackStates;
    std::array<ReleaseState, detector::count> baseReleaseStates;
    std::array<ReleaseState, detector::count> cleanReleaseStates;
    std::array<detector::ThresholdHysteresis, detector::count> cleanThresholdHysteresis;
    std::array<detector::ThresholdHysteresis, branchCount> tensionThresholdHysteresis;
    std::array<double, branchCount> adaptiveReferenceDb { -96.0, -96.0, -96.0, -96.0 };
    // Ring of cumulative energies: an arbitrary-length RMS window can be
    // queried in O(1), even while ADAP-TIME is being automated.
    std::array<std::vector<double>, channelCount> adaptiveRmsCumulativePowerHistory;
    std::array<double, channelCount> adaptiveRmsCumulativePower { 0.0, 0.0 };
    size_t adaptiveRmsWindowSamples = 1;
    size_t adaptiveRmsPosition = 0;
    size_t adaptiveRmsSampleCount = 0;
    std::array<std::vector<double>, channelCount> baseRmsPower;
    std::array<double, channelCount> baseRmsPowerSum {};
    std::array<std::vector<double>, channelCount> detectorRmsPower;
    std::array<double, channelCount> detectorRmsPowerSum {};
    size_t detectorRmsWindowSamples = 1, detectorRmsPosition = 0, detectorRmsSampleCount = 0;
    int bufPos = 0;
    int bufPosDry = 0;
    double delayedSample(const std::vector<double>& history, int writeIndex) const noexcept;

    std::array<std::vector<double>, branchCount> dmBase;
    std::vector<double> dryL;
    std::vector<double> dryR;
    std::vector<double> sidechainListenL, sidechainListenR;
};

using ProcessorBank = ava::modules::SampleRangeBank<DspCore>;
}
