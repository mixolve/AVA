#include "DspCore.h"

#include <algorithm>
#include <cmath>

namespace dyn::dsp
{
namespace
{
double levelToDb(const double level)
{
    return 20.0 * std::log10(std::max(level, epsilon));
}
}

double DspCore::releaseTowards(ReleaseState& state,
                               const double current,
                               const double target,
                               const int durationSamples,
                               const bool logarithmic,
                               const double curve)
{
    if (durationSamples <= 0 || target >= current)
    {
        state.active = false;
        return target;
    }

    if (! state.active)
    {
        state.start = current;
        state.target = target;
        state.elapsedSamples = 0;
        state.durationSamples = durationSamples;
        state.active = true;
    }

    const auto progress = juce::jlimit(0.0,
                                        1.0,
                                        static_cast<double>(++state.elapsedSamples)
                                            / static_cast<double>(state.durationSamples));
    const auto curveAmount = logarithmic ? juce::jlimit(-1.0, 1.0, curve) : 0.0;
    const auto curvature = std::abs(curveAmount) * 99.0;
    const auto shapedProgress = curveAmount > 0.0
        ? std::log1p(curvature * progress) / std::log1p(curvature)
        : curveAmount < 0.0
            ? std::expm1(std::log1p(curvature) * progress) / curvature
            : progress;
    const auto released = state.start + (state.target - state.start) * shapedProgress;

    if (progress >= 1.0)
        state.active = false;

    return std::max(state.target, released);
}

DspCore::StereoSample DspCore::processSample(const double leftInput, const double rightInput)
{
    const auto inL = leftInput;
    const auto inR = rightInput;

    if (! tensionSmoothingInitialised)
    {
        smoothedTensions = derived.tensions;
        tensionSmoothingInitialised = true;
    }

    dryL[static_cast<size_t>(bufPosDry)] = inL;
    dryR[static_cast<size_t>(bufPosDry)] = inR;

    const auto posL = std::max(inL, 0.0);
    const auto negL = std::min(inL, 0.0);
    const auto posR = std::max(inR, 0.0);
    const auto negR = std::min(inR, 0.0);

    // Feed detector/release from pre-clip half-waves; clipped path is only for output blend.
    std::array<double, branchCount> branchDetectorInput {
        posL,
        negL,
        posR,
        negR
    };
    std::array<double, branchCount> effectiveThresholds {};

    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
    {
        const auto controlBase = safeAbs(branchDetectorInput[branchIndex]);

        if (controlBase > envBase[branchIndex])
        {
            envBase[branchIndex] = controlBase;
            baseReleaseStates[branchIndex].active = false;
        }
        else
        {
            envBase[branchIndex] = releaseTowards(baseReleaseStates[branchIndex],
                                                   envBase[branchIndex],
                                                   0.0,
                                                   derived.releaseSamples[branchIndex],
                                                   derived.releaseLogarithmic,
                                                   derived.releaseCurve);
        }

        const auto envelopeDb = levelToDb(envBase[branchIndex]);
        auto& adaptiveReference = adaptiveReferenceDb[branchIndex];

        if (envelopeDb >= adaptiveReference)
        {
            adaptiveReference = derived.adaptiveAttackCoefficient * adaptiveReference
                + (1.0 - derived.adaptiveAttackCoefficient) * envelopeDb;
            adaptiveHoldSamplesRemaining[branchIndex] = derived.adaptiveHoldSamples;
        }
        else if (adaptiveHoldSamplesRemaining[branchIndex] > 0)
        {
            --adaptiveHoldSamplesRemaining[branchIndex];
        }
        else
        {
            adaptiveReference = derived.adaptiveReleaseCoefficient * adaptiveReference
                + (1.0 - derived.adaptiveReleaseCoefficient) * envelopeDb;
        }

        const auto adaptiveThresholdDb = juce::jlimit(-96.0, 12.0, adaptiveReference + derived.adaptiveOffsetDb);
        const auto manualThresholdDb = levelToDb(derived.manualThresholds[branchIndex]);
        const auto thresholdDb = manualThresholdDb
            + (adaptiveThresholdDb - manualThresholdDb) * derived.adaptiveAmounts[branchIndex];
        effectiveThresholds[branchIndex] = dbToAmp(thresholdDb);

        auto& smoothedTension = smoothedTensions[branchIndex];
        smoothedTension = derived.tensionSmoothingCoefficient * smoothedTension
            + (1.0 - derived.tensionSmoothingCoefficient) * derived.tensions[branchIndex];
        const auto targetBase = tensionTarget(envBase[branchIndex],
                                              effectiveThresholds[branchIndex],
                                              derived.tensionFloor,
                                              derived.tensionHysteresis,
                                              smoothedTension);
        const auto baseGainTarget = envBase[branchIndex] > epsilon ? (targetBase / envBase[branchIndex]) : 1.0;
        baseGainState[branchIndex] = baseGainTarget;

        const auto baseNow = branchDetectorInput[branchIndex] * baseGainState[branchIndex];
        dmBase[branchIndex][static_cast<size_t>(bufPos)] = baseNow;

    }

    const std::array<double, channelCount> cleanDetectorInput {
        dmBase[branchLeftUp][static_cast<size_t>(bufPos)] + dmBase[branchLeftDown][static_cast<size_t>(bufPos)],
        dmBase[branchRightUp][static_cast<size_t>(bufPos)] + dmBase[branchRightDown][static_cast<size_t>(bufPos)]
    };
    const std::array<double, channelCount> cleanPositiveThresholds {
        effectiveThresholds[branchLeftUp],
        effectiveThresholds[branchRightUp]
    };
    const std::array<double, channelCount> cleanNegativeThresholds {
        effectiveThresholds[branchLeftDown],
        effectiveThresholds[branchRightDown]
    };
    const std::array<int, channelCount> cleanPositiveReleaseSamples {
        derived.releaseSamples[branchLeftUp],
        derived.releaseSamples[branchRightUp]
    };
    const std::array<int, channelCount> cleanNegativeReleaseSamples {
        derived.releaseSamples[branchLeftDown],
        derived.releaseSamples[branchRightDown]
    };

    for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex)
    {
        const auto peakNow = safeAbs(cleanDetectorInput[channelIndex]);
        const auto inputPositive = cleanDetectorInput[channelIndex] >= 0.0;
        const auto releaseTarget = inputPositive
            ? cleanPositiveThresholds[channelIndex]
            : cleanNegativeThresholds[channelIndex];
        const auto releaseSamples = inputPositive
            ? cleanPositiveReleaseSamples[channelIndex]
            : cleanNegativeReleaseSamples[channelIndex];

        if (peakNow > cleanEnvPeak[channelIndex])
        {
            cleanEnvPeak[channelIndex] = peakNow;
            cleanHoldSamples[channelIndex] = derived.peakHoldSamples;
            cleanReleaseStates[channelIndex].active = false;
        }
        else if (cleanHoldSamples[channelIndex] > 0)
        {
            --cleanHoldSamples[channelIndex];
            cleanReleaseStates[channelIndex].active = false;
        }
        else
        {
            cleanEnvPeak[channelIndex] = releaseTowards(cleanReleaseStates[channelIndex],
                                                         cleanEnvPeak[channelIndex],
                                                         releaseTarget,
                                                         releaseSamples,
                                                         derived.releaseLogarithmic,
                                                         derived.releaseCurve);
        }

        if (inputPositive != cleanInputPositive[channelIndex])
        {
            cleanInputPositive[channelIndex] = inputPositive;
            const auto threshold = inputPositive
                ? cleanPositiveThresholds[channelIndex]
                : cleanNegativeThresholds[channelIndex];
            const auto gainReference = std::max(cleanHalfPeak[channelIndex], cleanEnvPeak[channelIndex]);
            if (gainReference <= epsilon || gainReference <= threshold)
            {
                cleanGainState[channelIndex] = 1.0;
            }
            else if (derived.ratio >= 99.995)
            {
                cleanGainState[channelIndex] = juce::jlimit(0.0, 1.0, threshold / gainReference);
            }
            else
            {
                const auto reductionDb = (levelToDb(gainReference) - levelToDb(threshold))
                    * (1.0 - 1.0 / derived.ratio);
                cleanGainState[channelIndex] = dbToAmp(-reductionDb);
            }
            cleanHalfPeak[channelIndex] = peakNow;
        }
        else
        {
            cleanHalfPeak[channelIndex] = std::max(cleanHalfPeak[channelIndex], peakNow);
        }

    }

    const auto readPos = wrapIndex(bufPos - derived.lookaheadSamples, derived.bufferSize);
    std::array<double, branchCount> branchProcessed {};
    std::array<double, branchCount> branchOutput {};

    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
    {
        const auto delayedBase = dmBase[branchIndex][static_cast<size_t>(readPos)];
        const auto threshold = effectiveThresholds[branchIndex];
        const auto clipped = threshold > epsilon
            ? threshold * satShape(delayedBase / threshold, derived.clipKneeDb)
            : satShape(delayedBase, derived.clipKneeDb);

        branchProcessed[branchIndex] = clipped;
        branchOutput[branchIndex] = branchProcessed[branchIndex] * derived.branchOutGains[branchIndex];
    }

    const auto clippedOutL = branchOutput[branchLeftUp] + branchOutput[branchLeftDown];
    const auto clippedOutR = branchOutput[branchRightUp] + branchOutput[branchRightDown];
    const auto clippedDeltaL = branchProcessed[branchLeftUp] + branchProcessed[branchLeftDown];
    const auto clippedDeltaR = branchProcessed[branchRightUp] + branchProcessed[branchRightDown];
    const auto cleanGainL = cleanGainState[0];
    const auto cleanGainR = cleanGainState[1];
    const auto cleanDeltaL = (dmBase[branchLeftUp][static_cast<size_t>(readPos)]
        + dmBase[branchLeftDown][static_cast<size_t>(readPos)]) * cleanGainL;
    const auto cleanDeltaR = (dmBase[branchRightUp][static_cast<size_t>(readPos)]
        + dmBase[branchRightDown][static_cast<size_t>(readPos)]) * cleanGainR;
    const auto cleanOutL = (dmBase[branchLeftUp][static_cast<size_t>(readPos)] * derived.branchOutGains[branchLeftUp]
        + dmBase[branchLeftDown][static_cast<size_t>(readPos)] * derived.branchOutGains[branchLeftDown]) * cleanGainL;
    const auto cleanOutR = (dmBase[branchRightUp][static_cast<size_t>(readPos)] * derived.branchOutGains[branchRightUp]
        + dmBase[branchRightDown][static_cast<size_t>(readPos)] * derived.branchOutGains[branchRightDown]) * cleanGainR;
    const auto outL = clippedOutL + (cleanOutL - clippedOutL) * derived.morph;
    const auto outR = clippedOutR + (cleanOutR - clippedOutR) * derived.morph;
    const auto deltaProcessedL = clippedDeltaL + (cleanDeltaL - clippedDeltaL) * derived.morph;
    const auto deltaProcessedR = clippedDeltaR + (cleanDeltaR - clippedDeltaR) * derived.morph;

    const auto readPosDry = wrapIndex(bufPosDry - derived.latencySamples, derived.dryBufferSize);
    const auto delayedDryL = dryL[static_cast<size_t>(readPosDry)];
    const auto delayedDryR = dryR[static_cast<size_t>(readPosDry)];

    StereoSample output;
    output.left = derived.delta ? (delayedDryL - deltaProcessedL) : outL;
    output.right = derived.delta ? (delayedDryR - deltaProcessedR) : outR;

    bufPos = wrapIndex(bufPos + 1, derived.bufferSize);
    bufPosDry = wrapIndex(bufPosDry + 1, derived.dryBufferSize);

    return output;
}
}
