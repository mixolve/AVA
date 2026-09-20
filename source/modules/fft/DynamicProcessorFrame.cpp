#include "Processor.h"
#include "Constants.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace
{
float wrapPhase(const float phase) noexcept
{
    return std::remainder(phase, juce::MathConstants<float>::twoPi);
}

struct CorrelationMetrics
{
    juce::dsp::Complex<float> leftBin;
    juce::dsp::Complex<float> rightBin;
    float leftAmplitude = 0.0f;
    float rightAmplitude = 0.0f;
    float leftPhase = 0.0f;
    float phaseDifference = 0.0f;
    float absoluteDifference = 0.0f;
    float phaseCorrelation = 1.0f;
    float frequencyCorrelation = 1.0f;
    float signedCorrelation = 1.0f;
    float amplitudePower = 0.0f;
    double correlationWeight = 0.0;
};

CorrelationMetrics measureCorrelationMetrics(const juce::dsp::Complex<float>& leftBin,
                                             const juce::dsp::Complex<float>& rightBin,
                                             const float detectorRangeMagnitude) noexcept
{
    CorrelationMetrics metrics;
    metrics.leftBin = leftBin;
    metrics.rightBin = rightBin;
    metrics.leftAmplitude = std::abs(leftBin);
    metrics.rightAmplitude = std::abs(rightBin);
    metrics.leftPhase = std::atan2(leftBin.imag(), leftBin.real());
    metrics.phaseDifference = wrapPhase(std::atan2(rightBin.imag(), rightBin.real()) - metrics.leftPhase);
    metrics.absoluteDifference = std::abs(metrics.phaseDifference);
    metrics.phaseCorrelation = std::cos(metrics.absoluteDifference);
    metrics.amplitudePower = metrics.leftAmplitude * metrics.leftAmplitude
                           + metrics.rightAmplitude * metrics.rightAmplitude;
    metrics.frequencyCorrelation = metrics.amplitudePower > 1.0e-12f
        ? juce::jlimit(0.0f, 1.0f,
                       2.0f * metrics.leftAmplitude * metrics.rightAmplitude / metrics.amplitudePower)
        : 1.0f;
    metrics.signedCorrelation = metrics.amplitudePower > 1.0e-12f
        ? juce::jlimit(-1.0f, 1.0f,
                       2.0f * (leftBin.real() * rightBin.real() + leftBin.imag() * rightBin.imag())
                           / metrics.amplitudePower)
        : 1.0f;
    metrics.correlationWeight = static_cast<double>(juce::jmin(metrics.leftAmplitude, metrics.rightAmplitude)
                                                     * detectorRangeMagnitude);
    return metrics;
}
}

auto FftModuleProcessor::DynamicProcessor::calculatePublishedThresholds(
    const int channelsToUse,
    const ProcessingSettings& settings) const noexcept
    -> std::array<float, maxChannels>
{
    const auto frequencyCorrelationMode = settings.correlationType == CorrelationType::frequency;
    std::array<float, maxChannels> publishedThreshold {
        settings.correlationMode ? thresholdToCorrelation(settings.correlationThreshold, settings.upward, settings.correlationType) : settings.leftThresholdDb,
        settings.correlationMode ? thresholdToCorrelation(settings.correlationThreshold, settings.upward, settings.correlationType) : settings.rightThresholdDb
    };

    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        const auto channelAdaptiveAmount = settings.correlationMode
            ? settings.correlationAdaptiveAmount
            : (channel == 0 ? settings.leftAdaptiveAmount : settings.rightAdaptiveAmount);
        const auto adaptiveAmount = juce::jlimit(0.0f, 1.0f, channelAdaptiveAmount * 0.01f);

        if (settings.correlationMode)
        {
            const auto manualThreshold = thresholdToCorrelation(settings.correlationThreshold, settings.upward, settings.correlationType);
            const auto adaptiveThreshold = juce::jlimit(frequencyCorrelationMode ? 0.0f : -1.0f,
                                                        1.0f,
                                                        correlationAdaptiveReference[0] + settings.adaptiveOffset);
            publishedThreshold[static_cast<size_t>(channel)] = juce::jmap(adaptiveAmount,
                                                                          manualThreshold,
                                                                          adaptiveThreshold);
        }
        else
        {
            const auto manualThresholdDb = channel == 0 ? settings.leftThresholdDb : settings.rightThresholdDb;
            const auto adaptiveThresholdDb = dualMonoAdaptiveReferenceDb[static_cast<size_t>(channel)]
                                           + settings.adaptiveOffset;
            publishedThreshold[static_cast<size_t>(channel)] = juce::jmap(adaptiveAmount,
                                                                          manualThresholdDb,
                                                                          adaptiveThresholdDb);
        }
    }

    return publishedThreshold;
}

void FftModuleProcessor::DynamicProcessor::prepareFrameSpectrum(const int channelsToUse,
                                                                const int fftIndex,
                                                                const int fftSize) noexcept
{
    const auto& window = windowTables[static_cast<size_t>(fftIndex)];
    auto& fft = *ffts[static_cast<size_t>(fftIndex)];

    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        auto& state = channelStates[static_cast<size_t>(channel)];

        for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
        {
            const auto windowedSample = state.analysisFifo[static_cast<size_t>(sampleIndex)]
                                      * window[static_cast<size_t>(sampleIndex)];
            state.frequencyData[static_cast<size_t>(sampleIndex)] = { windowedSample, 0.0f };
        }

        fft.perform(state.frequencyData.data(), state.frequencyData.data(), false);
    }
}

auto FftModuleProcessor::DynamicProcessor::measureDetectorLevelsForBin(
    const int channelsToUse,
    const int bin,
    const int fftSize,
    const float detectorRangeMagnitude,
    std::array<double, maxChannels>& accumulatedDetectorPower) const noexcept
    -> std::array<float, maxChannels>
{
    std::array<float, maxChannels> levelsDb {};
    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        const auto magnitude = std::abs(channelStates[static_cast<size_t>(channel)].frequencyData[static_cast<size_t>(bin)])
                             / static_cast<float>(fftSize) * detectorRangeMagnitude;
        accumulatedDetectorPower[static_cast<size_t>(channel)] += static_cast<double>(magnitude)
                                                                 * static_cast<double>(magnitude);
        levelsDb[static_cast<size_t>(channel)] = juce::Decibels::gainToDecibels(magnitude, -120.0f);
    }
    return levelsDb;
}

void FftModuleProcessor::DynamicProcessor::processDualMonoSpectrum(
    const int channelsToUse,
    const ProcessingSettings& settings,
    const int fftSize,
    const float attackCoefficient,
    const float releaseCoefficient,
    const float makeupGain,
    const std::array<float, maxChannels>& publishedThreshold,
    std::array<double, maxChannels>& accumulatedDetectorPower) noexcept
{
    for (auto bin = 0; bin <= fftSize / 2; ++bin)
    {
        const auto detectorRangeMagnitude = detectorRangeMagnitudes[static_cast<size_t>(bin)];
        const auto binFrequency = juce::jmax(analyserMinFrequency,
                                             (static_cast<float>(bin) * static_cast<float>(sampleRate))
                                                 / static_cast<float>(fftSize));
        const auto octavesAboveMin = std::log2(binFrequency / analyserMinFrequency);

        const auto channelLevelsDb = measureDetectorLevelsForBin(channelsToUse,
                                                                  bin,
                                                                  fftSize,
                                                                  detectorRangeMagnitude,
                                                                  accumulatedDetectorPower);
        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            const auto levelDb = channelLevelsDb[static_cast<size_t>(channel)];
            auto& smoothedReduction = dualMonoSmoothedReductionDb[static_cast<size_t>(channel)][static_cast<size_t>(bin)];
            const auto thresholdSlopeDb = settings.slopeDbPerOct * juce::jmax(0.0f, octavesAboveMin);
            const auto effectiveThresholdDb = publishedThreshold[static_cast<size_t>(channel)] - thresholdSlopeDb;
            const auto desiredReductionDb = -detectorRangeMagnitude * calculateGainChange(levelDb,
                                                                                            effectiveThresholdDb,
                                                                                            settings.ratio,
                                                                                            settings.kneeDb,
                                                                                            settings.upward);
            const auto coefficient = std::abs(desiredReductionDb) > std::abs(smoothedReduction)
                ? attackCoefficient : releaseCoefficient;
            smoothedReduction = coefficient * smoothedReduction + (1.0f - coefficient) * desiredReductionDb;

            const auto gain = settings.dynamicBypassed
                ? 1.0f
                : makeupGain * juce::Decibels::decibelsToGain(-smoothedReduction);
            auto& frequencyData = channelStates[static_cast<size_t>(channel)].frequencyData;
            frequencyData[static_cast<size_t>(bin)] *= gain;

            if (bin > 0 && bin < fftSize / 2)
                frequencyData[static_cast<size_t>(fftSize - bin)] *= gain;
        }
    }
}

float FftModuleProcessor::DynamicProcessor::calculateDesiredCorrelationChange(
    const ProcessingSettings& settings,
    const int bin,
    const float detectorRangeMagnitude,
    const float minimumLevelDb,
    const float octavesAboveMin,
    const float publishedThreshold,
    float& threshold) const noexcept
{
    const auto frequencyCorrelationMode = settings.correlationType == CorrelationType::frequency;
    const auto minimumCorrelation = frequencyCorrelationMode ? 0.0f : -1.0f;
    const auto correlationKnee = juce::jmap(settings.kneeDb,
                                            0.0f,
                                            24.0f,
                                            0.0f,
                                            frequencyCorrelationMode ? 1.0f : 2.0f);
    const auto correlationRatio = settings.ratio >= 100.0f
        ? std::numeric_limits<float>::infinity()
        : settings.ratio;
    threshold = juce::jlimit(minimumCorrelation,
                             1.0f,
                             publishedThreshold
                                 + settings.correlationSlopePerOctave * juce::jmax(0.0f, octavesAboveMin));

    if (minimumLevelDb < settings.floorDb
        || (settings.upward ? threshold >= 1.0f : threshold <= minimumCorrelation))
        return 0.0f;

    return detectorRangeMagnitude * (settings.upward
        ? -calculateGainChange(detectedCorrelation[static_cast<size_t>(bin)],
                               threshold,
                               correlationRatio,
                               correlationKnee,
                               true)
        : calculateReduction(threshold - detectedCorrelation[static_cast<size_t>(bin)],
                             0.0f,
                             correlationRatio,
                             correlationKnee));
}

void FftModuleProcessor::DynamicProcessor::processCorrelationSpectrum(
    const int channelsToUse,
    const ProcessingSettings& settings,
    const int fftSize,
    const float attackCoefficient,
    const float releaseCoefficient,
    const std::array<float, maxChannels>& publishedThreshold,
    std::array<double, maxChannels>& accumulatedDetectorPower,
    double& accumulatedCorrelation,
    double& accumulatedCorrelationWeight) noexcept
{
    const auto frequencyCorrelationMode = settings.correlationType == CorrelationType::frequency;
    const auto signedCorrelationMode = settings.correlationType == CorrelationType::signedCorrelation;

    for (auto bin = 0; bin <= fftSize / 2; ++bin)
    {
        const auto detectorRangeMagnitude = detectorRangeMagnitudes[static_cast<size_t>(bin)];
        const auto channelLevelsDb = measureDetectorLevelsForBin(channelsToUse,
                                                                  bin,
                                                                  fftSize,
                                                                  detectorRangeMagnitude,
                                                                  accumulatedDetectorPower);

        if (channelsToUse < 2)
            continue;

        auto& leftFrequencyData = channelStates[0].frequencyData;
        auto& rightFrequencyData = channelStates[1].frequencyData;
        const auto metrics = measureCorrelationMetrics(leftFrequencyData[static_cast<size_t>(bin)],
                                                       rightFrequencyData[static_cast<size_t>(bin)],
                                                       detectorRangeMagnitude);
        const auto& leftBin = metrics.leftBin;
        const auto& rightBin = metrics.rightBin;
        const auto leftAmplitude = metrics.leftAmplitude;
        const auto rightAmplitude = metrics.rightAmplitude;
        const auto leftPhase = metrics.leftPhase;
        const auto phaseDifference = metrics.phaseDifference;
        const auto absoluteDifference = metrics.absoluteDifference;
        const auto phaseCorrelation = metrics.phaseCorrelation;
        const auto frequencyCorrelation = metrics.frequencyCorrelation;
        const auto signedCorrelation = metrics.signedCorrelation;
        const auto amplitudePower = metrics.amplitudePower;
        const auto minimumLevelDb = juce::jmin(channelLevelsDb[0], channelLevelsDb[1]);
        const auto currentCorrelation = signedCorrelationMode
            ? signedCorrelation
            : (frequencyCorrelationMode ? frequencyCorrelation : phaseCorrelation);
        accumulatedCorrelation += static_cast<double>(currentCorrelation) * metrics.correlationWeight;
        accumulatedCorrelationWeight += metrics.correlationWeight;

        for (auto channel = 0; channel < maxChannels; ++channel)
            correlationChanges[static_cast<size_t>(channel)][static_cast<size_t>(bin)] = 0.0f;

        if (bin == 0 || bin == fftSize / 2)
            continue;

        const auto binFrequency = juce::jmax(analyserMinFrequency,
                                             (static_cast<float>(bin) * static_cast<float>(sampleRate))
                                                 / static_cast<float>(fftSize));
        const auto octavesAboveMin = std::log2(binFrequency / analyserMinFrequency);
        auto threshold = 0.0f;
        const auto desiredCorrelationChange = calculateDesiredCorrelationChange(settings,
                                                                                 bin,
                                                                                 detectorRangeMagnitude,
                                                                                 minimumLevelDb,
                                                                                 octavesAboveMin,
                                                                                 publishedThreshold[0],
                                                                                 threshold);
        if (frequencyCorrelationMode)
        {
            const auto detectorDistance = settings.upward
                ? detectedCorrelation[static_cast<size_t>(bin)] - threshold
                : threshold - detectedCorrelation[static_cast<size_t>(bin)];
            const auto desiredResponse = juce::jlimit(0.0f, 1.0f,
                std::abs(desiredCorrelationChange) / juce::jmax(1.0e-7f, detectorDistance));
            // Smooth detection selects bins; correction uses each bin's actual distance to the target.
            const auto targetDistance = settings.upward
                ? juce::jmax(0.0f, frequencyCorrelation - threshold)
                : juce::jmax(0.0f, threshold - frequencyCorrelation);
            const auto target = juce::jlimit(0.0f, 1.0f, frequencyCorrelation
                + (settings.upward ? -1.0f : 1.0f) * targetDistance * desiredResponse);
            const auto targetAngle = 0.5f * std::asin(target);
            const auto leftDominant = leftAmplitude >= rightAmplitude;
            const auto currentAngle = std::atan2(rightAmplitude, leftAmplitude);
            const auto desiredAngle = leftDominant ? targetAngle
                : juce::MathConstants<float>::halfPi - targetAngle;
            const auto desiredRotation = currentAngle - desiredAngle;
            auto& smoothedRotation = frequencySmoothedBalanceRotation[static_cast<size_t>(bin)];
            const auto coefficient = std::abs(desiredRotation) > std::abs(smoothedRotation)
                ? attackCoefficient : releaseCoefficient;
            smoothedRotation = coefficient * smoothedRotation + (1.0f - coefficient) * desiredRotation;
            const auto appliedRotation = settings.dynamicBypassed ? 0.0f : smoothedRotation;

            if (std::abs(appliedRotation) <= 1.0e-7f || amplitudePower <= 1.0e-12f)
                continue;

            // Envelope the actual magnitude rotation so release still acts when the target change falls to zero.
            const auto balanceAngle = juce::MathConstants<float>::pi * 0.25f;
            const auto minimumAngle = settings.upward
                ? (leftDominant ? 0.0f : currentAngle)
                : (leftDominant ? currentAngle : balanceAngle);
            const auto maximumAngle = settings.upward
                ? (leftDominant ? currentAngle : juce::MathConstants<float>::halfPi)
                : (leftDominant ? balanceAngle : currentAngle);
            const auto angle = juce::jlimit(minimumAngle, maximumAngle, currentAngle - appliedRotation);
            const auto radius = std::sqrt(amplitudePower);
            auto processedLeftAmplitude = radius * juce::jmax(0.0f, std::cos(angle));
            auto processedRightAmplitude = radius * juce::jmax(0.0f, std::sin(angle));
            const auto rightShare = juce::jmap(settings.correlationImpact, -100.0f, 100.0f, 0.0f, 1.0f);
            const auto leftShare = 1.0f - rightShare;

            // BOTH keeps bin energy constant; one-sided IMPACT leaves the other channel untouched.
            const auto anchor = leftShare * processedRightAmplitude / juce::jmax(1.0e-12f, rightAmplitude)
                              + rightShare * processedLeftAmplitude / juce::jmax(1.0e-12f, leftAmplitude);
            const auto scale = rightShare <= 0.0f && processedRightAmplitude > 0.0f
                ? rightAmplitude / processedRightAmplitude
                : (rightShare >= 1.0f && processedLeftAmplitude > 0.0f
                    ? leftAmplitude / processedLeftAmplitude
                    : 1.0f / juce::jmax(1.0e-4f, anchor));
            const auto energyPreservingMix = 4.0f * leftShare * rightShare;
            const auto amplitudeScale = juce::jmap(energyPreservingMix, scale, 1.0f);
            processedLeftAmplitude *= amplitudeScale;
            processedRightAmplitude *= amplitudeScale;
            // A previously empty bin has no phase; use the surviving channel's phase when filling it.
            const auto processedLeft = leftAmplitude > 1.0e-12f
                ? leftBin * (processedLeftAmplitude / leftAmplitude)
                : std::polar(processedLeftAmplitude, std::atan2(rightBin.imag(), rightBin.real()));
            const auto processedRight = rightAmplitude > 1.0e-12f
                ? rightBin * (processedRightAmplitude / rightAmplitude)
                : std::polar(processedRightAmplitude, leftPhase);
            const auto processedPower = processedLeftAmplitude * processedLeftAmplitude
                                      + processedRightAmplitude * processedRightAmplitude;
            const auto corrected = processedPower > 1.0e-12f
                ? 2.0f * processedLeftAmplitude * processedRightAmplitude / processedPower
                : 1.0f;
            const auto change = juce::jlimit(-1.0f, 1.0f, corrected - frequencyCorrelation);
            correlationChanges[0][static_cast<size_t>(bin)] = change;
            correlationChanges[1][static_cast<size_t>(bin)] = change;
            leftFrequencyData[static_cast<size_t>(bin)] = processedLeft;
            rightFrequencyData[static_cast<size_t>(bin)] = processedRight;
            leftFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedLeft);
            rightFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedRight);
            continue;
        }

        if (signedCorrelationMode)
        {
            const auto detectorDistance = settings.upward
                ? detectedCorrelation[static_cast<size_t>(bin)] - threshold
                : threshold - detectedCorrelation[static_cast<size_t>(bin)];
            const auto desiredResponse = juce::jlimit(0.0f, 1.0f,
                std::abs(desiredCorrelationChange) / juce::jmax(1.0e-7f, detectorDistance));
            const auto targetDistance = settings.upward
                ? juce::jmax(0.0f, signedCorrelation - threshold)
                : juce::jmax(0.0f, threshold - signedCorrelation);
            const auto target = juce::jlimit(-1.0f, 1.0f, signedCorrelation
                + (settings.upward ? -1.0f : 1.0f) * targetDistance * desiredResponse);

            constexpr auto invSqrt2 = 0.7071067811865475244f;
            const auto mid = (leftBin + rightBin) * invSqrt2;
            const auto side = (leftBin - rightBin) * invSqrt2;
            const auto midAmplitude = std::abs(mid);
            const auto sideAmplitude = std::abs(side);
            const auto midSidePower = midAmplitude * midAmplitude + sideAmplitude * sideAmplitude;

            if (midSidePower <= 1.0e-12f)
                continue;

            // SIGNED correlation is exactly the Mid/Side energy balance:
            //   C = (|M|^2 - |S|^2) / (|M|^2 + |S|^2)
            // Rotating the M/S magnitude vector therefore reaches any target in [-1, +1]
            // while preserving total bin energy at IMPACT=BOTH.
            const auto currentAngle = std::atan2(sideAmplitude, midAmplitude);
            const auto targetAngle = 0.5f * std::acos(target);
            const auto desiredRotation = currentAngle - targetAngle;
            auto& smoothedRotation = signedSmoothedMidSideRotation[static_cast<size_t>(bin)];
            const auto coefficient = std::abs(desiredRotation) > std::abs(smoothedRotation)
                ? attackCoefficient : releaseCoefficient;
            smoothedRotation = coefficient * smoothedRotation + (1.0f - coefficient) * desiredRotation;
            const auto appliedRotation = settings.dynamicBypassed ? 0.0f : smoothedRotation;

            if (std::abs(appliedRotation) <= 1.0e-7f)
                continue;

            const auto angle = juce::jlimit(0.0f,
                                            juce::MathConstants<float>::halfPi,
                                            currentAngle - appliedRotation);
            const auto radius = std::sqrt(midSidePower);
            const auto processedMidAmplitude = radius * juce::jmax(0.0f, std::cos(angle));
            const auto processedSideAmplitude = radius * juce::jmax(0.0f, std::sin(angle));
            const auto midPhase = std::atan2(mid.imag(), mid.real());
            const auto sidePhase = std::atan2(side.imag(), side.real());
            const auto fallbackMidPhase = sideAmplitude > 1.0e-12f ? sidePhase : leftPhase;
            const auto fallbackSidePhase = midAmplitude > 1.0e-12f ? midPhase : leftPhase;
            const auto processedMid = midAmplitude > 1.0e-12f
                ? mid * (processedMidAmplitude / midAmplitude)
                : std::polar(processedMidAmplitude, fallbackMidPhase);
            const auto processedSide = sideAmplitude > 1.0e-12f
                ? side * (processedSideAmplitude / sideAmplitude)
                : std::polar(processedSideAmplitude, fallbackSidePhase);

            auto processedLeft = (processedMid + processedSide) * invSqrt2;
            auto processedRight = (processedMid - processedSide) * invSqrt2;

            // IMPACT anchors the requested channel by applying one common complex gain to
            // both outputs. A common gain cannot change SIGNED correlation, so LEFT/RIGHT
            // anchoring remains exact while BOTH keeps the energy-preserving M/S result.
            const auto impact = juce::jlimit(-1.0f, 1.0f, settings.correlationImpact * 0.01f);
            juce::dsp::Complex<float> anchorGain { 1.0f, 0.0f };
            auto anchorAmount = 0.0f;
            if (impact < -1.0e-6f && std::abs(processedLeft) > 1.0e-12f)
            {
                anchorGain = leftBin / processedLeft;
                anchorAmount = -impact;
            }
            else if (impact > 1.0e-6f && std::abs(processedRight) > 1.0e-12f)
            {
                anchorGain = rightBin / processedRight;
                anchorAmount = impact;
            }

            if (anchorAmount > 0.0f)
            {
                const auto anchorMagnitude = juce::jmax(1.0e-12f, std::abs(anchorGain));
                const auto commonMagnitude = std::exp(anchorAmount * std::log(anchorMagnitude));
                const auto commonPhase = anchorAmount * std::atan2(anchorGain.imag(), anchorGain.real());
                const auto commonGain = std::polar(commonMagnitude, commonPhase);
                processedLeft *= commonGain;
                processedRight *= commonGain;
            }

            const auto processedPower = std::norm(processedLeft) + std::norm(processedRight);
            const auto corrected = processedPower > 1.0e-12f
                ? juce::jlimit(-1.0f, 1.0f,
                    2.0f * (processedLeft.real() * processedRight.real()
                          + processedLeft.imag() * processedRight.imag()) / processedPower)
                : 1.0f;
            const auto change = juce::jlimit(settings.upward ? -2.0f : 0.0f,
                                             settings.upward ? 0.0f : 2.0f,
                                             corrected - signedCorrelation);
            correlationChanges[0][static_cast<size_t>(bin)] = change;
            correlationChanges[1][static_cast<size_t>(bin)] = change;
            leftFrequencyData[static_cast<size_t>(bin)] = processedLeft;
            rightFrequencyData[static_cast<size_t>(bin)] = processedRight;
            leftFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedLeft);
            rightFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedRight);
            continue;
        }
        const auto targetCorrelation = juce::jlimit(-1.0f,
                                                    1.0f,
                                                    phaseCorrelation + desiredCorrelationChange);

        // PHASE must not aggressively lock bins whose L/R magnitudes are dissimilar.
        // Their relative phase is poorly conditioned, and forcing it creates magnitude changes
        // after overlap-add even though the individual FFT-bin magnitudes are untouched here.
        const auto phaseConfidence = frequencyCorrelation * frequencyCorrelation;
        const auto desiredPhaseReduction = (absoluteDifference - std::acos(targetCorrelation))
                                         * phaseConfidence;
        const auto direction = phaseDifference >= 0.0f ? 1.0f : -1.0f;
        const auto desiredSignedCorrection = direction * desiredPhaseReduction;
        auto& smoothedCorrection = phaseSmoothedReductionRadians[0][static_cast<size_t>(bin)];

        // Smooth the correction on the phase circle.  The state itself stays bounded to
        // [-pi, pi], so a stable target cannot wind by whole turns over time.
        const auto desiredWrappedCorrection = wrapPhase(desiredSignedCorrection);
        const auto currentWrappedCorrection = wrapPhase(smoothedCorrection);
        const auto correctionDelta = wrapPhase(desiredWrappedCorrection - currentWrappedCorrection);
        const auto coefficient = std::abs(desiredPhaseReduction) > std::abs(currentWrappedCorrection)
            ? attackCoefficient : releaseCoefficient;
        smoothedCorrection = wrapPhase(currentWrappedCorrection
                                     + ((1.0f - coefficient) * correctionDelta));
        phaseSmoothedReductionRadians[1][static_cast<size_t>(bin)] = smoothedCorrection;

        const auto appliedCorrection = settings.dynamicBypassed ? 0.0f : smoothedCorrection;
        const auto rightShare = juce::jmap(settings.correlationImpact, -100.0f, 100.0f, 0.0f, 1.0f);
        const auto leftShare = 1.0f - rightShare;
        const auto rightPhase = std::atan2(rightBin.imag(), rightBin.real());

        const auto processedLeftPhase = leftPhase + appliedCorrection * leftShare;
        const auto processedRightPhase = rightPhase - appliedCorrection * rightShare;
        const auto binIndex = static_cast<size_t>(bin);

        const auto processedPhaseDifference = wrapPhase(processedRightPhase - processedLeftPhase);
        const auto processedCorrelation = std::cos(processedPhaseDifference);
        const auto processedCorrelationReduction = juce::jlimit(settings.upward ? -2.0f : 0.0f,
                                                                settings.upward ? 0.0f : 2.0f,
                                                                processedCorrelation - phaseCorrelation);
        correlationChanges[0][binIndex] = processedCorrelationReduction;
        correlationChanges[1][binIndex] = processedCorrelationReduction;

        const juce::dsp::Complex<float> processedLeft {
            leftAmplitude * std::cos(processedLeftPhase),
            leftAmplitude * std::sin(processedLeftPhase)
        };
        const juce::dsp::Complex<float> processedRight {
            rightAmplitude * std::cos(processedRightPhase),
            rightAmplitude * std::sin(processedRightPhase)
        };

        leftFrequencyData[static_cast<size_t>(bin)] = processedLeft;
        rightFrequencyData[static_cast<size_t>(bin)] = processedRight;
        leftFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedLeft);
        rightFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedRight);
    }
}

void FftModuleProcessor::DynamicProcessor::updateAdaptiveReferences(
    const int channelsToUse,
    const ProcessingSettings& settings,
    const int fftSize,
    const int hopSize,
    const std::array<double, maxChannels>& accumulatedDetectorPower,
    const double accumulatedCorrelation,
    const double accumulatedCorrelationWeight) noexcept
{
    const auto frequencyCorrelationMode = settings.correlationType == CorrelationType::frequency;
    const auto frameDurationSeconds = static_cast<float>(hopSize) / static_cast<float>(sampleRate);
    const auto frameDurationMs = 1000.0f * frameDurationSeconds;
    const auto adaptiveAttackCoefficient = calculateTimeCoefficient(settings.adaptiveAttackMs, frameDurationSeconds);
    const auto adaptiveReleaseCoefficient = calculateTimeCoefficient(settings.adaptiveReleaseMs, frameDurationSeconds);
    const auto processedBinCount = juce::jmax(1, (fftSize / 2) + 1);
    const auto updateAdaptiveReference = [adaptiveAttackCoefficient,
                                          adaptiveReleaseCoefficient,
                                          frameDurationMs,
                                          holdMs = settings.adaptiveHoldMs] (float& current,
                                                                            const float desired,
                                                                            float& holdRemainingMs)
    {
        if (desired >= current)
        {
            current = adaptiveAttackCoefficient * current + (1.0f - adaptiveAttackCoefficient) * desired;
            holdRemainingMs = holdMs;
            return;
        }

        if (holdRemainingMs > 0.0f)
        {
            holdRemainingMs = juce::jmax(0.0f, holdRemainingMs - frameDurationMs);
            return;
        }

        current = adaptiveReleaseCoefficient * current + (1.0f - adaptiveReleaseCoefficient) * desired;
    };

    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        const auto detectorRms = std::sqrt(accumulatedDetectorPower[static_cast<size_t>(channel)]
                                           / static_cast<double>(processedBinCount));
        const auto desiredReferenceDb = juce::Decibels::gainToDecibels(static_cast<float>(detectorRms),
                                                                        analyserMinDecibels);
        updateAdaptiveReference(dualMonoAdaptiveReferenceDb[static_cast<size_t>(channel)],
                                desiredReferenceDb,
                                dualMonoAdaptiveHoldRemainingMs[static_cast<size_t>(channel)]);

        if (settings.correlationMode && channel == 0 && accumulatedCorrelationWeight > 1.0e-12)
        {
            const auto desiredCorrelationReference = juce::jlimit(
                frequencyCorrelationMode ? 0.0f : -1.0f,
                1.0f,
                static_cast<float>(accumulatedCorrelation / accumulatedCorrelationWeight));
            updateAdaptiveReference(correlationAdaptiveReference[0],
                                    desiredCorrelationReference,
                                    correlationAdaptiveHoldRemainingMs[0]);
            correlationAdaptiveReference[1] = correlationAdaptiveReference[0];
            correlationAdaptiveHoldRemainingMs[1] = correlationAdaptiveHoldRemainingMs[0];
        }
    }
}

void FftModuleProcessor::DynamicProcessor::processFrame(const int channelsToUse,
                                                        const ProcessingSettings& settings,
                                                        const int fftIndex,
                                                        const int fftSize,
                                                        const int hopSize) noexcept
{
    const auto frequencyCorrelationMode = settings.correlationType == CorrelationType::frequency;

    updateDetectorRange(settings, fftSize);
    if (settings.correlationMode != lastWasCorrelationMode
        || settings.correlationType != lastCorrelationType)
    {
        for (auto& values : phaseSmoothedReductionRadians)
            values.fill(0.0f);
        frequencySmoothedBalanceRotation.fill(0.0f);
        signedSmoothedMidSideRotation.fill(0.0f);
        correlationAdaptiveReference.fill(frequencyCorrelationMode ? 0.0f : -1.0f);
        correlationAdaptiveHoldRemainingMs.fill(0.0f);
        lastWasCorrelationMode = settings.correlationMode;
        lastCorrelationType = settings.correlationType;
    }

    const auto frameDurationSeconds = static_cast<float>(hopSize) / static_cast<float>(sampleRate);
    const auto attackCoefficient = calculateTimeCoefficient(settings.attackMs, frameDurationSeconds);
    const auto releaseCoefficient = calculateTimeCoefficient(settings.releaseMs, frameDurationSeconds);
    const auto makeupGain = juce::Decibels::decibelsToGain(settings.makeupDb);
    const auto publishedThreshold = calculatePublishedThresholds(channelsToUse, settings);
    std::array<double, maxChannels> accumulatedDetectorPower {};
    auto accumulatedCorrelation = 0.0;
    auto accumulatedCorrelationWeight = 0.0;

    prepareFrameSpectrum(channelsToUse, fftIndex, fftSize);

    if (settings.correlationMode && channelsToUse >= 2)
        updateCorrelationDetector(settings, fftSize);

    if (settings.correlationMode)
    {
        processCorrelationSpectrum(channelsToUse,
                                   settings,
                                   fftSize,
                                   attackCoefficient,
                                   releaseCoefficient,
                                   publishedThreshold,
                                   accumulatedDetectorPower,
                                   accumulatedCorrelation,
                                   accumulatedCorrelationWeight);
    }
    else
    {
        processDualMonoSpectrum(channelsToUse,
                                settings,
                                fftSize,
                                attackCoefficient,
                                releaseCoefficient,
                                makeupGain,
                                publishedThreshold,
                                accumulatedDetectorPower);
    }

    updateAdaptiveReferences(channelsToUse,
                             settings,
                             fftSize,
                             hopSize,
                             accumulatedDetectorPower,
                             accumulatedCorrelation,
                             accumulatedCorrelationWeight);
    applyFrequencyConsistencyCompensation(channelsToUse, settings, fftIndex, fftSize);
    synthesiseFrame(channelsToUse, fftIndex, fftSize);
    publishReductionScope(settings, fftSize, hopSize);
}

void FftModuleProcessor::DynamicProcessor::applyFrequencyConsistencyCompensation(
    const int channelsToUse,
    const ProcessingSettings& settings,
    const int fftIndex,
    const int fftSize) noexcept
{
    const auto frequencyCorrelationMode = settings.correlationType == CorrelationType::frequency;
    const auto& window = windowTables[static_cast<size_t>(fftIndex)];
    auto& fft = *ffts[static_cast<size_t>(fftIndex)];
    // CORR->FREQ consistency pre-compensation.
    //
    // Equal L/R magnitudes inside one STFT frame do not remain exactly equal after
    // synthesis-window + overlap-add when the L/R phases differ.  Predict the local
    // round trip through the synthesis and analysis windows, measure its balance error,
    // and pre-distort only the magnitudes of bins that CORR->FREQ is already processing.
    // The output-bin phases are left untouched.  This is intentionally a single local
    // projection: it adds FFT work, but no second streaming stage and therefore no extra
    // latency.
    if (settings.correlationMode
        && frequencyCorrelationMode
        && ! settings.dynamicBypassed
        && channelsToUse >= 2)
    {
        for (auto channel = 0; channel < maxChannels; ++channel)
        {
            auto& scratch = frequencyConsistencyScratch[static_cast<size_t>(channel)];
            const auto& source = channelStates[static_cast<size_t>(channel)].frequencyData;
            std::copy_n(source.begin(), fftSize, scratch.begin());
            fft.perform(scratch.data(), scratch.data(), true);

            for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
            {
                const auto weight = window[static_cast<size_t>(sampleIndex)];
                const auto roundTripWeight = weight * weight;
                scratch[static_cast<size_t>(sampleIndex)] = {
                    scratch[static_cast<size_t>(sampleIndex)].real() * roundTripWeight,
                    0.0f
                };
            }

            fft.perform(scratch.data(), scratch.data(), false);
        }

        constexpr auto consistencyCompensation = 3.0f;
        constexpr auto minimumBalanceAngle = 0.01f;
        constexpr auto maximumBalanceAngle = juce::MathConstants<float>::halfPi - minimumBalanceAngle;
        const auto rightShare = juce::jmap(settings.correlationImpact, -100.0f, 100.0f, 0.0f, 1.0f);
        const auto leftShare = 1.0f - rightShare;

        auto& leftFrequencyData = channelStates[0].frequencyData;
        auto& rightFrequencyData = channelStates[1].frequencyData;
        const auto& predictedLeft = frequencyConsistencyScratch[0];
        const auto& predictedRight = frequencyConsistencyScratch[1];

        for (auto bin = 1; bin < fftSize / 2; ++bin)
        {
            const auto binIndex = static_cast<size_t>(bin);

            // A zero reduction means this bin was not selected by CORR->FREQ.  Do not
            // let the consistency pass turn into processing outside the detector target.
            if (std::abs(correlationChanges[0][binIndex]) <= 1.0e-7f)
                continue;

            const auto leftBin = leftFrequencyData[binIndex];
            const auto rightBin = rightFrequencyData[binIndex];
            const auto leftAmplitude = std::abs(leftBin);
            const auto rightAmplitude = std::abs(rightBin);
            const auto power = leftAmplitude * leftAmplitude + rightAmplitude * rightAmplitude;

            if (power <= 1.0e-12f)
                continue;

            const auto predictedLeftAmplitude = std::abs(predictedLeft[binIndex]);
            const auto predictedRightAmplitude = std::abs(predictedRight[binIndex]);
            const auto predictedPower = predictedLeftAmplitude * predictedLeftAmplitude
                                      + predictedRightAmplitude * predictedRightAmplitude;

            if (predictedPower <= 1.0e-12f)
                continue;

            const auto targetAngle = std::atan2(rightAmplitude, leftAmplitude);
            const auto predictedAngle = std::atan2(predictedRightAmplitude, predictedLeftAmplitude);
            const auto balanceError = targetAngle - predictedAngle;
            const auto precompensatedAngle = juce::jlimit(minimumBalanceAngle,
                                                          maximumBalanceAngle,
                                                          targetAngle
                                                              + consistencyCompensation * balanceError);
            const auto radius = std::sqrt(power);
            auto compensatedLeftAmplitude = radius * std::cos(precompensatedAngle);
            auto compensatedRightAmplitude = radius * std::sin(precompensatedAngle);

            // Match the main CORR->FREQ pass so consistency compensation does not change IMPACT semantics.
            const auto anchor = leftShare * compensatedRightAmplitude / juce::jmax(1.0e-12f, rightAmplitude)
                              + rightShare * compensatedLeftAmplitude / juce::jmax(1.0e-12f, leftAmplitude);
            const auto scale = rightShare <= 0.0f && compensatedRightAmplitude > 0.0f
                ? rightAmplitude / compensatedRightAmplitude
                : (rightShare >= 1.0f && compensatedLeftAmplitude > 0.0f
                    ? leftAmplitude / compensatedLeftAmplitude
                    : 1.0f / juce::jmax(1.0e-4f, anchor));
            const auto energyPreservingMix = 4.0f * leftShare * rightShare;
            const auto amplitudeScale = juce::jmap(energyPreservingMix, scale, 1.0f);
            compensatedLeftAmplitude *= amplitudeScale;
            compensatedRightAmplitude *= amplitudeScale;

            const auto compensatedLeft = leftAmplitude > 1.0e-12f
                ? leftBin * (compensatedLeftAmplitude / leftAmplitude)
                : std::polar(compensatedLeftAmplitude, std::arg(rightBin));
            const auto compensatedRight = rightAmplitude > 1.0e-12f
                ? rightBin * (compensatedRightAmplitude / rightAmplitude)
                : std::polar(compensatedRightAmplitude, std::arg(leftBin));

            leftFrequencyData[binIndex] = compensatedLeft;
            rightFrequencyData[binIndex] = compensatedRight;
            leftFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(compensatedLeft);
            rightFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(compensatedRight);
        }
    }
}

void FftModuleProcessor::DynamicProcessor::synthesiseFrame(const int channelsToUse,
                                                           const int fftIndex,
                                                           const int fftSize) noexcept
{
    const auto& window = windowTables[static_cast<size_t>(fftIndex)];
    auto& fft = *ffts[static_cast<size_t>(fftIndex)];
    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        auto& state = channelStates[static_cast<size_t>(channel)];
        fft.perform(state.frequencyData.data(), state.frequencyData.data(), true);

        for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
        {
            const auto synthesisWeight = window[static_cast<size_t>(sampleIndex)];
            const auto weightedSample = state.frequencyData[static_cast<size_t>(sampleIndex)].real() * synthesisWeight;
            state.outputAccum[static_cast<size_t>(sampleIndex)] += weightedSample;
            state.normalizationAccum[static_cast<size_t>(sampleIndex)] += synthesisWeight * synthesisWeight;
        }
    }
}

void FftModuleProcessor::DynamicProcessor::publishReductionScope(const ProcessingSettings& settings,
                                                                 const int fftSize,
                                                                 const int hopSize) noexcept
{
    const auto currentSampleRate = juce::jmax(1.0, sampleRate);
    const auto displayCoefficient = calculateTimeCoefficient(
        settings.reductionDisplayTimeMs,
        static_cast<float>(hopSize) / static_cast<float>(currentSampleRate));
    const auto sourceMaximumHz = juce::jlimit(analyserMinFrequency + 1.0f,
                                              analyserMaxFrequency,
                                              static_cast<float>(currentSampleRate * 0.5));
    const auto publishedIndex = activeReductionScopeBuffer.load(std::memory_order_relaxed);
    const auto writeIndex = 1 - publishedIndex;
    auto& reductionScope = reductionScopeBuffers[static_cast<size_t>(writeIndex)];

    for (std::size_t i = 0; i < analyserScopeSize; ++i)
    {
        const auto proportion = static_cast<float>(i) / static_cast<float>(analyserScopeSize - 1);
        const auto frequency = juce::mapToLog10(proportion, analyserMinFrequency, sourceMaximumHz);
        const auto fractionalBin = juce::jlimit(0.0f,
                                                static_cast<float>(fftSize / 2),
                                                frequency * static_cast<float>(fftSize)
                                                    / static_cast<float>(currentSampleRate));
        const auto lowerBin = juce::jlimit(0, fftSize / 2, static_cast<int>(std::floor(fractionalBin)));
        const auto upperBin = juce::jlimit(0, fftSize / 2, lowerBin + 1);
        const auto interpolation = fractionalBin - static_cast<float>(lowerBin);

        for (auto channel = 0; channel < maxChannels; ++channel)
        {
            const auto& source = settings.correlationMode
                ? correlationChanges[static_cast<size_t>(channel)]
                : dualMonoSmoothedReductionDb[static_cast<size_t>(channel)];
            const auto rawReduction = settings.dynamicBypassed
                ? 0.0f
                : juce::jmap(interpolation,
                             source[static_cast<size_t>(lowerBin)],
                             source[static_cast<size_t>(upperBin)]);
            auto& smoothedReduction = smoothedReductionScopes[static_cast<size_t>(channel)][i];
            smoothedReduction = (displayCoefficient * smoothedReduction)
                              + ((1.0f - displayCoefficient) * rawReduction);
            reductionScope[static_cast<size_t>(channel)][i] = smoothedReduction;
        }
    }

    activeReductionScopeBuffer.store(writeIndex, std::memory_order_release);
}
