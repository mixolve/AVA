#include "SampleProcessor.h"
#include "ProgressionTransfer.h"

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

double shapedBallisticsProgress(const double progress, const double curve)
{
    const auto curveAmount = juce::jlimit(-1.0, 1.0, curve);
    constexpr double maximumExponent = 4.605170185988091; // log(100).
    // Distribute midpoint deviation from LIN evenly across the control range.
    // For a normalised exponential that deviation is 0.5 * tanh(exponent / 4).
    const auto exponent = 4.0 * std::atanh(std::abs(curveAmount) * std::tanh(maximumExponent * 0.25));
    const auto normalisation = std::expm1(exponent);
    // Both signs use the same exponential, reflected around the linear release.
    return curveAmount > 0.0
        ? 1.0 - std::expm1(exponent * (1.0 - progress)) / normalisation
        : curveAmount < 0.0
            ? std::expm1(exponent * progress) / normalisation
            : progress;
}

bool refreshHeldPeak(const double level, const double heldLevel, const int holdSamples)
{
    if (level > heldLevel)
        return true;

    // Sampled peaks of a steady carrier vary slightly with its phase relative
    // to the sample grid. Refresh HOLD on equal peaks as well, so it starts
    // after the level falls rather than expiring periodically during a tone.
    constexpr double peakTolerance = 0.001;
    return holdSamples > 0 && heldLevel > epsilon
        && level >= heldLevel * (1.0 - peakTolerance);
}

double compressorGain(const double level, const double threshold,
                      const double gainExponent, const double kneeDb,
                      const double progression = 0.0, const double span = 12.0, const double feedback = 0.0)
{
    if (progression != 0.0 && level > epsilon)
    {
        const auto baseExponent = gainExponent / (1.0 + feedback * gainExponent);
        // The limiter has infinite ratio, so positive progression must not enter the finite-ratio formula.
        if (progression < 0.0 || baseExponent < 0.998999)
        {
            const auto ratio = 1.0 / std::max(0.01, 1.0 - baseExponent);
            return dbToAmp(-transfer::feedbackReduction(levelToDb(level) - levelToDb(threshold),
                ratio, progression, span, kneeDb, feedback));
        }
    }
    if (level <= epsilon || gainExponent <= epsilon)
        return 1.0;

    const auto aboveThresholdDb = levelToDb(level) - levelToDb(threshold);
    auto reductionDb = std::max(0.0, aboveThresholdDb) * gainExponent;
    if (kneeDb > epsilon)
    {
        const auto halfKnee = kneeDb * 0.5;
        if (aboveThresholdDb <= -halfKnee)
            reductionDb = 0.0;
        else if (aboveThresholdDb < halfKnee)
        {
            const auto distance = aboveThresholdDb + halfKnee;
            const auto u = distance / kneeDb;
            reductionDb = gainExponent * kneeDb * 0.5 * u * u;
        }
    }
    return dbToAmp(-reductionDb);
}

double compressorLevelForGain(const double gain, const double threshold,
                             const double gainExponent, const double kneeDb,
                             const double progression = 0.0, const double span = 12.0)
{
    if (progression != 0.0 && (progression < 0.0 || gainExponent < 0.998999))
    {
        const auto ratio = 1.0 / std::max(0.01, 1.0 - gainExponent);
        return threshold * dbToAmp(transfer::distanceForReduction(-levelToDb(gain), ratio, progression, span, kneeDb));
    }
    const auto reductionDb = -levelToDb(gain) / gainExponent;
    const auto aboveThresholdDb = kneeDb > epsilon && reductionDb < kneeDb * 0.5
        ? std::sqrt(std::max(0.0, 2.0 * kneeDb * reductionDb)) - kneeDb * 0.5
        : reductionDb;
    return threshold * dbToAmp(aboveThresholdDb);
}
}

double DspCore::getEffectiveThreshold(const size_t branchIndex) const noexcept
{
    const auto adaptiveDb = juce::jlimit(-96.0, 12.0,
        adaptiveReferenceDb[branchIndex] + derived.adaptiveOffsetDb);
    const auto manualDb = 20.0 * std::log10(std::max(derived.manualThresholds[branchIndex], epsilon));
    return dbToAmp(manualDb + (adaptiveDb - manualDb) * derived.adaptiveAmounts[branchIndex]);
}

double DspCore::releaseTowards(ReleaseState& state,
                               const double current,
                               const double target,
                               const int durationSamples,
                               const double curve,
                               const double gainExponent,
                               const int holdSamples,
                               const double kneeDb,
                               const double minimumGain,
                               const double gainThreshold,
                               const bool followTarget)
{
    if (target >= current || (durationSamples <= 0 && holdSamples <= 0))
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
        state.holdSamplesRemaining = std::max(0, holdSamples);
        state.active = true;
    }

    if (followTarget) state.target = target;

    if (state.holdSamplesRemaining > 0)
    {
        --state.holdSamplesRemaining;
        return state.start;
    }

    if (state.durationSamples <= 0)
    {
        state.active = false;
        return state.target;
    }

    const auto progress = juce::jlimit(0.0,
                                        1.0,
                                        static_cast<double>(++state.elapsedSamples)
                                            / static_cast<double>(state.durationSamples));
    const auto shapedProgress = shapedBallisticsProgress(progress, curve);
    auto released = state.start + (state.target - state.start) * shapedProgress;

    if (gainExponent > epsilon && state.target > epsilon)
    {
        // The compressor applies (threshold / envelope)^exponent. Shape the
        // audible amplitude gain first, then invert it back to detector level.
        // A linear detector decay would otherwise produce a curved recovery.
        const auto threshold = gainThreshold > epsilon ? gainThreshold : state.target * dbToAmp(kneeDb * 0.5);
        const auto startGain = std::max(minimumGain, compressorGain(state.start, threshold, gainExponent, kneeDb, derived.progression, derived.progressionSpan));
        const auto targetGain = gainThreshold > epsilon
            ? std::max(minimumGain, compressorGain(state.target, threshold, gainExponent, kneeDb, derived.progression, derived.progressionSpan)) : 1.0;
        const auto gain = startGain + (targetGain - startGain) * shapedProgress;
        released = compressorLevelForGain(gain, threshold, gainExponent, kneeDb, derived.progression, derived.progressionSpan);
    }

    if (progress >= 1.0)
        state.active = false;

    return std::max(state.target, released);
}

void DspCore::prepareSignalPeak(SignalPeakState& state, const bool positive)
{
    const auto capacity = state.queue.size();
    if (positive != state.positive)
    {
        if (state.hasCrossing)
            state.window = juce::jlimit<size_t>(1, capacity - 1,
                static_cast<size_t>(2 * (state.sample - state.lastCrossing)));
        state.lastCrossing = state.sample;
        state.hasCrossing = true;
        state.positive = positive;
    }
    // Keep one carrier period, so SIGNAL follows amplitude rather than falling
    // to zero between peaks. The finite window also releases on silence or DC steps.
    while (state.count > 0 && state.sample - state.queue[state.head].sample >= state.window)
    {
        state.head = (state.head + 1) % capacity;
        --state.count;
    }
}

double DspCore::updateSignalPeak(SignalPeakState& state, const double level, const bool positive)
{
    prepareSignalPeak(state, positive);
    const auto capacity = state.queue.size();
    while (state.count > 0 && state.queue[(state.head + state.count - 1) % capacity].level <= level)
        --state.count;
    state.queue[(state.head + state.count) % capacity] = { level, state.sample++ };
    ++state.count;
    return state.queue[state.head].level;
}

void DspCore::updateCompressorGain(const size_t detectorIndex, const double peakNow,
                                   const bool inputPositive, const double releaseSignal,
                                   const double releaseThreshold, const int releaseSamples,
                                   const double gainExponent, const double trackingBlend, const double feedbackGainScale)
{
    const auto releaseTarget = releaseThreshold * dbToAmp(-derived.compKneeDb * 0.5);
    if (derived.thresholdHysteresisDb <= 0.0) cleanThresholdHysteresis[detectorIndex] = {};
    const auto holdingThreshold = derived.thresholdHysteresisDb > 0.0 && cleanThresholdHysteresis[detectorIndex].holds(
        levelToDb(releaseSignal * feedbackGainScale) - levelToDb(releaseTarget), derived.thresholdHysteresisDb);
    if (! holdingThreshold)
    {
        if (refreshHeldPeak(peakNow, cleanEnvPeak[detectorIndex], derived.holdSamples))
        {
            cleanEnvPeak[detectorIndex] = std::max(cleanEnvPeak[detectorIndex], peakNow);
            cleanReleaseStates[detectorIndex].active = false;
        }
        else
        {
            cleanEnvPeak[detectorIndex] = releaseTowards(cleanReleaseStates[detectorIndex],
                                                         cleanEnvPeak[detectorIndex],
                                                         releaseTarget + (std::max(releaseTarget, releaseSignal) - releaseTarget) * derived.releaseType,
                                                         releaseSamples,
                                                         derived.releaseCurve,
                                                         trackingBlend > 0.0 ? derived.releaseGainExponent : gainExponent,
                                                         derived.holdSamples,
                                                         derived.compKneeDb,
                                                         derived.rangeCurve > 0.0 ? 0.0 : derived.minimumGain,
                                                         derived.releaseType > 0.0 ? releaseThreshold : 0.0,
                                                         derived.releaseType > 0.0);
        }

        const auto updateHalfPeak = inputPositive != cleanInputPositive[detectorIndex] || detectorIndex == detector::rms;
        // Follow the SIGNAL destination throughout the release instead of sampling
        // the recovering gain only at the carrier zero crossings.
        if (updateHalfPeak || derived.releaseType > 0.0 || trackingBlend > 0.0)
        {
            cleanInputPositive[detectorIndex] = inputPositive;
            const auto threshold = releaseThreshold;
            const auto referencePeak = (derived.releaseType > 0.0 || trackingBlend > 0.0)
                ? releaseSignal : cleanHalfPeak[detectorIndex];
            const auto gainReference = std::max(referencePeak, cleanEnvPeak[detectorIndex]);
            const auto target = std::max(derived.rangeCurve > 0.0 ? 0.0 : derived.minimumGain,
                compressorGain(gainReference * feedbackGainScale, threshold, gainExponent, derived.compKneeDb,
                    derived.progression, derived.progressionSpan, trackingBlend));
            if (updateHalfPeak || derived.releaseType > 0.0)
                cleanGainTarget[detectorIndex] = target;
            else
                cleanGainTarget[detectorIndex] += (target - cleanGainTarget[detectorIndex]) * trackingBlend;
            cleanHalfPeak[detectorIndex] = updateHalfPeak
                ? peakNow : std::max(cleanHalfPeak[detectorIndex], peakNow);
        }
        else
        {
            cleanHalfPeak[detectorIndex] = std::max(cleanHalfPeak[detectorIndex], peakNow);
        }

    }
    else
    {
        // Keep the captured gain/envelope; pending delay/attack can still finish.
        cleanReleaseStates[detectorIndex].active = false;
        cleanInputPositive[detectorIndex] = inputPositive;
        cleanHalfPeak[detectorIndex] = peakNow;
    }

    auto& attack = cleanAttackStates[detectorIndex];
    auto& gain = cleanGainState[detectorIndex];
    const auto targetGain = cleanGainTarget[detectorIndex];
    if (targetGain >= gain || (derived.attackSamples <= 0 && derived.delaySamples <= 0))
    {
        gain = targetGain;
        attack.active = false;
    }
    else
    {
        if (! attack.active)
        {
            attack.startGain = gain;
            attack.elapsedSamples = 0;
            attack.durationSamples = derived.attackSamples;
            attack.delaySamplesRemaining = derived.delaySamples;
            attack.active = true;
        }
        if (attack.delaySamplesRemaining > 0)
        {
            --attack.delaySamplesRemaining;
            gain = attack.startGain;
        }
        else if (attack.durationSamples <= 0)
        {
            gain = targetGain;
            attack.active = false;
        }
        else
        {
            // Repeated carrier peaks update the destination without restarting delay or attack.
            const auto progress = std::min(1.0, static_cast<double>(++attack.elapsedSamples)
                                                   / static_cast<double>(attack.durationSamples));
            gain = attack.startGain + (targetGain - attack.startGain)
                * shapedBallisticsProgress(progress, derived.attackCurve);
            if (progress >= 1.0)
                attack.active = false;
        }
    }

}

double DspCore::alignLookaheadGain(const double gain)
{
    if (derived.lookaheadSamples == 0)
        return gain;

    auto& state = lookaheadGain;
    const auto capacity = state.queue.size();
    const auto window = static_cast<juce::uint64>(derived.lookaheadSamples);
    // Include the gain computed for the sample leaving the audio buffer, as well
    // as all future gains within LOOKAHEAD. Attack can anticipate, but release
    // cannot expose audio whose detector event is still in the delay line.
    while (state.count > 0 && state.sample - state.queue[state.head].sample > window)
    {
        state.head = (state.head + 1) % capacity;
        --state.count;
    }
    while (state.count > 0 && state.queue[(state.head + state.count - 1) % capacity].level >= gain)
        --state.count;
    state.queue[(state.head + state.count) % capacity] = { gain, state.sample++ };
    ++state.count;
    return state.queue[state.head].level;
}

DspCore::StereoSample DspCore::processSample(const double leftInput, const double rightInput,
                                               const double sidechainLeft, const double sidechainRight)
{
    const auto inL = leftInput * derived.inputGain;
    const auto inR = rightInput * derived.inputGain;
    const auto keyL = parameters.useSidechainInput ? sidechainLeft : inL;
    const auto keyR = parameters.useSidechainInput ? sidechainRight : inR;
    const std::array<double, channelCount> rmsInput { keyL, keyR };
    std::array<double, channelCount> adaptiveRmsDb {};
    const auto rmsCapacity = adaptiveRmsPower[0].size();
    const auto leavingPosition = (adaptiveRmsPosition + rmsCapacity - adaptiveRmsWindowSamples) % rmsCapacity;
    adaptiveRmsSampleCount = std::min(adaptiveRmsSampleCount + 1, rmsCapacity);
    for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex)
    {
        const auto power = rmsInput[channelIndex] * rmsInput[channelIndex];
        adaptiveRmsPowerSum[channelIndex] += power - adaptiveRmsPower[channelIndex][leavingPosition];
        adaptiveRmsPower[channelIndex][adaptiveRmsPosition] = power;
        const auto meanPower = std::max(0.0, adaptiveRmsPowerSum[channelIndex])
            / static_cast<double>(std::min(adaptiveRmsSampleCount, adaptiveRmsWindowSamples));
        adaptiveRmsDb[channelIndex] = levelToDb(std::sqrt(meanPower));
    }
    adaptiveRmsPosition = (adaptiveRmsPosition + 1) % rmsCapacity;
    const auto stereoAdaptiveRmsDb = std::max(adaptiveRmsDb[0], adaptiveRmsDb[1]);
    const auto stereoKeyPeak = std::max(std::abs(keyL), std::abs(keyR));
    const auto keyPositive = std::abs(keyL) >= std::abs(keyR) ? keyL >= 0.0 : keyR >= 0.0;
    const auto baseReleaseSignal = updateSignalPeak(baseSignalPeak, stereoKeyPeak, keyPositive);

    double baseRms = 0.0;
    const auto rmsSize = baseRmsPower[0].size();
    const auto baseLeaving = (detectorRmsPosition + rmsSize - detectorRmsWindowSamples) % rmsSize;
    const auto count = std::min(detectorRmsSampleCount + 1, rmsSize);
    for (size_t channel = 0; channel < channelCount; ++channel)
    {
        const auto power = rmsInput[channel] * rmsInput[channel];
        baseRmsPowerSum[channel] += power - baseRmsPower[channel][baseLeaving];
        baseRmsPower[channel][detectorRmsPosition] = power;
        baseRms = std::max(baseRms, std::sqrt(std::max(0.0, baseRmsPowerSum[channel])
            / static_cast<double>(std::min(count, detectorRmsWindowSamples))));
    }
    const std::array<double, detector::count> baseLevels { stereoKeyPeak * derived.peakScale, baseRms };
    const std::array<double, detector::count> baseDestinations { baseReleaseSignal * derived.peakScale, baseRms };
    auto effectiveReleaseSamples = derived.releaseSamples;
    for (size_t path = 0; path < detector::count; ++path)
    {
        if (derived.autoRelease > 0.0)
        {
            const auto level = baseDestinations[path];
            const auto threshold = getEffectiveThreshold(keyPositive ? branchLeftUp : branchLeftDown);
            const auto compressorActive = derived.morph > 0.0
                && (derived.releaseGainExponent > epsilon || derived.progression > 0.0)
                && levelToDb(level) > levelToDb(threshold) - derived.compKneeDb * 0.5;
            const auto tensionActive = std::abs(derived.tensions[0]) > epsilon
                && level > epsilon && level < threshold;
            auto strength = 1.0;
            if (derived.releaseDepth > 0.0)
            {
                const auto compressionDb = compressorActive ? -levelToDb(compressorGain(level, threshold,
                    derived.releaseGainExponent, derived.compKneeDb, derived.progression, derived.progressionSpan)) * derived.morph : 0.0;
                const auto tensionDb = tensionActive ? std::abs(levelToDb(tensionTarget(level, threshold,
                    derived.tensionFloor, derived.tensionHysteresis, derived.tensions[0])) - levelToDb(level)) : 0.0;
                strength = std::min(derived.rangeDb, std::max(compressionDb, tensionDb)) / 12.0;
            }
            releaseMemories[path].advance(compressorActive || tensionActive,
                derived.releaseMemoryCoefficient, derived.autoRelease, derived.releaseDepth, strength);
            effectiveReleaseSamples[path] = static_cast<int>(std::round(derived.releaseSamples[path]
                * releaseMemories[path].multiplier(derived.autoRelease)));
        }
        else releaseMemories[path] = {};
        if (refreshHeldPeak(baseLevels[path], envBase[path], derived.holdSamples))
        {
            envBase[path] = std::max(envBase[path], baseLevels[path]);
            baseReleaseStates[path].active = false;
        }
        else
            envBase[path] = releaseTowards(baseReleaseStates[path], envBase[path],
                baseDestinations[path] * derived.releaseType, effectiveReleaseSamples[path],
                derived.releaseCurve, 0.0, derived.holdSamples, 0.0, 0.0, 0.0, derived.releaseType > 0.0);
    }
    const auto baseEnvelope = detector::mix(envBase[detector::peak], envBase[detector::rms], derived.rmsMix);

    if (! tensionSmoothingInitialised)
    {
        smoothedTensions = derived.tensions;
        tensionSmoothingInitialised = true;
    }

    dryL[static_cast<size_t>(bufPosDry)] = inL;
    dryR[static_cast<size_t>(bufPosDry)] = inR;
    sidechainListenL[static_cast<size_t>(bufPosDry)] = keyL;
    sidechainListenR[static_cast<size_t>(bufPosDry)] = keyR;

    if (parameters.transust)
        return processTransustSample(keyL, keyR, stereoAdaptiveRmsDb, baseReleaseSignal);

    const auto posL = std::max(inL, 0.0);
    const auto negL = std::min(inL, 0.0);
    const auto posR = std::max(inR, 0.0);
    const auto negR = std::min(inR, 0.0);

    // Feed detector/release from pre-clip half-waves; clipped path is only for output blend.
    const std::array<double, branchCount> branchAudioInput { posL, negL, posR, negR };
    const std::array<double, branchCount> branchDetectorInput {
        std::max(keyL, 0.0), std::min(keyL, 0.0), std::max(keyR, 0.0), std::min(keyR, 0.0)
    };
    std::array<double, branchCount> branchBaseDetector {};
    std::array<double, branchCount> effectiveThresholds {};

    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
    {
        const auto envelopeDb = stereoAdaptiveRmsDb;
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

        effectiveThresholds[branchIndex] = getEffectiveThreshold(branchIndex);

        auto& smoothedTension = smoothedTensions[branchIndex];
        smoothedTension = derived.tensionSmoothingCoefficient * smoothedTension
            + (1.0 - derived.tensionSmoothingCoefficient) * derived.tensions[branchIndex];
        const auto targetBase = tensionTarget(baseEnvelope,
                                              effectiveThresholds[branchIndex],
                                              derived.tensionFloor,
                                              derived.tensionHysteresis,
                                              smoothedTension);
        const auto shapedGain = baseEnvelope > epsilon ? (targetBase / baseEnvelope) : 1.0;
        auto baseGainTarget = shapedGain;
        if (derived.thresholdHysteresisDb > 0.0 && std::abs(smoothedTension) > epsilon)
            baseGainTarget = tensionThresholdHysteresis[branchIndex].process(shapedGain,
                levelToDb(effectiveThresholds[branchIndex]) - levelToDb(baseEnvelope), derived.thresholdHysteresisDb);
        else
            tensionThresholdHysteresis[branchIndex] = {};
        baseGainState[branchIndex] = baseGainTarget;

        branchBaseDetector[branchIndex] = branchDetectorInput[branchIndex] * baseGainState[branchIndex];
        const auto baseNow = branchAudioInput[branchIndex] * baseGainState[branchIndex];
        dmBase[branchIndex][static_cast<size_t>(bufPos)] = baseNow;

    }

    const std::array<double, channelCount> cleanDetectorInput {
        branchBaseDetector[branchLeftUp] + branchBaseDetector[branchLeftDown],
        branchBaseDetector[branchRightUp] + branchBaseDetector[branchRightDown]
    };
    const auto limitReduction = [this] (const double dry, const double processed)
    {
        if (derived.rangeCurve > epsilon && derived.rangeDb > epsilon && std::abs(dry) > epsilon)
        {
            const auto reductionDb = -20.0 * std::log10(std::max(epsilon, std::abs(processed / dry)));
            const auto width = derived.rangeDb * derived.rangeCurve;
            const auto lower = derived.rangeDb - width;
            if (reductionDb <= lower) return processed;
            const auto distance = reductionDb - lower;
            const auto boundedDb = reductionDb >= derived.rangeDb + width
                ? derived.rangeDb : reductionDb - distance * distance / (4.0 * width);
            return dry * dbToAmp(-boundedDb);
        }
        const auto minimumAmplitude = std::abs(dry) * derived.minimumGain;
        return std::abs(processed) < minimumAmplitude
            ? std::copysign(minimumAmplitude, dry) : processed;
    };
    if (! feedbackInitialised)
    {
        smoothedFeedback = derived.feedback;
        feedbackInitialised = true;
    }
    smoothedFeedback = derived.feedbackSmoothingCoefficient * smoothedFeedback
        + (1.0 - derived.feedbackSmoothingCoefficient) * derived.feedback;
    if (std::abs(smoothedFeedback - derived.feedback) < 1.0e-8)
        smoothedFeedback = derived.feedback;
    const auto feedbackAmount = derived.morph > 0.0 ? smoothedFeedback : 0.0;
    if (feedbackAmount <= 0.0)
    {
        std::array<double, channelCount> detectorRms {};
        const auto detectorCapacity = detectorRmsPower[0].size();
        const auto detectorLeaving = (detectorRmsPosition + detectorCapacity - detectorRmsWindowSamples) % detectorCapacity;
        detectorRmsSampleCount = std::min(detectorRmsSampleCount + 1, detectorCapacity);
        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            const auto power = cleanDetectorInput[channel] * cleanDetectorInput[channel];
            detectorRmsPowerSum[channel] += power - detectorRmsPower[channel][detectorLeaving];
            detectorRmsPower[channel][detectorRmsPosition] = power;
            detectorRms[channel] = std::sqrt(std::max(0.0, detectorRmsPowerSum[channel])
                / static_cast<double>(std::min(detectorRmsSampleCount, detectorRmsWindowSamples)));
        }
        detectorRmsPosition = (detectorRmsPosition + 1) % detectorCapacity;
        const auto strongestChannel = std::abs(cleanDetectorInput[0]) >= std::abs(cleanDetectorInput[1]) ? size_t { 0 } : size_t { 1 };
        const auto stereoPeak = std::abs(cleanDetectorInput[strongestChannel]);
        const auto stereoRms = std::max(detectorRms[0], detectorRms[1]);
        const auto stereoPositive = cleanDetectorInput[strongestChannel] >= 0.0;
        const auto signalPeak = updateSignalPeak(cleanSignalPeak, stereoPeak, stereoPositive);
        const auto threshold = effectiveThresholds[stereoPositive ? branchLeftUp : branchLeftDown];
        updateCompressorGain(detector::peak, stereoPeak * derived.peakScale, stereoPositive,
            signalPeak * derived.peakScale, threshold, effectiveReleaseSamples[detector::peak],
            derived.releaseGainExponent, 0.0);
        updateCompressorGain(detector::rms, stereoRms, stereoPositive, stereoRms, threshold,
            effectiveReleaseSamples[detector::rms], derived.releaseGainExponent, 0.0);
    }
    else
    {
        // Keep peak/RMS ballistics in the source domain, then apply feedback to
        // the gain computer. Otherwise our own attack is mistaken for a falling
        // source and the stored unattenuated peak causes an undershoot.
        // Blend the pre/post gain keys in dB. Calibrate the feedback gain law so
        // the steady-state RATIO is unchanged: k = a / (1 - feedback * a).
        const auto exponent = std::min(derived.releaseGainExponent, 1.0 - 1.0e-3);
        const auto loopExponent = exponent / (1.0 - feedbackAmount * exponent);
        const auto strongest = std::abs(cleanDetectorInput[0]) >= std::abs(cleanDetectorInput[1]) ? size_t { 0 } : size_t { 1 };
        const auto positive = cleanDetectorInput[strongest] >= 0.0;
        const auto keyPeak = std::abs(cleanDetectorInput[strongest]);
        const auto threshold = effectiveThresholds[positive ? branchLeftUp : branchLeftDown];
        const auto capacity = detectorRmsPower[0].size();
        const auto leaving = (detectorRmsPosition + capacity - detectorRmsWindowSamples) % capacity;
        const auto sampleCount = std::min(detectorRmsSampleCount + 1, capacity);
        const auto denominator = static_cast<double>(std::min(sampleCount, detectorRmsWindowSamples));
        prepareSignalPeak(cleanSignalPeak, positive);
        const auto historicalPeak = cleanSignalPeak.count > 0 ? cleanSignalPeak.queue[cleanSignalPeak.head].level : 0.0;
        const auto savedEnv = cleanEnvPeak;
        const auto savedRelease = cleanReleaseStates;
        const auto savedAttack = cleanAttackStates;
        const auto savedGain = cleanGainState;
        const auto savedTarget = cleanGainTarget;
        const auto savedHalfPeak = cleanHalfPeak;
        const auto savedPositive = cleanInputPositive;
        const auto savedThresholdHysteresis = cleanThresholdHysteresis;
        const auto evaluate = [&] (const double candidate)
        {
            cleanEnvPeak = savedEnv;
            cleanReleaseStates = savedRelease;
            cleanAttackStates = savedAttack;
            cleanGainState = savedGain;
            cleanGainTarget = savedTarget;
            cleanHalfPeak = savedHalfPeak;
            cleanInputPositive = savedPositive;
            cleanThresholdHysteresis = savedThresholdHysteresis;
            const auto scale = std::pow(std::max(candidate, epsilon), feedbackAmount);
            double rms = 0.0;
            for (size_t channel = 0; channel < channelCount; ++channel)
            {
                const auto key = cleanDetectorInput[channel];
                const auto power = detectorRmsPowerSum[channel] + key * key - detectorRmsPower[channel][leaving];
                rms = std::max(rms, std::sqrt(std::max(0.0, power) / denominator));
            }
            const auto heldPeak = std::max(historicalPeak, keyPeak);
            updateCompressorGain(detector::peak, keyPeak * derived.peakScale, positive,
                heldPeak * derived.peakScale, threshold, effectiveReleaseSamples[detector::peak], loopExponent, feedbackAmount, scale);
            updateCompressorGain(detector::rms, rms, positive, rms, threshold,
                effectiveReleaseSamples[detector::rms], loopExponent, feedbackAmount, scale);
            return limitReduction(1.0, detector::mix(cleanGainState[detector::peak], cleanGainState[detector::rms], derived.rmsMix));
        };
        // Solve the same-sample negative-feedback loop. Each trial restores all
        // ballistics; only the final solution advances time or commits history.
        double lower = 0.0, upper = 1.0;
        auto solvedGain = limitReduction(1.0, detector::mix(savedGain[detector::peak], savedGain[detector::rms], derived.rmsMix));
        for (int iteration = 0; iteration < 34; ++iteration)
        {
            const auto predicted = evaluate(solvedGain);
            if (std::abs(predicted - solvedGain) < 1.0e-9) break;
            if (predicted > solvedGain) lower = solvedGain;
            else upper = solvedGain;
            solvedGain = (lower + upper) * 0.5;
        }
        evaluate(solvedGain);
        detectorRmsSampleCount = sampleCount;
        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            const auto key = cleanDetectorInput[channel];
            const auto power = key * key;
            detectorRmsPowerSum[channel] += power - detectorRmsPower[channel][leaving];
            detectorRmsPower[channel][detectorRmsPosition] = power;
        }
        detectorRmsPosition = (detectorRmsPosition + 1) % capacity;
        updateSignalPeak(cleanSignalPeak, keyPeak, positive);
    }

    const auto readPos = wrapIndex(bufPos - derived.lookaheadSamples, derived.bufferSize);
    std::array<double, branchCount> branchProcessed {};

    if (derived.morph < 1.0)
    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
    {
        const auto delayedBase = dmBase[branchIndex][static_cast<size_t>(readPos)];
        const auto threshold = effectiveThresholds[branchIndex];
        branchProcessed[branchIndex] = threshold > epsilon
            ? threshold * satShape(delayedBase / threshold)
            : satShape(delayedBase);
    }

    const auto clippedL = branchProcessed[branchLeftUp] + branchProcessed[branchLeftDown];
    const auto clippedR = branchProcessed[branchRightUp] + branchProcessed[branchRightDown];
    const auto linkedGain = alignLookaheadGain(detector::mix(cleanGainState[detector::peak], cleanGainState[detector::rms], derived.rmsMix));
    const auto cleanL = (dmBase[branchLeftUp][static_cast<size_t>(readPos)]
        + dmBase[branchLeftDown][static_cast<size_t>(readPos)]) * linkedGain;
    const auto cleanR = (dmBase[branchRightUp][static_cast<size_t>(readPos)]
        + dmBase[branchRightDown][static_cast<size_t>(readPos)]) * linkedGain;
    const auto processedL = derived.morph >= 1.0 ? cleanL : clippedL + (cleanL - clippedL) * derived.morph;
    const auto processedR = derived.morph >= 1.0 ? cleanR : clippedR + (cleanR - clippedR) * derived.morph;

    const auto readPosDry = wrapIndex(bufPosDry - derived.latencySamples, derived.dryBufferSize);
    const auto delayedDryL = dryL[static_cast<size_t>(readPosDry)];
    const auto delayedDryR = dryR[static_cast<size_t>(readPosDry)];
    const auto wetL = limitReduction(delayedDryL, processedL);
    const auto wetR = limitReduction(delayedDryR, processedR);

    return finishSample(wetL, wetR);
}

DspCore::StereoSample DspCore::finishSample(const double wetL, const double wetR)
{
    const auto readPosDry = wrapIndex(bufPosDry - derived.latencySamples, derived.dryBufferSize);
    const auto delayedDryL = dryL[static_cast<size_t>(readPosDry)];
    const auto delayedDryR = dryR[static_cast<size_t>(readPosDry)];
    StereoSample output;
    if (parameters.sidechainListen)
    {
        output.left = sidechainListenL[static_cast<size_t>(readPosDry)] * derived.outputGain;
        output.right = sidechainListenR[static_cast<size_t>(readPosDry)] * derived.outputGain;
    }
    else if (derived.delta)
    {
        output.left = (delayedDryL - wetL) * derived.mix * derived.outputGain;
        output.right = (delayedDryR - wetR) * derived.mix * derived.outputGain;
    }
    else
    {
        output.left = (delayedDryL + (wetL * derived.makeUpGain - delayedDryL) * derived.mix) * derived.outputGain;
        output.right = (delayedDryR + (wetR * derived.makeUpGain - delayedDryR) * derived.mix) * derived.outputGain;
    }

    bufPos = wrapIndex(bufPos + 1, derived.bufferSize);
    bufPosDry = wrapIndex(bufPosDry + 1, derived.dryBufferSize);

    return output;
}

DspCore::StereoSample DspCore::processTransustSample(const double keyL, const double keyR,
                                                    const double adaptiveLevelDb, const double signalPeak)
{
    auto& reference = adaptiveReferenceDb[0];
    if (adaptiveLevelDb >= reference)
    {
        reference = derived.adaptiveAttackCoefficient * reference
            + (1.0 - derived.adaptiveAttackCoefficient) * adaptiveLevelDb;
        adaptiveHoldSamplesRemaining[0] = derived.adaptiveHoldSamples;
    }
    else if (adaptiveHoldSamplesRemaining[0] > 0) --adaptiveHoldSamplesRemaining[0];
    else reference = derived.adaptiveReleaseCoefficient * reference
        + (1.0 - derived.adaptiveReleaseCoefficient) * adaptiveLevelDb;

    const auto capacity = detectorRmsPower[0].size();
    const auto leaving = (detectorRmsPosition + capacity - detectorRmsWindowSamples) % capacity;
    detectorRmsSampleCount = std::min(detectorRmsSampleCount + 1, capacity);
    const auto denominator = static_cast<double>(std::min(detectorRmsSampleCount, detectorRmsWindowSamples));
    double rms = 0.0;
    const std::array<double, 2> key { keyL, keyR };
    for (size_t channel = 0; channel < channelCount; ++channel)
    {
        const auto power = key[channel] * key[channel];
        detectorRmsPowerSum[channel] += power - detectorRmsPower[channel][leaving];
        detectorRmsPower[channel][detectorRmsPosition] = power;
        rms = std::max(rms, std::sqrt(std::max(0.0, detectorRmsPowerSum[channel]) / denominator));
    }
    detectorRmsPosition = (detectorRmsPosition + 1) % capacity;
    const auto peak = std::max(std::abs(keyL), std::abs(keyR));
    const auto thresholdDb = static_cast<float>(levelToDb(getEffectiveThreshold(0)));
    const auto peakAmount = transustCores[detector::peak].processAmount(static_cast<float>(peak),
        thresholdDb + static_cast<float>(derived.peakCrestDb), static_cast<float>(derived.compKneeDb), static_cast<float>(signalPeak));
    const auto rmsAmount = transustCores[detector::rms].processAmount(static_cast<float>(rms),
        thresholdDb, static_cast<float>(derived.compKneeDb), static_cast<float>(rms));
    const auto amount = detector::mix(static_cast<double>(peakAmount), static_cast<double>(rmsAmount), derived.rmsMix);
    const auto gain = amount * derived.transientGain + (1.0 - amount) * derived.sustainGain;
    const auto read = static_cast<size_t>(wrapIndex(bufPosDry - derived.latencySamples, derived.dryBufferSize));
    return finishSample(dryL[read] * gain, dryR[read] * gain);
}

}
