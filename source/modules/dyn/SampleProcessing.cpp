#include "SampleProcessor.h"
#include "BallisticsCurve.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dyn::dsp
{
namespace
{
double levelToDb(const double level)
{
    return 20.0 * std::log10(std::max(level, epsilon));
}

double compressorGain(const double level, const double threshold,
                      const double gainExponent, const double kneeDb)
{
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
                             const double gainExponent, const double kneeDb)
{
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
    if (durationSamples <= 0 && holdSamples <= 0)
    {
        state.active = false;
        return target;
    }

    // A release may already be running because of tiny variations in the
    // detector's held peak. If the real signal then falls, reusing that nearly
    // completed release would jump to the new target in one or two samples.
    // Re-anchor a falling target to the CURRENT envelope, preserving the
    // user's complete RELEASE time. This must not start another HOLD.
    if (state.active && state.holdCompleted
        && state.elapsedSamples >= state.durationSamples)
    {
        if (target >= current)
        {
            if (target > current)
                state.active = false;
            state.target = target;
            return target;
        }
        state.start = current;
        state.target = target;
        state.elapsedSamples = 0;
        state.durationSamples = durationSamples;
        state.holdElapsedSamples = std::max(0, holdSamples);
        state.holdCompleted = true;
    }

    if (target >= current)
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
        state.holdElapsedSamples = 0;
        state.holdCompleted = holdSamples <= 0;
        state.active = true;
    }

    if (followTarget)
    {
        if (state.holdCompleted && (target < state.target || target > state.target) && target < current)
        {
            // Do not change the destination of an almost finished release
            // without resetting its time origin. Rebase from the current
            // output so successive detector updates remain continuous.
            state.start = current;
            state.elapsedSamples = 0;
            state.durationSamples = durationSamples;
        }
        state.target = target;
    }

    if (!state.holdCompleted)
    {
        // Read the user's current HOLD every sample. A stored countdown
        // continues using the OLD Hold after the user edits the parameter;
        // the resulting premature release sounds like a timer jumping back.
        if (state.holdElapsedSamples < std::max(0, holdSamples))
        {
            ++state.holdElapsedSamples;
            return state.start;
        }
        state.holdCompleted = true;
    }

    if (state.durationSamples <= 0)
    {
        // The hold/release cycle has completed, but the detector is still
        // falling. Keep the release phase latched: otherwise the next lower
        // sample starts a fresh HOLD and produces periodic staircase jumps.
        // A genuine rising level resets this phase at the entry above.
        return state.target;
    }

    const auto progress = juce::jlimit(0.0,
                                        1.0,
                                        static_cast<double>(++state.elapsedSamples)
                                            / static_cast<double>(state.durationSamples));
    const auto shapedProgress = dyn::ballistics::shapedProgress(progress, curve);
    auto released = state.start + (state.target - state.start) * shapedProgress;

    if (gainExponent > epsilon && state.target > epsilon)
    {
        // The compressor applies (threshold / envelope)^exponent. Shape the
        // audible amplitude gain first, then invert it back to detector level.
        // A linear detector decay would otherwise produce a curved recovery.
        const auto threshold = gainThreshold > epsilon ? gainThreshold : state.target * dbToAmp(kneeDb * 0.5);
        const auto startGain = std::max(minimumGain, compressorGain(state.start, threshold, gainExponent, kneeDb));
        const auto targetGain = gainThreshold > epsilon
            ? std::max(minimumGain, compressorGain(state.target, threshold, gainExponent, kneeDb)) : 1.0;
        const auto gain = startGain + (targetGain - startGain) * shapedProgress;
        released = compressorLevelForGain(gain, threshold, gainExponent, kneeDb);
    }

    // Remain in the completed release phase while the input keeps falling.
    // Rearming HOLD here would repeat it every (HOLD + RELEASE) interval.
    return std::max(state.target, released);
}

void DspCore::updateCompressorGain(const size_t detectorIndex, const double detectorLevel,
                                   const double threshold, const int releaseSamples,
                                   const double gainExponent, const double feedbackGainScale)
{
    // No carrier-period hold, sinusoid recognition or half-wave gain latch:
    // when ATTACK/HOLD/RELEASE are zero, gain follows the actual detector
    // sample on *every* sample, including the troughs of low-frequency tones.
    auto& envelope = cleanEnvPeak[detectorIndex];
    auto& release = cleanReleaseStates[detectorIndex];
    auto& attack = cleanAttackStates[detectorIndex];
    auto& gain = cleanGainState[detectorIndex];
    const auto releaseFloor = threshold * dbToAmp(-derived.compKneeDb * 0.5);

    if (derived.thresholdHysteresisDb <= 0.0)
        cleanThresholdHysteresis[detectorIndex] = {};
    const auto holdingThreshold = derived.thresholdHysteresisDb > 0.0
        && cleanThresholdHysteresis[detectorIndex].holds(
            levelToDb(detectorLevel * feedbackGainScale) - levelToDb(releaseFloor),
            derived.thresholdHysteresisDb);

    if (!holdingThreshold && attack.active && attackSourceQuiet
        && detectorLevel * feedbackGainScale < releaseFloor)
    {
        // The source ended before attack reached its requested reduction.
        // Release must start from the gain actually audible at that instant,
        // not finish the old attack or recover from an unapplied deeper gain.
        if (gainExponent > epsilon && feedbackGainScale > epsilon)
            envelope = std::max(detectorLevel, compressorLevelForGain(gain, threshold,
                gainExponent, derived.compKneeDb) / feedbackGainScale);
        release = {};
        attack.active = false;
        attack.settled = false;
    }

    if (! holdingThreshold)
    {
        if (detectorLevel >= envelope || (releaseSamples <= 0 && derived.holdSamples <= 0))
        {
            envelope = detectorLevel;
            release.active = false;
        }
        else
        {
            // Use the same recovery target regardless of HOLD. The outer
            // clamp keeps the envelope above the signal still arriving now.
            envelope = std::max(detectorLevel, releaseTowards(
                release, envelope, releaseFloor, releaseSamples,
                derived.releaseCurve, gainExponent, 0,
                derived.compKneeDb,
                derived.rangeCurve > 0.0 ? 0.0 : derived.minimumGain,
                0.0, false));
        }

        // Feedback is already accounted for in the same-sample solver; do not
        // add a second hidden smoothing pass to the resulting gain target.
        cleanGainTarget[detectorIndex] = std::max(
            derived.rangeCurve > 0.0 ? 0.0 : derived.minimumGain,
            compressorGain(envelope * feedbackGainScale, threshold, gainExponent,
                           derived.compKneeDb));
    }
    else
    {
        release.active = false;
    }

    const auto targetGain = cleanGainTarget[detectorIndex];
    // A sampled carrier can make the requested reduction rise briefly even
    // while the programmed ATTACK is still in progress. Do not abort ATTACK
    // and snap to that instantaneous target: it creates the gain step at
    // the Attack/Sustain boundary. A detected end of sustain may still
    // release immediately, and zero Attack remains sample-exact.
    const auto canReleaseDuringAttack = attackSourceQuiet && targetGain > gain;
    constexpr double gainPrecision = 1.0e-6;
    if (gain >= 1.0 - gainPrecision && !attack.active) attack.settled = false;
    if ((targetGain >= gain && (!attack.active || canReleaseDuringAttack))
        || derived.attackSamples <= 0)
    {
        // Recovery is already timed and curved by releaseTowards(), which
        // advances cleanEnvPeak and calculates cleanGainTarget. Applying an
        // additional one-pole filter here only when ATTACK > 0 changed both
        // RELEASE time and shape depending on ATTACK. Never smooth twice.
        gain = targetGain;
        attack.active = false;
        if (canReleaseDuringAttack) attack.settled = false;
    }
    else if (!attack.active && attack.settled && targetGain >= attack.targetGain - gainPrecision)
    {
        // The completed attack already reached this reduction. Carrier ripple
        // returning to that same level is sustain, not a fresh timed attack.
        gain = targetGain;
    }
    else
    {
        if (! attack.active)
        {
            attack.settled = false;
            attack.startGain = gain;
            attack.targetGain = targetGain;
            attack.elapsedSamples = 0;
            attack.durationSamples = derived.attackSamples;
            attack.active = true;
        }
        // A low-frequency carrier makes the sample-by-sample target move
        // throughout Attack. Preserve the strongest reduction requested so
        // far; weaker, momentary requests are handled by the user's Release
        // once Attack finishes rather than causing a one-sample jump.
        attack.targetGain = std::min(attack.targetGain, targetGain);
        if (attack.durationSamples <= 0)
        {
            gain = targetGain;
            attack.active = false;
            attack.settled = true;
        }
        else
        {
            const auto progress = std::min(1.0, static_cast<double>(++attack.elapsedSamples)
                                                   / static_cast<double>(attack.durationSamples));
            gain = attack.startGain + (attack.targetGain - attack.startGain)
                * dyn::ballistics::shapedAttackToSustainProgress(progress, derived.attackCurve);
            if (progress >= 1.0)
            {
                attack.active = false;
                attack.settled = true;
                // Join release from the gain actually reached by attack, not
                // an older detector envelope that was decaying underneath it.
                if (gainExponent > epsilon && feedbackGainScale > epsilon)
                    envelope = std::max(envelope, compressorLevelForGain(gain, threshold,
                        gainExponent, derived.compKneeDb) / feedbackGainScale);
                release = {};
            }
        }
    }
}

double DspCore::delayedSample(const std::vector<double>& history, const int writeIndex) const noexcept
{
    return history[static_cast<size_t>(wrapIndex(writeIndex - derived.lookaheadSamples, maxBuf))];
}

DspCore::TensionFrame DspCore::evaluateTension(const double envelope, const double adaptiveRmsDb)
{
    TensionFrame frame;
    // The same TENSION transfer is used in COMP/CLIP and TRANS/SUST. Evaluate
    // it from the current detector frame; lookahead shapes the gain before
    // the corresponding delayed carrier reaches the output.
    const auto previewEnvelope = tensionPreviewWindow.process(envelope, derived.lookaheadSamples);
    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
    {
        // ADAP-TIME has already smoothed the level: no second detector stage.
        adaptiveReferenceDb[branchIndex] = adaptiveRmsDb;
        frame.thresholds[branchIndex] = getEffectiveThreshold(branchIndex);

        const auto tension = derived.tensions[branchIndex];
        const auto targetBase = tensionTarget(previewEnvelope,
            frame.thresholds[branchIndex], derived.tensionFloor,
            derived.tensionHysteresis, tension);
        const auto shapedGain = previewEnvelope > epsilon ? targetBase / previewEnvelope : 1.0;
        auto baseGainTarget = shapedGain;
        if (derived.thresholdHysteresisDb > 0.0 && std::abs(tension) > epsilon)
            baseGainTarget = tensionThresholdHysteresis[branchIndex].process(shapedGain,
                levelToDb(frame.thresholds[branchIndex]) - levelToDb(previewEnvelope),
                derived.thresholdHysteresisDb);
        else
            tensionThresholdHysteresis[branchIndex] = {};

        frame.gainTargets[branchIndex] = baseGainTarget;
        frame.playbackGains[branchIndex] = lookaheadTension[branchIndex].process(
            baseGainTarget, derived.lookaheadSamples, derived.lookaheadCurve);
    }
    return frame;
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
    const auto rmsCapacity = adaptiveRmsCumulativePowerHistory[0].size();
    const auto previousCount = adaptiveRmsSampleCount;
    const auto leavingPosition = (adaptiveRmsPosition + rmsCapacity - adaptiveRmsWindowSamples) % rmsCapacity;
    const auto sampleCount = std::min(previousCount + 1, adaptiveRmsWindowSamples);
    for (size_t channelIndex = 0; channelIndex < channelCount; ++channelIndex)
    {
        const auto power = rmsInput[channelIndex] * rmsInput[channelIndex];
        const auto previousCumulative = previousCount >= adaptiveRmsWindowSamples
            ? adaptiveRmsCumulativePowerHistory[channelIndex][leavingPosition] : 0.0;
        adaptiveRmsCumulativePower[channelIndex] += power;
        adaptiveRmsCumulativePowerHistory[channelIndex][adaptiveRmsPosition] = adaptiveRmsCumulativePower[channelIndex];
        const auto meanPower = std::max(0.0, adaptiveRmsCumulativePower[channelIndex] - previousCumulative)
            / static_cast<double>(sampleCount);
        adaptiveRmsDb[channelIndex] = levelToDb(std::sqrt(meanPower));
    }
    adaptiveRmsSampleCount = std::min(previousCount + 1, rmsCapacity);
    adaptiveRmsPosition = (adaptiveRmsPosition + 1) % rmsCapacity;
    const auto stereoAdaptiveRmsDb = std::max(adaptiveRmsDb[0], adaptiveRmsDb[1]);
    const auto stereoKeyPeak = std::max(std::abs(keyL), std::abs(keyR));
    const auto keyPositive = std::abs(keyL) >= std::abs(keyR) ? keyL >= 0.0 : keyR >= 0.0;
    const auto baseReleaseSignal = stereoKeyPeak;
    const auto basePeak = baseReleaseSignal;

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
    const std::array<double, detector::count> baseLevels { basePeak * derived.peakScale, baseRms };
    const std::array<double, detector::count> baseDestinations { baseReleaseSignal * derived.peakScale, baseRms };
    auto effectiveReleaseSamples = derived.releaseSamples;
    for (size_t path = 0; path < detector::count; ++path)
    {
        if (derived.autoRelease > 0.0)
        {
            const auto level = baseDestinations[path];
            const auto threshold = getEffectiveThreshold(keyPositive ? branchLeftUp : branchLeftDown);
            const auto compressorActive = derived.morph > 0.0
                && derived.releaseGainExponent > epsilon
                && levelToDb(level) > levelToDb(threshold) - derived.compKneeDb * 0.5;
            const auto tensionActive = std::abs(derived.tensions[0]) > epsilon
                && level > epsilon && level < threshold;
            auto strength = 1.0;
            if (derived.releaseDepth > 0.0)
            {
                const auto compressionDb = compressorActive ? -levelToDb(compressorGain(level, threshold,
                    derived.releaseGainExponent, derived.compKneeDb)) * derived.morph : 0.0;
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
        if (baseLevels[path] >= envBase[path]
            || (effectiveReleaseSamples[path] <= 0 && derived.holdSamples <= 0))
        {
            envBase[path] = baseLevels[path];
            baseReleaseStates[path].active = false;
        }
        else
            // The TENSION detector needs the same peak-follower invariant:
            // the held envelope can decay, but never below the live input.
            // Otherwise HOLD expiration causes a single-sample reset and
            // starts a fresh HOLD cycle on a continuously falling signal.
            envBase[path] = std::max(baseLevels[path], releaseTowards(
                baseReleaseStates[path], envBase[path],
                0.0, effectiveReleaseSamples[path],
                derived.releaseCurve, 0.0, derived.holdSamples, 0.0, 0.0, 0.0,
                false));
    }
    const auto baseEnvelope = detector::mix(envBase[detector::peak], envBase[detector::rms], derived.rmsMix);

    // Track source activity independently of HOLD/RELEASE so a note-off
    // can interrupt an unfinished Attack without following carrier troughs.
    const auto sourceThreshold = getEffectiveThreshold(keyPositive
        ? branchLeftUp : branchLeftDown);
    const auto sourceSignal = std::abs(keyL) >= std::abs(keyR) ? keyL : keyR;
    const auto wasSourceActive = attackSourceGate.isActive();
    const auto sourceActive = attackSourceGate.advance(sourceSignal, sourceThreshold);
    attackSourceQuiet = !sourceActive;
    if (!wasSourceActive && sourceActive)
        for (auto& attack : cleanAttackStates) attack.settled = false;

    dryL[static_cast<size_t>(bufPosDry)] = inL;
    dryR[static_cast<size_t>(bufPosDry)] = inR;
    sidechainListenL[static_cast<size_t>(bufPosDry)] = keyL;
    sidechainListenR[static_cast<size_t>(bufPosDry)] = keyR;

    // Compute TENSION once for either processing mode. Both modes use the
    // same lookahead-aligned gain and the same positive/negative branches.
    const auto tensionFrame = evaluateTension(baseEnvelope, stereoAdaptiveRmsDb);
    if (parameters.transientSustain)
        return processTransientSustainSample(keyL, keyR, stereoAdaptiveRmsDb,
                                    baseReleaseSignal, tensionFrame);

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
    const auto& tensionPlaybackGain = tensionFrame.playbackGains;
    const auto& effectiveThresholds = tensionFrame.thresholds;
    for (size_t branchIndex = 0; branchIndex < branchCount; ++branchIndex)
    {
        branchBaseDetector[branchIndex] = branchDetectorInput[branchIndex]
            * tensionFrame.gainTargets[branchIndex];
        dmBase[branchIndex][static_cast<size_t>(bufPos)] = branchAudioInput[branchIndex];
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
    // FEEDBACK follows its user parameter without a fixed 5-ms ramp.
    const auto feedbackAmount = derived.morph > 0.0 ? derived.feedback : 0.0;
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
        const auto threshold = effectiveThresholds[stereoPositive ? branchLeftUp : branchLeftDown];
        const auto heldPeak = compressorHoldWindows[detector::peak].process(
            stereoPeak * derived.peakScale, derived.holdSamples);
        const auto heldRms = compressorHoldWindows[detector::rms].process(
            stereoRms, derived.holdSamples);
        updateCompressorGain(detector::peak, heldPeak, threshold,
            effectiveReleaseSamples[detector::peak], derived.releaseGainExponent, 1.0);
        updateCompressorGain(detector::rms, heldRms, threshold,
            effectiveReleaseSamples[detector::rms], derived.releaseGainExponent, 1.0);
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
        const auto feedbackRmsSampleCount = std::min(detectorRmsSampleCount + 1, capacity);
        const auto denominator = static_cast<double>(std::min(feedbackRmsSampleCount, detectorRmsWindowSamples));
        double keyRms = 0.0;
        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            const auto key = cleanDetectorInput[channel];
            const auto power = detectorRmsPowerSum[channel] + key * key - detectorRmsPower[channel][leaving];
            keyRms = std::max(keyRms, std::sqrt(std::max(0.0, power) / denominator));
        }
        // This must happen ONCE per audio sample, not inside feedback's
        // iterative gain solver (which can evaluate many candidates).
        const auto heldPeak = compressorHoldWindows[detector::peak].process(
            keyPeak * derived.peakScale, derived.holdSamples);
        const auto heldRms = compressorHoldWindows[detector::rms].process(
            keyRms, derived.holdSamples);
        const auto savedEnv = cleanEnvPeak;
        const auto savedRelease = cleanReleaseStates;
        const auto savedAttack = cleanAttackStates;
        const auto savedGain = cleanGainState;
        const auto savedTarget = cleanGainTarget;
        const auto savedThresholdHysteresis = cleanThresholdHysteresis;
        const auto evaluate = [&] (const double candidate)
        {
            cleanEnvPeak = savedEnv;
            cleanReleaseStates = savedRelease;
            cleanAttackStates = savedAttack;
            cleanGainState = savedGain;
            cleanGainTarget = savedTarget;
            cleanThresholdHysteresis = savedThresholdHysteresis;
            const auto scale = std::pow(std::max(candidate, epsilon), feedbackAmount);
            updateCompressorGain(detector::peak, heldPeak, threshold,
                effectiveReleaseSamples[detector::peak], loopExponent, scale);
            updateCompressorGain(detector::rms, heldRms, threshold,
                effectiveReleaseSamples[detector::rms], loopExponent, scale);
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
        detectorRmsSampleCount = feedbackRmsSampleCount;
        for (size_t channel = 0; channel < channelCount; ++channel)
        {
            const auto key = cleanDetectorInput[channel];
            const auto power = key * key;
            detectorRmsPowerSum[channel] += power - detectorRmsPower[channel][leaving];
            detectorRmsPower[channel][detectorRmsPosition] = power;
        }
        detectorRmsPosition = (detectorRmsPosition + 1) % capacity;
    }

    // MORPH selects the processing transfer, not the input of the clipper.
    // Apply the clipper to the unchanged, lookahead-aligned carrier. Clipping
    // *after* TENSION could not remove a pre-threshold dip because TENSION had
    // already reduced the sample below the clipping knee. The clipped and
    // compressed paths must receive the SAME raw carrier; the common TENSION
    // gain is applied after MORPH. This preserves TENSION on both endpoints
    // without making the CLIPPER transfer depend on LOOKAHEAD smoothing.
    const auto delayedRawL = delayedSample(dmBase[branchLeftUp], bufPos)
        + delayedSample(dmBase[branchLeftDown], bufPos);
    const auto delayedRawR = delayedSample(dmBase[branchRightUp], bufPos)
        + delayedSample(dmBase[branchRightDown], bufPos);
    auto clipGain = 1.0;
    if (derived.morph < 1.0)
    {
        // Use one gain computer for both channels to preserve their balance.
        const auto strongest = std::abs(delayedRawL) >= std::abs(delayedRawR)
            ? delayedRawL : delayedRawR;
        if (std::abs(strongest) > epsilon)
        {
            const auto threshold = effectiveThresholds[strongest >= 0.0 ? branchLeftUp : branchLeftDown];
            const auto clipped = threshold > epsilon
                ? threshold * satShape(strongest / threshold, derived.inputGain / threshold)
                : satShape(strongest, derived.inputGain);
            clipGain = clipped / strongest;
        }
    }
    const auto detectorGain = detector::mix(cleanGainState[detector::peak],
                                           cleanGainState[detector::rms], derived.rmsMix);
    const auto alignedGain = lookaheadGain.process(detectorGain,
        derived.lookaheadSamples, derived.lookaheadCurve);

    const auto tensionL = delayedSample(dmBase[branchLeftUp], bufPos) * tensionPlaybackGain[branchLeftUp]
        + delayedSample(dmBase[branchLeftDown], bufPos) * tensionPlaybackGain[branchLeftDown];
    const auto tensionR = delayedSample(dmBase[branchRightUp], bufPos) * tensionPlaybackGain[branchRightUp]
        + delayedSample(dmBase[branchRightDown], bufPos) * tensionPlaybackGain[branchRightDown];
    const auto compressedL = tensionL * alignedGain;
    const auto compressedR = tensionR * alignedGain;

    // CLIPPER uses the raw carrier when calculating its transfer, BEFORE
    // TENSION. This ensures that a TENSION-induced dip cannot disable clipping.
    // A final peak ceiling is needed only for upward TENSION expansion: it
    // preserves the clipper ceiling without applying the soft knee twice.
    double clippedL = tensionL * clipGain;
    double clippedR = tensionR * clipGain;
    if (derived.morph < 1.0)
    {
        const auto strongest = std::abs(clippedL) >= std::abs(clippedR) ? clippedL : clippedR;
        const auto magnitude = std::abs(strongest);
        if (magnitude > epsilon)
        {
            const auto ceiling = effectiveThresholds[strongest >= 0.0 ? branchLeftUp : branchLeftDown];
            if (ceiling > epsilon && magnitude > ceiling)
            {
                const auto ceilingGain = ceiling / magnitude;
                clippedL *= ceilingGain;
                clippedR *= ceilingGain;
            }
        }
    }
    const auto processedL = derived.morph >= 1.0 ? compressedL
        : clippedL + (compressedL - clippedL) * derived.morph;
    const auto processedR = derived.morph >= 1.0 ? compressedR
        : clippedR + (compressedR - clippedR) * derived.morph;

    const auto delayedDryL = delayedSample(dryL, bufPosDry);
    const auto delayedDryR = delayedSample(dryR, bufPosDry);
    const auto wetL = limitReduction(delayedDryL, processedL);
    const auto wetR = limitReduction(delayedDryR, processedR);

    return finishSample(wetL, wetR);
}

DspCore::StereoSample DspCore::finishSample(const double wetL, const double wetR)
{
    const auto delayedDryL = delayedSample(dryL, bufPosDry);
    const auto delayedDryR = delayedSample(dryR, bufPosDry);
    StereoSample output;
    if (parameters.sidechainListen)
    {
        output.left = delayedSample(sidechainListenL, bufPosDry) * derived.outputGain;
        output.right = delayedSample(sidechainListenR, bufPosDry) * derived.outputGain;
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

    bufPos = wrapIndex(bufPos + 1, maxBuf);
    bufPosDry = wrapIndex(bufPosDry + 1, maxBuf);
    return output;
}

DspCore::StereoSample DspCore::processTransientSustainSample(const double keyL, const double keyR,
                                                    const double adaptiveLevelDb, const double signalPeak,
                                                    const TensionFrame& tension)
{
    auto& reference = adaptiveReferenceDb[0];
    reference = adaptiveLevelDb;

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
    const auto peakAmount = transientSustainCores[detector::peak].processAmount(static_cast<float>(peak),
        thresholdDb + static_cast<float>(derived.peakCrestDb), static_cast<float>(derived.compKneeDb), static_cast<float>(signalPeak));
    const auto rmsAmount = transientSustainCores[detector::rms].processAmount(static_cast<float>(rms),
        thresholdDb, static_cast<float>(derived.compKneeDb), static_cast<float>(rms));
    const auto amount = detector::mix(static_cast<double>(peakAmount), static_cast<double>(rmsAmount), derived.rmsMix);
    // Shape the transient activation ahead of the delayed carrier. Invert the
    // normal gain-reduction envelope so LOOK-CURVE operates on 0..1 activity;
    // transient and sustain gains themselves may exceed unity.
    const auto shapedAmount = 1.0 - lookaheadGain.process(1.0 - amount,
        derived.lookaheadSamples, derived.lookaheadCurve);
    const auto gain = shapedAmount * derived.transientGain + (1.0 - shapedAmount) * derived.sustainGain;
    // TRANS/SUST applies its transient/sustain balance first, then the same
    // polarity-aware TENSION gain as COMP/CLIP. Select the branch from the
    // delayed sample's sign, never the current undelayed key: otherwise
    // LOOKAHEAD would apply the opposite half-wave's gain to this carrier.
    const auto delayedL = delayedSample(dryL, bufPosDry);
    const auto delayedR = delayedSample(dryR, bufPosDry);
    const auto gainL = tension.playbackGains[delayedL >= 0.0 ? branchLeftUp : branchLeftDown];
    const auto gainR = tension.playbackGains[delayedR >= 0.0 ? branchRightUp : branchRightDown];
    return finishSample(delayedL * gain * gainL,
                        delayedR * gain * gainR);
}

}
