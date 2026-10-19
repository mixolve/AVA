#pragma once

#include "../shared/DspUtilities.h"
#include "../shared/SampleRangeBank.h"
#include "comb/StereoEngine.h"
#include "BlockOrder.h"
#include "GainOrder.h"

#include <JuceHeader.h>

#include <array>
#include <vector>

namespace tls::dsp
{
using ava::modules::dsp::dbToAmp;
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
        float gainMid = 0.0f;
        bool gainMidMute = false;
        float gainSide = 0.0f;
        bool gainSideMute = false;
        float gainL = 0.0f;
        bool gainLMute = false;
        float gainR = 0.0f;
        bool gainRMute = false;
        float gainLr = 0.0f;
        bool gainLrMute = false;
        bool halfPositive = false;
        bool halfNegative = false;
        bool fullPositive = false;
        bool fullNegative = false;
        float left = -100.0f;
        float right = 100.0f;
        float law = 0.0f;
        float impact = 0.0f;
        bool impactToRight = false;
        float mid = 0.0f;
        float side = 0.0f;
        float degree = 0.0f;
        bool flipRight = false;
        bool listenLc = false;
        bool listenRc = false;
        bool listenMc = false;
        bool listenSc = false;
        bool listenLl = false;
        bool listenRr = false;
        bool listenMsPlus3 = false;
        bool listenSs = false;
        float stereoDelayMs = 0.0f;
        float leftDelayMs = 0.0f;
        float rightDelayMs = 0.0f;
        float stereoPhase = 0.0f;
        float leftPhase = 0.0f;
        float rightPhase = 0.0f;
        std::array<bool,blocks::count> blockHidden {};
        std::array<bool,blocks::count> blockBypass {};
        std::array<bool,gain::count> gainBypass {};
        blocks::Order blockOrder = blocks::defaultOrder;
        gain::Order gainOrder = gain::defaultOrder;
        bool combCountBypass = false;
        int combCount = 0;
        float combShift = 0.0f;
        bool combShiftBypass = false;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels);
    void reset();
    void setParameters(const Parameters& newParameters);
    StereoSample processSample(double leftInput, double rightInput);
    int getLatencySamples() const noexcept;
    bool isNeutral() const noexcept;

private:
    enum class ListenMode
    {
        leftCenter,
        rightCenter,
        midCenter,
        sideCenter,
        leftLeft,
        rightRight,
        neutral,
        sideStereo
    };

    struct DerivedParameters
    {
        double midGain = 1.0;
        double sideGain = 1.0;
        double leftGain = 1.0;
        double rightGain = 1.0;
        double linkedGain = 1.0;
        double gLL = 1.0;
        double gLR = 0.0;
        double gRL = 0.0;
        double gRR = 1.0;
        double impactAmount = 0.0;
        double midAmount = 0.0;
        double sideAmount = 0.0;
        double orthogonalM11 = 1.0;
        double orthogonalM12 = 0.0;
        double orthogonalM21 = 0.0;
        double orthogonalM22 = 1.0;
        ListenMode listenMode = ListenMode::neutral;
        double listenMsGain = 1.0;
        std::array<bool,blocks::count> blockEnabled {};
        bool signalTransformEnabled = false;
        int leftDelaySamples = 0;
        int rightDelaySamples = 0;
        bool delayEnabled = false;
        bool phaseEnabled = false;
        bool combEnabled = false;
        int latencySamples = 0;
        double leftPhaseCosine = 1.0;
        double leftPhaseSine = 0.0;
        double rightPhaseCosine = 1.0;
        double rightPhaseSine = 0.0;
    };

    static constexpr int phaseFilterTaps = 129;
    static constexpr int phaseFilterLatency = (phaseFilterTaps - 1) / 2;
    static constexpr int phaseBufferSize = 256;
    static constexpr double maximumCombinedDelayMs = 200.0;

    static int msToSamples(double ms, double sampleRate) noexcept;
    void initialisePhaseFilterCoefficients();
    void clearState();
    void updateDerivedParameters();
    blocks::Order activeOrder = blocks::defaultOrder;
    gain::Order activeGainOrder = gain::defaultOrder;
    bool hasProcessedSamples = false, orderFadingOut = false;
    int orderFadeLength = 1, orderFadeRemaining = 0;
    comb::StereoEngine combEngine;
    Parameters parameters;
    DerivedParameters derived;

    double currentSampleRate = 44100.0;
    int delayBufferSize = 1;
    int delayWritePosition = 0;
    int phaseWritePosition = 0;
    std::vector<double> leftDelayBuffer;
    std::vector<double> rightDelayBuffer;
    std::array<double, phaseBufferSize> leftPhaseBuffer {};
    std::array<double, phaseBufferSize> rightPhaseBuffer {};
    std::array<double, phaseFilterTaps> phaseFilterCoefficients {};
};

using ProcessorBank = ava::modules::SampleRangeBank<DspCore>;
}
