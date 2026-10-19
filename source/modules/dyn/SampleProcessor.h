#pragma once

#include "../shared/DspUtilities.h"
#include "TransustEnvelope.h"
#include "PeakCrest.h"
#include "AutoRelease.h"
#include "ThresholdHysteresis.h"
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
        float delayMs = 0.0f;
        float attackMs = 0.0f;
        float attackCurve = 0.0f;
        float holdMs = 0.0f;
        float autoRelease = 0.0f, releaseMemoryMs = 200.0f, releaseDepth = 0.0f;
        float releasePeakMs = 0.0f, releaseRmsMs = 150.0f;
        float input = 0.0f;
        float makeUp = 0.0f;
        float mix = 100.0f;
        float output = 0.0f;
        float range = 96.0f;
        float morph = 100.0f;
        float ratio = 100.0f;
        float progression = 0.0f, progressionSpan = 12.0f;
        float peakRms = 0.0f;
        float peakCrestDb = 0.0f, rmsTime = 50.0f;
        float feedback = 0.0f;
        float compKnee = 0.0f, clipKnee = 0.0f;
        float lookaheadMs = 0.0f;
        float tensionFloor = -96.0f;
        float tensionHysteresis = 0.0f;
        float releaseCurve = 0.0f;
        float releaseType = 0.0f, rangeCurve = 0.0f;
        float adaptiveTime = 50.0f;
        float adaptiveOffset = 0.0f;
        float adaptiveAttack = 30.0f;
        float adaptiveHold = 0.0f;
        float adaptiveRelease = 300.0f;
        bool sidechainListen = false;
        bool useSidechainInput = false;
        bool delta = false;
        bool transust = false, transientMute = false, sustainMute = false, oneShot = false;
        float transientGain = 0.0f, sustainGain = 0.0f, retriggerMs = 60.0f;
        bool operator==(const Parameters& other) const noexcept
        {
            return juce::approximatelyEqual(threshold, other.threshold)
                && juce::approximatelyEqual(thresholdHysteresisDb, other.thresholdHysteresisDb)
                && juce::approximatelyEqual(adaptive, other.adaptive)
                && juce::approximatelyEqual(tension, other.tension)
                && juce::approximatelyEqual(delayMs, other.delayMs)
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
                && juce::approximatelyEqual(progression, other.progression)
                && juce::approximatelyEqual(progressionSpan, other.progressionSpan)
                && juce::approximatelyEqual(peakRms, other.peakRms)
                && juce::approximatelyEqual(peakCrestDb, other.peakCrestDb)
                && juce::approximatelyEqual(rmsTime, other.rmsTime)
                && juce::approximatelyEqual(feedback, other.feedback)
                && juce::approximatelyEqual(compKnee, other.compKnee)
                && juce::approximatelyEqual(clipKnee, other.clipKnee)
                && juce::approximatelyEqual(lookaheadMs, other.lookaheadMs)
                && juce::approximatelyEqual(tensionFloor, other.tensionFloor)
                && juce::approximatelyEqual(tensionHysteresis, other.tensionHysteresis)
                && juce::approximatelyEqual(releaseCurve, other.releaseCurve)
                && juce::approximatelyEqual(releaseType, other.releaseType)
                && juce::approximatelyEqual(rangeCurve, other.rangeCurve)
                && juce::approximatelyEqual(adaptiveTime, other.adaptiveTime)
                && juce::approximatelyEqual(adaptiveOffset, other.adaptiveOffset)
                && juce::approximatelyEqual(adaptiveAttack, other.adaptiveAttack)
                && juce::approximatelyEqual(adaptiveHold, other.adaptiveHold)
                && juce::approximatelyEqual(adaptiveRelease, other.adaptiveRelease)
                && sidechainListen == other.sidechainListen
                && useSidechainInput == other.useSidechainInput
                && delta == other.delta
                && transust == other.transust && transientMute == other.transientMute
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
        int delaySamples = 0;
        int attackSamples = 0;
        double attackCurve = 0.0;
        double makeUpGain = 1.0;
        double mix = 1.0;
        double outputGain = 1.0;
        double minimumGain = 0.0;
        double inputGain = 1.0;
        double morph = 1.0;
        double ratio = 100.0;
        double progression = 0.0, progressionSpan = 12.0;
        double rmsMix = 0.0;
        double peakCrestDb = 0.0, peakScale = 1.0, thresholdHysteresisDb = 0.0;
        double transientGain = 1.0, sustainGain = 1.0;
        double feedback = 0.0, feedbackSmoothingCoefficient = 0.0;
        double autoRelease = 0.0, releaseMemoryCoefficient = 0.0, releaseDepth = 0.0;
        double releaseGainExponent = 1.0;
        double compKneeDb = 0.0;
        double clipKneeDb = 0.0, clipLower = 1.0, clipUpper = 1.0;
        double tensionFloor = 0.0;
        double tensionHysteresis = 0.0;
        double tensionSmoothingCoefficient = 0.0;
        double releaseCurve = 0.0;
        double releaseType = 0.0, rangeCurve = 0.0, rangeDb = 96.0;
        double adaptiveOffsetDb = 0.0;
        double adaptiveAttackCoefficient = 0.0;
        double adaptiveReleaseCoefficient = 0.0;
        int adaptiveHoldSamples = 0;
        bool delta = false;
        int lookaheadSamples = 0;
        int bufferSize = 1;
        int dryBufferSize = 1;
        int latencySamples = 0;
    };

    double satShape(double value) const;
    static double tensionTarget(double env, double threshold, double floorThreshold, double floorHysteresis, double tension);
    struct ReleaseState
    {
        double start = 0.0;
        double target = 0.0;
        int elapsedSamples = 0;
        int durationSamples = 0;
        int holdSamplesRemaining = 0;
        bool active = false;
    };

    struct AttackState
    {
        double startGain = 1.0;
        int elapsedSamples = 0;
        int durationSamples = 0;
        int delaySamplesRemaining = 0;
        bool active = false;
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
    std::array<dyn::transust::Envelope, detector::count> transustCores;
    StereoSample processTransustSample(double keyL, double keyR, double adaptiveLevelDb, double signalPeak);
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
    std::array<double, branchCount> baseGainState { 1.0, 1.0, 1.0, 1.0 };
    std::array<double, branchCount> smoothedTensions { 0.0, 0.0, 0.0, 0.0 };
    bool tensionSmoothingInitialised = false;
    struct SignalPeakState
    {
        struct Entry { double level = 0.0; juce::uint64 sample = 0; };
        std::vector<Entry> queue;
        size_t head = 0, count = 0, window = 1;
        juce::uint64 sample = 0, lastCrossing = 0;
        bool positive = true, hasCrossing = false;
    };
    SignalPeakState baseSignalPeak, cleanSignalPeak;
    // Minimum gain over the audio lookahead interval; storage is prepared off the audio path.
    SignalPeakState lookaheadGain;
    double alignLookaheadGain(double gain);
    std::array<detector::ReleaseMemory, detector::count> releaseMemories;
    double smoothedFeedback = 0.0;
    bool feedbackInitialised = false;
    void prepareSignalPeak(SignalPeakState& state, bool positive);
    void updateCompressorGain(size_t detectorIndex, double peakNow, bool inputPositive,
                              double releaseSignal, double releaseThreshold, int releaseSamples,
                              double gainExponent, double trackingBlend, double feedbackGainScale = 1.0);
    double updateSignalPeak(SignalPeakState& state, double level, bool positive);
    std::array<double, detector::count> cleanEnvPeak { 0.0, 0.0 };
    std::array<double, detector::count> cleanGainState { 1.0, 1.0 };
    std::array<double, detector::count> cleanGainTarget { 1.0, 1.0 };
    std::array<AttackState, detector::count> cleanAttackStates;
    std::array<double, detector::count> cleanHalfPeak { 0.0, 0.0 };
    std::array<bool, detector::count> cleanInputPositive { true, true };
    std::array<ReleaseState, detector::count> baseReleaseStates;
    std::array<ReleaseState, detector::count> cleanReleaseStates;
    std::array<detector::ThresholdHysteresis, detector::count> cleanThresholdHysteresis;
    std::array<detector::ThresholdHysteresis, branchCount> tensionThresholdHysteresis;
    std::array<double, branchCount> adaptiveReferenceDb { -96.0, -96.0, -96.0, -96.0 };
    std::array<int, branchCount> adaptiveHoldSamplesRemaining { 0, 0, 0, 0 };
    std::array<std::vector<double>, channelCount> adaptiveRmsPower;
    std::array<double, channelCount> adaptiveRmsPowerSum { 0.0, 0.0 };
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

    std::array<std::vector<double>, branchCount> dmBase;
    std::vector<double> dryL;
    std::vector<double> dryR;
    std::vector<double> sidechainListenL, sidechainListenR;
};

using ProcessorBank = ava::modules::SampleRangeBank<DspCore>;
}
