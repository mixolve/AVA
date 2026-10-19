#include "SpectralProcessor.h"
#include "SpectralConstants.h"
#include "TensionTransfer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace
{

float limitReductionDb(const float reduction, const float rangeDb, const float rangeCurve) noexcept
{
    if (reduction <= 0.0f)
        return reduction;
    const auto width = rangeDb * rangeCurve;
    if (width <= 0.0001f)
        return juce::jmin(reduction, rangeDb);
    const auto lower = rangeDb - width;
    if (reduction <= lower)
        return reduction;
    if (reduction >= rangeDb + width)
        return rangeDb;
    const auto distance = reduction - lower;
    return reduction - distance * distance / (4.0f * width);
}

struct CorrelationMetrics
{
    juce::dsp::Complex<float> leftBin;
    juce::dsp::Complex<float> rightBin;
    float leftAmplitude = 0.0f;
    float rightAmplitude = 0.0f;
    float leftPhase = 0.0f;
    float signedCorrelation = 1.0f;
    float amplitudePower = 0.0f;
    double correlationWeight = 0.0;
};

CorrelationMetrics measureCorrelationMetrics(const juce::dsp::Complex<float>& leftBin,
                                             const juce::dsp::Complex<float>& rightBin) noexcept
{
    CorrelationMetrics metrics;
    metrics.leftBin = leftBin;
    metrics.rightBin = rightBin;
    metrics.leftAmplitude = std::abs(leftBin);
    metrics.rightAmplitude = std::abs(rightBin);
    metrics.leftPhase = std::atan2(leftBin.imag(), leftBin.real());
    metrics.amplitudePower =
        metrics.leftAmplitude * metrics.leftAmplitude + metrics.rightAmplitude * metrics.rightAmplitude;
    metrics.signedCorrelation =
        metrics.amplitudePower > 1.0e-12f
            ? juce::jlimit(-1.0f, 1.0f,
                           2.0f * (leftBin.real() * rightBin.real() + leftBin.imag() * rightBin.imag()) /
                               metrics.amplitudePower)
            : 1.0f;
    metrics.correlationWeight = static_cast<double>(juce::jmin(metrics.leftAmplitude, metrics.rightAmplitude));
    return metrics;
}
}

float DynSpectralProcessor::DynamicProcessor::calculatePublishedThreshold(
    const ProcessingSettings& settings) const noexcept
{
    const auto amount = juce::jlimit(0.0f, 1.0f, settings.adaptiveAmount * 0.01f);
    if (settings.correlationMode)
    {
        const auto adaptiveThreshold =
            juce::jlimit(-1.0f, 1.0f, correlationAdaptiveReference + settings.adaptiveOffset);
        return juce::jmap(amount, settings.correlationThreshold, adaptiveThreshold);
    }
    return juce::jmap(amount, settings.thresholdDb, linkedAdaptiveReferenceDb + settings.adaptiveOffset);
}

void DynSpectralProcessor::DynamicProcessor::prepareFrameSpectrum(const int channelsToUse, const int fftIndex,
                                                                  const int fftSize) noexcept
{
    const auto& window = windowTables[static_cast<size_t>(fftIndex)];
    auto& fft = *ffts[static_cast<size_t>(fftIndex)];

    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        auto& state = channelStates[static_cast<size_t>(channel)];

        for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
        {
            const auto windowedSample =
                state.analysisFifo[static_cast<size_t>(sampleIndex)] * window[static_cast<size_t>(sampleIndex)];
            state.frequencyData[static_cast<size_t>(sampleIndex)] = {windowedSample, 0.0f};
        }

        fft.perform(state.frequencyData.data(), state.frequencyData.data(), false);
    }
}

void DynSpectralProcessor::DynamicProcessor::prepareSidechainSpectrum(const int channelsToUse, const int fftIndex,
                                                                      const int fftSize) noexcept
{
    const auto& window = windowTables[static_cast<size_t>(fftIndex)];
    auto& fft = *ffts[static_cast<size_t>(fftIndex)];
    for (int channel = 0; channel < channelsToUse; ++channel)
    {
        auto& spectrum = sidechainFrequencyData[static_cast<size_t>(channel)];
        const auto& fifo = sidechainAnalysisFifo[static_cast<size_t>(channel)];
        for (int sample = 0; sample < fftSize; ++sample)
            spectrum[static_cast<size_t>(sample)] = {
                fifo[static_cast<size_t>(sample)] * window[static_cast<size_t>(sample)], 0.0f};
        fft.perform(spectrum.data(), spectrum.data(), false);
    }
}

auto DynSpectralProcessor::DynamicProcessor::measureDetectorLevelsForBin(
    const int channelsToUse, const int bin, const int fftSize,

    std::array<double, maxChannels>& accumulatedDetectorPower, const bool useSidechainInput) const noexcept
    -> std::array<float, maxChannels>
{
    std::array<float, maxChannels> levelsDb{};
    for (auto channel = 0; channel < channelsToUse; ++channel)
    {
        const auto& spectrum = useSidechainInput ? sidechainFrequencyData[static_cast<size_t>(channel)]
                                                 : channelStates[static_cast<size_t>(channel)].frequencyData;
        const auto magnitude = std::abs(spectrum[static_cast<size_t>(bin)]) / static_cast<float>(fftSize);
        accumulatedDetectorPower[static_cast<size_t>(channel)] +=
            static_cast<double>(magnitude) * static_cast<double>(magnitude);
        levelsDb[static_cast<size_t>(channel)] = juce::Decibels::gainToDecibels(magnitude, -120.0f);
    }
    return levelsDb;
}

void DynSpectralProcessor::DynamicProcessor::processLinkedSpectrum(
    const int channelsToUse, const ProcessingSettings& settings, const int fftSize, const float publishedThreshold,
    std::array<double, maxChannels>& accumulatedDetectorPower) noexcept
{
    const auto frameMs = 1000.0f * static_cast<float>(currentHopSize / sampleRate);
    const auto rmsCoefficient = calculateTimeCoefficient(settings.rmsTimeMs, frameMs * 0.001f);
    const auto memoryCoefficient = settings.autoRelease > 0.0f ? std::exp(-frameMs / settings.releaseMemoryMs) : 0.0;
    for (int bin = 0; bin <= fftSize / 2; ++bin)
    {
        const auto frequency = juce::jmax(detectorMinFrequency, static_cast<float>(bin * sampleRate / fftSize));
        const auto slope = settings.slopeDbPerOct * juce::jmax(0.0f, std::log2(frequency / detectorMinFrequency));
        const auto levels = measureDetectorLevelsForBin(channelsToUse, bin, fftSize, accumulatedDetectorPower,
                                                        settings.separateDetector);
        const auto peakLevel = channelsToUse > 1 ? juce::jmax(levels[0], levels[1]) : levels[0];
        const auto peak = juce::Decibels::decibelsToGain(peakLevel);
        auto& meanPower = spectralRmsPower[static_cast<size_t>(bin)];
        meanPower = rmsCoefficient * meanPower + (1.0f - rmsCoefficient) * peak * peak;
        const auto rmsScale = bin == 0 || bin == fftSize / 2 ? 1.0f : 0.5f;
        const auto rmsLevel = juce::Decibels::gainToDecibels(std::sqrt(juce::jmax(0.0f, meanPower) * rmsScale), -120.0f);
        std::array<float, dyn::detector::count> detectorGains {};
        for (size_t path = 0; path < dyn::detector::count; ++path)
        {
            const auto level = path == dyn::detector::peak ? peakLevel : rmsLevel;
            const auto threshold = publishedThreshold - slope + (path == dyn::detector::peak ? dyn::detector::effectiveCrest(settings.peakCrestDb, settings.rmsMix) : 0.0f);
            const auto releaseMs = path == dyn::detector::peak ? settings.releasePeakMs : settings.releaseRmsMs;
            auto tensionGainDb = 0.0f;

            auto targetGainDb = 0.0f;
            if (std::abs(smoothedTension) >= 0.005f && level < threshold)
            {
                const auto floor = juce::jmin(settings.tensionFloorDb, threshold);
                const auto shapedLevel = dyn_spectral_tension::shape(level, threshold, floor, smoothedTension);
                targetGainDb = shapedLevel - level;
                if (level <= floor)
                    targetGainDb += juce::Decibels::gainToDecibels(settings.tensionHysteresis, -240.0f);
            }
            auto& tensionEnvelope = tensionEnvelopes[path][static_cast<size_t>(bin)];
            auto& direction = tensionDirections[path][static_cast<size_t>(bin)];
            auto& tensionLatch = tensionThresholdHysteresis[path][static_cast<size_t>(bin)];
            targetGainDb = tensionLatch.process(targetGainDb,
                threshold - level, std::abs(smoothedTension) >= 0.005f ? settings.thresholdHysteresisDb : 0.0f);
            const auto currentTensionGainDb = tensionEnvelope.value * 240.0f * direction;
            if (tensionLatch.active && level > threshold && std::abs(currentTensionGainDb) > std::abs(targetGainDb))
                targetGainDb = currentTensionGainDb;
            if (std::abs(targetGainDb) > 0.00001f)
            {
                const auto nextDirection = targetGainDb < 0.0f ? -1.0f : 1.0f;
                if (direction * nextDirection < 0.5f)
                    tensionEnvelope = {};
                direction = nextDirection;
            }
            const auto amount = tensionEnvelope.advance(
                juce::jmin(1.0f, std::abs(targetGainDb) / 240.0f), frameMs, settings.delayMs, settings.attackMs,
                settings.holdMs, releaseMs, settings.attackCurve, settings.releaseCurve, settings.releaseType, settings.autoRelease, memoryCoefficient, settings.releaseDepth, std::abs(targetGainDb) / 12.0f);
            tensionGainDb = amount * 240.0f * direction;

            const auto requestedReduction = calculateReduction(level + tensionGainDb, threshold, settings.ratio, settings.kneeDb, settings.progression, settings.progressionSpan);
            auto& envelope = spectralEnvelopes[path][static_cast<size_t>(bin)];
            auto& thresholdLatch = thresholdHysteresis[path][static_cast<size_t>(bin)];
            const auto thresholdDistance = level + tensionGainDb - (threshold - settings.kneeDb * 0.5f);
            auto desired = thresholdLatch.process(requestedReduction, thresholdDistance, settings.thresholdHysteresisDb);
            if (thresholdLatch.active && thresholdDistance < 0.0f)
                desired = juce::jmax(desired, envelope.value);
            envelope.releaseMemory.advance(desired > 0.00001f, memoryCoefficient, settings.autoRelease,
                settings.releaseDepth, limitReductionDb(desired, settings.rangeDb, settings.rangeCurve) / 12.0f);
            const auto increasing = std::abs(desired) > std::abs(envelope.value) + 0.00001f;
            if (increasing)
            {
                envelope.holdMs = settings.holdMs;
                if (!envelope.attacking)
                {
                    envelope.start = envelope.value;
                    envelope.elapsedMs = 0.0f;
                    envelope.delayMs = settings.delayMs;
                }
                envelope.attacking = true;
                envelope.target = desired;
                envelope.delayMs = juce::jmax(0.0f, envelope.delayMs - frameMs);
                if (envelope.delayMs <= 0.0f)
                {
                    envelope.elapsedMs += frameMs;
                    const auto position =
                        settings.attackMs <= 0.0f ? 1.0f : juce::jlimit(0.0f, 1.0f, envelope.elapsedMs / settings.attackMs);
                    const auto shape = std::pow(position, std::exp2(-settings.attackCurve * 0.03f));
                    envelope.value = envelope.start + (envelope.target - envelope.start) * shape;
                }
            }
            else if (std::abs(desired - envelope.value) > 0.00001f)
            {
                envelope.delayMs = 0.0f;
                if (envelope.attacking || std::abs(envelope.target - desired) > 0.00001f)
                {
                    envelope.start = envelope.value;
                    envelope.elapsedMs = 0.0f;
                    envelope.releaseDurationMs = static_cast<float>(releaseMs * envelope.releaseMemory.multiplier(settings.autoRelease));
                }
                envelope.attacking = false;
                envelope.target = desired;
                if (envelope.holdMs > 0.0f)
                    envelope.holdMs = juce::jmax(0.0f, envelope.holdMs - frameMs);
                else
                {
                    envelope.elapsedMs += frameMs;
                    const auto duration = settings.autoRelease > 0.0f ? envelope.releaseDurationMs : releaseMs;
                    const auto position = duration <= 0.0f
                                              ? 1.0f
                                              : juce::jlimit(0.0f, 1.0f, envelope.elapsedMs / duration);
                    const auto shape = std::pow(position, std::exp2(-settings.releaseCurve * 0.03f));
                    const auto destination = envelope.target * settings.releaseType;
                    const auto released = envelope.start + (destination - envelope.start) * shape;
                    envelope.value =
                        std::copysign(juce::jmax(std::abs(envelope.target), std::abs(released)), envelope.start);
                }
            }
            else
            {
                // A sustained signal reloads HOLD, so its timer starts when the detector falls.
                envelope.holdMs = settings.holdMs;
                envelope.attacking = false;
                envelope.delayMs = 0.0f;
            }
            detectorGains[path] = juce::Decibels::decibelsToGain(
                -limitReductionDb(envelope.value - tensionGainDb, settings.rangeDb, settings.rangeCurve));
        }
        const auto gain = dyn::detector::mix(detectorGains[dyn::detector::peak], detectorGains[dyn::detector::rms], settings.rmsMix);

        for (int channel = 0; channel < channelsToUse; ++channel)
        {
            auto& bins = channelStates[static_cast<size_t>(channel)].frequencyData;
            bins[static_cast<size_t>(bin)] *= gain;
            if (bin > 0 && bin < fftSize / 2)
                bins[static_cast<size_t>(fftSize - bin)] *= gain;
        }
    }
}

void DynSpectralProcessor::DynamicProcessor::processCorrelationSpectrum(
    const int channelsToUse, const ProcessingSettings& settings, const int fftSize, const float publishedThreshold,
    std::array<double, maxChannels>& accumulatedDetectorPower, double& accumulatedCorrelation,
    double& accumulatedCorrelationWeight) noexcept
{
    const auto frameMs = 1000.0f * static_cast<float>(currentHopSize / sampleRate);
    const auto memoryCoefficient = settings.autoRelease > 0.0f ? std::exp(-frameMs / settings.releaseMemoryMs) : 0.0;
    for (auto bin = 0; bin <= fftSize / 2; ++bin)
    {
        measureDetectorLevelsForBin(channelsToUse, bin, fftSize,

                                    accumulatedDetectorPower);

        if (channelsToUse < 2)
            continue;

        auto& leftFrequencyData = channelStates[0].frequencyData;
        auto& rightFrequencyData = channelStates[1].frequencyData;
        const auto metrics = measureCorrelationMetrics(leftFrequencyData[static_cast<size_t>(bin)],
                                                       rightFrequencyData[static_cast<size_t>(bin)]);
        const auto& leftBin = metrics.leftBin;
        const auto& rightBin = metrics.rightBin;
        const auto leftPhase = metrics.leftPhase;
        const auto signedCorrelation = metrics.signedCorrelation;
        const auto currentCorrelation = signedCorrelation;
        if (settings.separateDetector)
        {
            const auto key = measureCorrelationMetrics(sidechainFrequencyData[0][static_cast<size_t>(bin)],
                                                       sidechainFrequencyData[1][static_cast<size_t>(bin)]);
            const auto correlation = key.signedCorrelation;
            accumulatedCorrelation += static_cast<double>(correlation) * key.correlationWeight;
            accumulatedCorrelationWeight += key.correlationWeight;
        }
        else
        {
            accumulatedCorrelation += static_cast<double>(currentCorrelation) * metrics.correlationWeight;
            accumulatedCorrelationWeight += metrics.correlationWeight;
        }

        if (bin == 0 || bin == fftSize / 2)
            continue;

        const auto binFrequency =
            juce::jmax(detectorMinFrequency,
                       (static_cast<float>(bin) * static_cast<float>(sampleRate)) / static_cast<float>(fftSize));
        const auto octavesAboveMin = std::log2(binFrequency / detectorMinFrequency);
        const auto commonThreshold = juce::jlimit(-1.0f, 1.0f,
            publishedThreshold + settings.correlationSlopePerOctave * juce::jmax(0.0f, octavesAboveMin));
        constexpr auto invSqrt2 = 0.7071067811865475244f;
        const auto mid = (leftBin + rightBin) * invSqrt2;
        const auto side = (leftBin - rightBin) * invSqrt2;
        const auto midAmplitude = std::abs(mid);
        const auto sideAmplitude = std::abs(side);
        const auto midSidePower = midAmplitude * midAmplitude + sideAmplitude * sideAmplitude;

        // SIGNED correlation is exactly the Mid/Side energy balance:
        //   C = (|M|^2 - |S|^2) / (|M|^2 + |S|^2)
        // Rotating the M/S magnitude vector therefore reaches any target in [-1, +1]
        // while preserving total bin energy at IMPACT=BOTH.
        const auto currentAngle = std::atan2(sideAmplitude, midAmplitude);

        std::array<float, dyn::detector::count> detectorTargets {};
        for (size_t path = 0; path < dyn::detector::count; ++path)
        {
            const auto keyCorrelation = path == dyn::detector::peak ? detectedCorrelation[static_cast<size_t>(bin)]
                                                                   : detectedRmsCorrelation[static_cast<size_t>(bin)];
            const auto threshold = path == dyn::detector::peak
                ? dyn::detector::peakCorrelationThreshold(commonThreshold,
                    dyn::detector::effectiveCrest(settings.correlationPeakCrest, settings.rmsMix)) : commonThreshold;
            const auto detectorDistance = threshold - keyCorrelation;
            const auto desiredCorrelationChange = threshold <= -1.0f ? 0.0f
                : calculateReduction(detectorDistance, 0.0f, settings.ratio, settings.correlationKnee, settings.progression, settings.progressionSpan);
            const auto releaseMs = path == dyn::detector::peak ? settings.releasePeakMs : settings.releaseRmsMs;
            auto& envelope = correlationEnvelopes[path][static_cast<size_t>(bin)];
            const auto openingCorrelation = juce::jlimit(-1.0f, 1.0f, threshold + settings.correlationKnee * 0.5f);
            const auto hysteresisWidth = juce::jmin(settings.correlationThresholdHysteresis, 1.0f - openingCorrelation);
            auto& thresholdLatch = thresholdHysteresis[path][static_cast<size_t>(bin)];
            auto desiredAmount = thresholdLatch.process(
                std::abs(desiredCorrelationChange) * 0.5f, openingCorrelation - keyCorrelation, hysteresisWidth);
            if (thresholdLatch.active && keyCorrelation > openingCorrelation)
                desiredAmount = juce::jmax(desiredAmount, envelope.value);
            auto& referenceDistance = correlationResponseDistances[path][static_cast<size_t>(bin)];
            // Preserve the reference during release, so a falling key cannot
            // cancel its own release by rescaling the correction every frame.
            if (detectorDistance > 1.0e-7f && desiredAmount >= envelope.value - 1.0e-5f)
                referenceDistance = detectorDistance;
            const auto amount = envelope.advance(desiredAmount, 1000.0f * static_cast<float>(currentHopSize / sampleRate),
                                                 settings.delayMs, settings.attackMs, settings.holdMs, releaseMs,
                                                 settings.attackCurve, settings.releaseCurve, settings.releaseType, settings.autoRelease, memoryCoefficient, settings.releaseDepth, std::abs(desiredCorrelationChange));
            const auto response = juce::jlimit(0.0f, 1.0f, 2.0f * amount / juce::jmax(1.0e-7f, referenceDistance));
            const auto shapedKey = dyn_spectral_tension::correlation(
                keyCorrelation, threshold, settings.correlationTensionFloor, smoothedTension, settings.tensionHysteresis);
            auto& tensionEnvelope = tensionEnvelopes[path][static_cast<size_t>(bin)];
            auto& direction = tensionDirections[path][static_cast<size_t>(bin)];
            auto keyChange = tensionThresholdHysteresis[path][static_cast<size_t>(bin)].process(
                shapedKey - keyCorrelation, threshold - keyCorrelation,
                std::abs(smoothedTension) >= 0.005f ? juce::jmin(settings.correlationThresholdHysteresis, 1.0f - threshold) : 0.0f);
            const auto currentTensionChange = tensionEnvelope.value * 2.0f * direction;
            if (tensionThresholdHysteresis[path][static_cast<size_t>(bin)].active && keyCorrelation > threshold
                && std::abs(currentTensionChange) > std::abs(keyChange))
                keyChange = currentTensionChange;
            if (std::abs(keyChange) > 0.00001f)
            {
                const auto nextDirection = keyChange < 0.0f ? -1.0f : 1.0f;
                if (direction * nextDirection < 0.5f)
                    tensionEnvelope = {};
                direction = nextDirection;
            }
            const auto tensionAmount = tensionEnvelope.advance(
                std::abs(keyChange) * 0.5f, 1000.0f * static_cast<float>(currentHopSize / sampleRate),
                settings.delayMs, settings.attackMs, settings.holdMs, releaseMs, settings.attackCurve,
                settings.releaseCurve, settings.releaseType, settings.autoRelease, memoryCoefficient, settings.releaseDepth, std::abs(keyChange));
            const auto baseCorrelation = juce::jlimit(-1.0f, 1.0f, signedCorrelation + 2.0f * tensionAmount * direction);
            auto baseDistance = juce::jmax(0.0f, threshold - baseCorrelation);
            auto& retainedDistance = correlationActionDistances[path][static_cast<size_t>(bin)];
            const auto holdingThreshold = hysteresisWidth > 0.0f
                && thresholdHysteresis[path][static_cast<size_t>(bin)].active && keyCorrelation > openingCorrelation;
            // Keep the captured correction while the detector is latched, then
            // let RELEASE remove it after the return threshold is crossed.
            if ((settings.correlationThresholdHysteresis > 0.0f || settings.autoRelease > 0.0f)
                && (holdingThreshold || desiredAmount < amount - 1.0e-7f))
                baseDistance = retainedDistance;
            else
                retainedDistance = baseDistance;
            const auto candidate = juce::jlimit(-1.0f, 1.0f, baseCorrelation + baseDistance * response);
            detectorTargets[path] = candidate;
        }
        const auto envelopedTarget = dyn::detector::mix(
            detectorTargets[dyn::detector::peak], detectorTargets[dyn::detector::rms], settings.rmsMix);
        const auto appliedRotation = currentAngle - 0.5f * std::acos(envelopedTarget);

        if (std::abs(appliedRotation) <= 1.0e-7f || midSidePower <= 1.0e-12f)
            continue;

        auto angle = juce::jlimit(0.0f, juce::MathConstants<float>::halfPi, currentAngle - appliedRotation);

        // RANGE caps attenuation of the reduced M/S component in dB.
        // The other component still compensates to preserve total bin energy.
        const auto reducingMid = angle > currentAngle;
        const auto original = reducingMid ? std::cos(currentAngle) : std::sin(currentAngle);
        const auto corrected = reducingMid ? std::cos(angle) : std::sin(angle);
        const auto reduction = -juce::Decibels::gainToDecibels(
            juce::jlimit(1.0e-12f, 1.0f, corrected / juce::jmax(1.0e-12f, original)), -240.0f);
        const auto bounded = limitReductionDb(reduction, settings.rangeDb, settings.rangeCurve);
        const auto minimum = original * juce::Decibels::decibelsToGain(-bounded);
        angle = reducingMid ? juce::jmin(angle, std::acos(juce::jlimit(0.0f, 1.0f, minimum)))
                            : juce::jmax(angle, std::asin(juce::jlimit(0.0f, 1.0f, minimum)));

        const auto radius = std::sqrt(midSidePower);
        const auto processedMidAmplitude = radius * juce::jmax(0.0f, std::cos(angle));
        const auto processedSideAmplitude = radius * juce::jmax(0.0f, std::sin(angle));
        const auto midPhase = std::atan2(mid.imag(), mid.real());
        const auto sidePhase = std::atan2(side.imag(), side.real());
        const auto fallbackMidPhase = sideAmplitude > 1.0e-12f ? sidePhase : leftPhase;
        const auto fallbackSidePhase = midAmplitude > 1.0e-12f ? midPhase : leftPhase;
        const auto processedMid = midAmplitude > 1.0e-12f ? mid * (processedMidAmplitude / midAmplitude)
                                                          : std::polar(processedMidAmplitude, fallbackMidPhase);
        const auto processedSide = sideAmplitude > 1.0e-12f ? side * (processedSideAmplitude / sideAmplitude)
                                                            : std::polar(processedSideAmplitude, fallbackSidePhase);

        auto processedLeft = (processedMid + processedSide) * invSqrt2;
        auto processedRight = (processedMid - processedSide) * invSqrt2;

        // IMPACT anchors the requested channel by applying one common complex gain to
        // both outputs. A common gain cannot change SIGNED correlation, so LEFT/RIGHT
        // anchoring remains exact while BOTH keeps the energy-preserving M/S result.
        const auto impact = juce::jlimit(-1.0f, 1.0f, settings.correlationImpact * 0.01f);
        juce::dsp::Complex<float> anchorGain{1.0f, 0.0f};
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

        leftFrequencyData[static_cast<size_t>(bin)] = processedLeft;
        rightFrequencyData[static_cast<size_t>(bin)] = processedRight;
        leftFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedLeft);
        rightFrequencyData[static_cast<size_t>(fftSize - bin)] = std::conj(processedRight);
        continue;
    }
}

void DynSpectralProcessor::DynamicProcessor::updateAdaptiveReferences(
    const int channelsToUse, const ProcessingSettings& settings, const int fftSize, const int hopSize,
    const std::array<double, maxChannels>& accumulatedDetectorPower, const double accumulatedCorrelation,
    const double accumulatedCorrelationWeight) noexcept
{
    const auto frameDurationSeconds = static_cast<float>(hopSize) / static_cast<float>(sampleRate);
    const auto frameDurationMs = 1000.0f * frameDurationSeconds;
    const auto adaptiveAttackCoefficient = calculateTimeCoefficient(settings.adaptiveAttackMs, frameDurationSeconds);
    const auto adaptiveReleaseCoefficient = calculateTimeCoefficient(settings.adaptiveReleaseMs, frameDurationSeconds);
    const auto processedBinCount = juce::jmax(1, (fftSize / 2) + 1);
    const auto updateAdaptiveReference =
        [adaptiveAttackCoefficient, adaptiveReleaseCoefficient, frameDurationMs,
         holdMs = settings.adaptiveHoldMs](float& current, const float desired, float& holdRemainingMs)
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

    auto detectorPower = !settings.correlationMode && channelsToUse > 1
                             ? juce::jmax(accumulatedDetectorPower[0], accumulatedDetectorPower[1])
                             : accumulatedDetectorPower[0];

    {
        const auto coefficient = calculateTimeCoefficient(settings.adaptiveTimeMs, frameDurationSeconds);
        spectralAdaptivePower =
            coefficient * spectralAdaptivePower + (1.0f - coefficient) * static_cast<float>(detectorPower);
    }
    detectorPower = spectralAdaptivePower;

    const auto detectorRms = std::sqrt(detectorPower / static_cast<double>(processedBinCount));
    const auto desiredReferenceDb =
        juce::Decibels::gainToDecibels(static_cast<float>(detectorRms), detectorMinDecibels);
    updateAdaptiveReference(linkedAdaptiveReferenceDb, desiredReferenceDb, linkedAdaptiveHoldRemainingMs);

    if (settings.correlationMode && accumulatedCorrelationWeight > 1.0e-12)
    {
        auto crossPower = accumulatedCorrelation;
        auto totalPower = accumulatedCorrelationWeight;

        const auto coefficient = calculateTimeCoefficient(settings.adaptiveTimeMs, frameDurationSeconds);
        correlationAdaptiveCrossPower = coefficient * correlationAdaptiveCrossPower + (1.0f - coefficient) * crossPower;
        correlationAdaptivePower = coefficient * correlationAdaptivePower + (1.0f - coefficient) * totalPower;
        crossPower = correlationAdaptiveCrossPower;
        totalPower = correlationAdaptivePower;

        const auto desiredCorrelationReference =
            juce::jlimit(-1.0f, 1.0f, static_cast<float>(crossPower / juce::jmax(1.0e-12, totalPower)));
        updateAdaptiveReference(correlationAdaptiveReference, desiredCorrelationReference,
                                correlationAdaptiveHoldRemainingMs);
    }
}

void DynSpectralProcessor::DynamicProcessor::processFrame(const int channelsToUse, const ProcessingSettings& settings,
                                                          const int fftIndex, const int fftSize,
                                                          const int hopSize) noexcept
{

    if (settings.correlationMode != lastWasCorrelationMode)
    {
        for (auto& bank : thresholdHysteresis)
            bank.fill({});
        for (auto& bank : tensionThresholdHysteresis)
            bank.fill({});
        for (auto& bank : correlationEnvelopes)
            bank.fill({});
        for (auto& bank : tensionEnvelopes)
            bank.fill({});
        for (auto& bank : tensionDirections)
            bank.fill({});
        for (auto& bank : correlationResponseDistances)
            bank.fill({});
        for (auto& bank : correlationActionDistances)
            bank.fill({});
        smoothedTension = 0.0f;
        correlationRmsCrossPower.fill(0.0f);
        correlationRmsPower.fill(0.0f);
        correlationAdaptiveCrossPower = correlationAdaptivePower = 0.0;
        correlationAdaptiveReference = -1.0f;
        correlationAdaptiveHoldRemainingMs = 0.0f;
        lastWasCorrelationMode = settings.correlationMode;
    }

    const auto frameDurationSeconds = static_cast<float>(hopSize) / static_cast<float>(sampleRate);

    const auto coefficient = calculateTimeCoefficient(5.0f, frameDurationSeconds);
    smoothedTension = coefficient * smoothedTension + (1.0f - coefficient) * settings.tension;

    const auto publishedThreshold = calculatePublishedThreshold(settings);

    std::array<double, maxChannels> accumulatedDetectorPower{};
    auto accumulatedCorrelation = 0.0;
    auto accumulatedCorrelationWeight = 0.0;

    prepareFrameSpectrum(channelsToUse, fftIndex, fftSize);
    if (settings.separateDetector)
        prepareSidechainSpectrum(channelsToUse, fftIndex, fftSize);

    if (settings.correlationMode && channelsToUse >= 2)
        updateCorrelationDetector(settings, fftSize);

    if (settings.correlationMode)
    {
        processCorrelationSpectrum(channelsToUse, settings, fftSize, publishedThreshold, accumulatedDetectorPower,
                                   accumulatedCorrelation, accumulatedCorrelationWeight);
    }
    else
    {
        processLinkedSpectrum(channelsToUse, settings, fftSize, publishedThreshold, accumulatedDetectorPower);
    }

    updateAdaptiveReferences(channelsToUse, settings, fftSize, hopSize, accumulatedDetectorPower,
                             accumulatedCorrelation, accumulatedCorrelationWeight);
    synthesiseFrame(channelsToUse, fftIndex, fftSize);
}

void DynSpectralProcessor::DynamicProcessor::synthesiseFrame(const int channelsToUse, const int fftIndex,
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
