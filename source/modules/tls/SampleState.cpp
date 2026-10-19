#include "SampleProcessor.h"

#include <algorithm>
#include <utility>

namespace tls::dsp
{
namespace
{
constexpr double neutralEpsilon = 1.0e-9;

bool isNear(const double value, const double target) noexcept
{
    return std::abs(value - target) <= neutralEpsilon;
}
}

int DspCore::msToSamples(const double ms, const double sampleRate) noexcept
{
    return static_cast<int>(std::floor(ms * sampleRate / 1000.0 + 0.5));
}

void DspCore::initialisePhaseFilterCoefficients()
{
    constexpr auto pi = juce::MathConstants<double>::pi;

    for (int index = 0; index < phaseFilterTaps; ++index)
    {
        const auto k = index - phaseFilterLatency;
        auto coefficient = 0.0;

        if (k != 0 && (std::abs(k) & 1) != 0)
            coefficient = 2.0 / (pi * static_cast<double>(k));

        const auto window = 0.42
            - (0.5 * std::cos(2.0 * pi * index / (phaseFilterTaps - 1)))
            + (0.08 * std::cos(4.0 * pi * index / (phaseFilterTaps - 1)));
        phaseFilterCoefficients[static_cast<size_t>(index)] = coefficient * window;
    }
}

void DspCore::clearState()
{
    activeOrder = parameters.blockOrder;
    activeGainOrder = parameters.gainOrder;
    hasProcessedSamples = false;
    orderFadeRemaining = 0;
    orderFadingOut = false;
    delayWritePosition = 0;
    phaseWritePosition = 0;
    combEngine.reset();
    std::fill(leftDelayBuffer.begin(), leftDelayBuffer.end(), 0.0);
    std::fill(rightDelayBuffer.begin(), rightDelayBuffer.end(), 0.0);
    leftPhaseBuffer.fill(0.0);
    rightPhaseBuffer.fill(0.0);
}

void DspCore::updateDerivedParameters()
{
    const auto midGainDb = juce::jlimit(-99.0, 48.0, roundToParameterStep(parameters.gainMid));
    const auto sideGainDb = juce::jlimit(-99.0, 48.0, roundToParameterStep(parameters.gainSide));
    derived.midGain = parameters.gainMidMute ? 0.0 : dbToAmp(midGainDb);
    derived.sideGain = parameters.gainSideMute ? 0.0 : dbToAmp(sideGainDb);
    derived.leftGain = parameters.gainLMute
        ? 0.0
        : dbToAmp(juce::jlimit(-99.0, 48.0, roundToParameterStep(parameters.gainL)));
    derived.rightGain = parameters.gainRMute
        ? 0.0
        : dbToAmp(juce::jlimit(-99.0, 48.0, roundToParameterStep(parameters.gainR)));
    derived.linkedGain = parameters.gainLrMute
        ? 0.0
        : dbToAmp(juce::jlimit(-99.0, 48.0, roundToParameterStep(parameters.gainLr)));

    const auto leftPosition = roundToParameterStep(parameters.left) * 0.01;
    const auto rightPosition = roundToParameterStep(parameters.right) * 0.01;
    const auto leftTheta = (leftPosition + 1.0) * juce::MathConstants<double>::pi * 0.25;
    const auto rightTheta = (rightPosition + 1.0) * juce::MathConstants<double>::pi * 0.25;
    const auto lawDb = juce::jlimit(0.0f, 6.0f, parameters.law);
    const auto lawGainForPosition = [lawDb] (const double position)
    {
        const auto distanceFromCentre = juce::jlimit(0.0, 1.0, std::abs(position));
        const auto centreWeight = std::cos(distanceFromCentre * juce::MathConstants<double>::pi * 0.5);
        return dbToAmp(-lawDb * centreWeight);
    };

    derived.gLL = std::cos(leftTheta);
    derived.gLR = std::sin(leftTheta);
    derived.gRL = std::cos(rightTheta);
    derived.gRR = std::sin(rightTheta);

    const auto leftLawGain = lawGainForPosition(leftPosition);
    derived.gLL *= leftLawGain;
    derived.gLR *= leftLawGain;

    const auto rightLawGain = lawGainForPosition(rightPosition);
    derived.gRL *= rightLawGain;
    derived.gRR *= rightLawGain;

    derived.impactAmount = roundToParameterStep(parameters.impact) * 0.01;
    derived.midAmount = roundToParameterStep(parameters.mid) * 0.01;
    derived.sideAmount = roundToParameterStep(parameters.side) * 0.01;

    const auto orthogonalTheta = roundToParameterStep(parameters.degree) * (juce::MathConstants<double>::pi / 180.0);
    const auto c = std::cos(orthogonalTheta);
    const auto s = std::sin(orthogonalTheta);
    derived.orthogonalM11 = c;
    derived.orthogonalM12 = parameters.flipRight ? s : -s;
    derived.orthogonalM21 = s;
    derived.orthogonalM22 = parameters.flipRight ? -c : c;

    derived.listenMode = ListenMode::neutral;
    if (parameters.listenLc)
        derived.listenMode = ListenMode::leftCenter;
    else if (parameters.listenRc)
        derived.listenMode = ListenMode::rightCenter;
    else if (parameters.listenMc)
        derived.listenMode = ListenMode::midCenter;
    else if (parameters.listenSc)
        derived.listenMode = ListenMode::sideCenter;
    else if (parameters.listenLl)
        derived.listenMode = ListenMode::leftLeft;
    else if (parameters.listenRr)
        derived.listenMode = ListenMode::rightRight;
    else if (parameters.listenSs)
        derived.listenMode = ListenMode::sideStereo;
    derived.listenMsGain = parameters.listenMsPlus3 ? dbToAmp(3.0) : 1.0;

    const auto stereoDelayMs = juce::jlimit(-100.0, 100.0, roundToParameterStep(parameters.stereoDelayMs));
    const auto leftDelayMs = juce::jlimit(-100.0, 100.0, roundToParameterStep(parameters.leftDelayMs));
    const auto rightDelayMs = juce::jlimit(-100.0, 100.0, roundToParameterStep(parameters.rightDelayMs));
    const auto leftOffsetMs = stereoDelayMs + leftDelayMs;
    const auto rightOffsetMs = stereoDelayMs + rightDelayMs;
    const auto lookaheadMs = juce::jmax(0.0, -juce::jmin(leftOffsetMs, rightOffsetMs));
    const auto lookaheadSamples = msToSamples(lookaheadMs, currentSampleRate);
    derived.leftDelaySamples = msToSamples(lookaheadMs + leftOffsetMs, currentSampleRate);
    derived.rightDelaySamples = msToSamples(lookaheadMs + rightOffsetMs, currentSampleRate);
    derived.delayEnabled = !parameters.blockBypass[size_t(blocks::Id::delay)] && !parameters.blockHidden[size_t(blocks::Id::delay)]
        && (!isNear(leftOffsetMs,0.0) || !isNear(rightOffsetMs,0.0));

    if (! derived.delayEnabled)
    {
        derived.leftDelaySamples = 0;
        derived.rightDelaySamples = 0;
    }

    const auto stereoPhase = juce::jlimit(-180.0, 180.0, roundToParameterStep(parameters.stereoPhase));
    const auto phaseL = std::remainder(stereoPhase + juce::jlimit(-180.0, 180.0, roundToParameterStep(parameters.leftPhase)), 360.0)
        * (juce::MathConstants<double>::pi / 180.0);
    const auto phaseR = std::remainder(stereoPhase + juce::jlimit(-180.0, 180.0, roundToParameterStep(parameters.rightPhase)), 360.0)
        * (juce::MathConstants<double>::pi / 180.0);
    derived.phaseEnabled = !parameters.blockBypass[size_t(blocks::Id::phase)] && !parameters.blockHidden[size_t(blocks::Id::phase)]
        && (std::abs(phaseL)>neutralEpsilon || std::abs(phaseR)>neutralEpsilon);
    derived.combEnabled=parameters.combCount>0 && !parameters.combCountBypass
        && !parameters.blockBypass[size_t(blocks::Id::comb)] && !parameters.blockHidden[size_t(blocks::Id::comb)];
    if(derived.combEnabled) combEngine.setComb(static_cast<float>(juce::jlimit(2,100,parameters.combCount)));
    combEngine.setShift(parameters.combShiftBypass ? 0.0f : juce::jlimit(-50.0f,50.0f,parameters.combShift));
    derived.latencySamples = (derived.delayEnabled ? lookaheadSamples : 0) + (derived.phaseEnabled ? phaseFilterLatency : 0)
        + (derived.combEnabled ? combEngine.getLatencySamples() : 0);
    derived.leftPhaseCosine = std::cos(phaseL);
    derived.leftPhaseSine = std::sin(phaseL);
    derived.rightPhaseCosine = std::cos(phaseR);
    derived.rightPhaseSine = std::sin(phaseR);
    auto& active=derived.blockEnabled;
    active[size_t(blocks::Id::listen)]=derived.listenMode!=ListenMode::neutral;
    active[size_t(blocks::Id::gain)]=
        (!parameters.gainBypass[size_t(gain::Id::stereo)] && !isNear(derived.linkedGain,1.0))
        || (!parameters.gainBypass[size_t(gain::Id::left)] && !isNear(derived.leftGain,1.0))
        || (!parameters.gainBypass[size_t(gain::Id::right)] && !isNear(derived.rightGain,1.0))
        || (!parameters.gainBypass[size_t(gain::Id::mid)] && !isNear(derived.midGain,1.0))
        || (!parameters.gainBypass[size_t(gain::Id::side)] && !isNear(derived.sideGain,1.0));
    active[size_t(blocks::Id::delay)]=derived.delayEnabled;
    active[size_t(blocks::Id::phase)]=derived.phaseEnabled;
    active[size_t(blocks::Id::panorama)]=!isNear(derived.gLL,1.0) || !isNear(derived.gLR,0.0)
        || !isNear(derived.gRL,0.0) || !isNear(derived.gRR,1.0);
    active[size_t(blocks::Id::shear)]=!isNear(derived.impactAmount,0.0);
    active[size_t(blocks::Id::balance)]=!isNear(derived.midAmount,0.0) || !isNear(derived.sideAmount,0.0);
    active[size_t(blocks::Id::ortho)]=!isNear(derived.orthogonalM11,1.0) || !isNear(derived.orthogonalM12,0.0)
        || !isNear(derived.orthogonalM21,0.0) || !isNear(derived.orthogonalM22,1.0);
    active[size_t(blocks::Id::rect)]=parameters.halfPositive || parameters.halfNegative
        || parameters.fullPositive || parameters.fullNegative;
    active[size_t(blocks::Id::comb)]=derived.combEnabled;
    for(size_t i=0;i<active.size();++i) active[i]=active[i] && !parameters.blockBypass[i] && !parameters.blockHidden[i];
    derived.signalTransformEnabled=std::any_of(active.begin(),active.end(),[](bool value){return value;});

}

bool DspCore::isNeutral() const noexcept
{
    return !derived.signalTransformEnabled;
}
}
