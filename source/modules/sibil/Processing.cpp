#include "Processor.h"

#include <JuceHeader.h>
#include <algorithm>
#include <cmath>

namespace ava::modules::chain::dsp
{
namespace
{
double smoothingCoefficient(double samples) noexcept
{
    return samples <= 0.0 ? 1.0 : -std::expm1(-1.0 / samples);
}

}

double SibilProcessor::Filter::process(double sample, const Coefficients& c) noexcept
{
    const auto output = c[0] * sample + z1;
    z1 = c[1] * sample - c[4] * output + z2;
    z2 = c[2] * sample - c[5] * output;
    return output;
}

void SibilProcessor::prepare(double rate)
{
    sampleRate = std::max(8000.0, rate);
    const auto highPass = [this](double frequency)
    {
        return juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(
            sampleRate, std::min(frequency, sampleRate * 0.42), std::sqrt(0.5));
    };
    presenceCoefficients = highPass(2000.0);
    highCoefficients = highPass(4500.0);
    const auto predictionLag = [this](double seconds)
    {
        return unsigned(std::clamp(int(std::lround(sampleRate * seconds)),
                                   1, int(Detector::historySize / 2 - 1)));
    };
    predictionLags = {predictionLag(1.0 / 24000.0), predictionLag(0.00045)};
    decisionInterval = std::max(1, juce::roundToInt(sampleRate * 0.0005));
    envelopeCoefficient = smoothingCoefficient(sampleRate * 0.0012);
    backgroundRise = smoothingCoefficient(sampleRate * 0.120 / decisionInterval);
    backgroundFall = smoothingCoefficient(sampleRate * 0.025 / decisionInterval);
    gainCoefficient = smoothingCoefficient(sampleRate * 0.005);
    audioDelay.resize(size_t(std::ceil(sampleRate * ava::modules::sibil::maximumLookaheadMs * 0.001)) + 1);
    setParameters(parameters, false);
    reset();
}

void SibilProcessor::reset() noexcept
{
    detectors = {};
    samplesToDecision = 1;
    std::fill(audioDelay.begin(), audioDelay.end(), std::array<double, 2>{});
    delayWrite = 0;
    detectorOpen = false;
    holdRemaining = 0;
    fadePosition = 0.0;
    nextTimelineSample.reset();
    lastTransportPlaying.reset();
    gainS = targetS;
    gainRest = targetRest;
}

void SibilProcessor::beginBlock(std::optional<int64_t> timelineSample,
                                std::optional<bool> playing, int blockSamples) noexcept
{
    if (blockSamples <= 0 || !isActive()) return;
    if (playing && !*playing) timelineSample.reset();
    const bool transportChanged = playing && lastTransportPlaying != playing;
    const bool jumped = timelineSample && nextTimelineSample && timelineSample != nextTimelineSample;
    const bool positionTrackingLost = !timelineSample && nextTimelineSample && (!playing || *playing);
    if (transportChanged || jumped || positionTrackingLost) reset();
    if (playing) lastTransportPlaying = playing;
    if (timelineSample)
    {
        if (!nextTimelineSample)
        {
            const auto phase = (*timelineSample % decisionInterval + decisionInterval) % decisionInterval;
            samplesToDecision = decisionInterval - int(phase);
        }
        nextTimelineSample = *timelineSample + blockSamples;
    }
    else nextTimelineSample.reset();
}

void SibilProcessor::setParameters(const ava::modules::sibil::Parameters& next, bool processed)
{
    const auto wasActive = isActive();
    parameters = next;
    lookaheadSamples = int(std::lround(sampleRate
        * std::clamp(next.lookaheadMs, 0.0f, ava::modules::sibil::maximumLookaheadMs) / 1000.0));
    const auto sensitivity = std::clamp(double(next.sensitivity) * 0.01, 0.0, 1.0);
    brightnessThreshold = 0.65 - 0.56 * sensitivity;
    // A brighter sustained tone should still fail when SENS is all the way up.
    noiseThreshold = std::clamp(0.22 + 0.25 * (double(next.noise) * 0.01 - 0.20), 0.12, 0.55);
    minimumContrast = 2.8 - 1.65 * sensitivity;
    levelThreshold = std::pow(10.0, double(next.level) * 0.1);
    // The detector is ahead of the delayed audio. Keep the mask open until that
    // audio has caught up, plus HOLD to bridge dips in the estimate.
    const auto extraHold = std::clamp(next.holdMs, 0.0f, ava::modules::sibil::maximumHoldMs);
    const auto nextHoldSamples = lookaheadSamples + decisionInterval
        + int(std::lround(sampleRate * extraHold / 1000.0));
    if (holdRemaining > 0)
        holdRemaining = std::max(0, holdRemaining + nextHoldSamples - holdSamples);
    holdSamples = nextHoldSamples;
    fadeStep = 1.0 / std::max(1, int(std::lround(std::clamp(double(next.fade), 0.0, 100.0)
                                                 * 0.001 * sampleRate)));
    const auto soloed = next.soloS || next.soloRest;
    const auto gain = [](float db) { return juce::Decibels::decibelsToGain(std::clamp(double(db), -48.0, 48.0)); };
    targetS = !soloed || next.soloS ? gain(next.gainS) : 0.0;
    targetRest = !soloed || next.soloRest ? gain(next.gainRest) : 0.0;
    if (wasActive != isActive()) reset();
    if (!processed)
    {
        gainS = targetS;
        gainRest = targetRest;
    }
}

void SibilProcessor::updateDetector(Detector& d, double sample) noexcept
{
    const auto present = d.presence.process(sample, presenceCoefficients);
    const auto high = d.high.process(sample, highCoefficients);
    const auto follow = [this](double& state, double value) { state += envelopeCoefficient * (value - state); };
    follow(d.levelPower, sample * sample);
    follow(d.presencePower, present * present);
    follow(d.highPower, high * high);
    // A sine is predictable at both spacings. Narrow frication can resemble
    // a sine over a few samples, but decorrelates over the longer spacing.
    for (size_t i = 0; i < predictionLags.size(); ++i)
    {
        const auto lag = predictionLags[i];
        const auto past = d.history[(d.position - lag) % Detector::historySize];
        const auto prediction = present + d.history[(d.position - 2 * lag) % Detector::historySize];
        auto& p = d.predictions[i];
        follow(p.power, prediction * prediction);
        follow(p.referencePower, past * past);
        follow(p.crossPower, prediction * past);
    }
    d.history[d.position] = present;
    d.position = (d.position + 1) % Detector::historySize;
}

bool SibilProcessor::detectsSibilance(Detector& d) const noexcept
{
    // Keep a reference for ordinary high-frequency energy. A new S should
    // stand out above it; freeze the reference once an event is established.
    const auto brightPower = 0.70 * d.presencePower + 0.30 * d.highPower;
    const auto thresholdRatio = detectorOpen ? 0.82 : 1.0;
    const auto aperiodicity = [](const Detector::Prediction& p)
    {
        if (p.referencePower <= 1.0e-12) return 0.0;
        const auto coherence = p.crossPower / p.referencePower;
        return std::clamp(0.5 * (p.power / p.referencePower - coherence * coherence), 0.0, 1.0);
    };
    const auto fastNoise = aperiodicity(d.predictions[0]);
    const auto slowNoise = aperiodicity(d.predictions[1]);
    const auto noise = std::max(fastNoise, 0.25 * fastNoise + 0.75 * slowNoise);
    const auto noisyPower = brightPower * noise;
    const bool detected = d.levelPower >= levelThreshold * thresholdRatio
        && brightPower >= d.levelPower * brightnessThreshold * thresholdRatio
        && noise >= noiseThreshold
        && (detectorOpen || noisyPower >= d.backgroundPower * minimumContrast);
    if (!detectorOpen && !detected)
    {
        const auto coefficient = noisyPower > d.backgroundPower ? backgroundRise : backgroundFall;
        d.backgroundPower += coefficient * (noisyPower - d.backgroundPower);
    }
    return detected;
}

void SibilProcessor::process(double& left, double& right) noexcept
{
    if (!isActive()) return;
    updateDetector(detectors[0], left);
    updateDetector(detectors[1], right);
    if (--samplesToDecision == 0)
    {
        const bool leftDetected = detectsSibilance(detectors[0]);
        const bool rightDetected = detectsSibilance(detectors[1]);
        detectorOpen = leftDetected || rightDetected;
        if (detectorOpen) holdRemaining = holdSamples;
        samplesToDecision = decisionInterval;
    }

    const bool open = holdRemaining > 0;
    if (open) --holdRemaining;
    fadePosition = std::clamp(fadePosition + (open ? fadeStep : -fadeStep), 0.0, 1.0);
    const auto mask = fadePosition * fadePosition * (3.0 - 2.0 * fadePosition);
    gainS += gainCoefficient * (targetS - gainS);
    gainRest += gainCoefficient * (targetRest - gainRest);

    if (!audioDelay.empty())
    {
        audioDelay[delayWrite] = {left, right};
        const auto size = audioDelay.size();
        const auto& delayed = audioDelay[(delayWrite + size - size_t(lookaheadSamples)) % size];
        left = delayed[0];
        right = delayed[1];
        if (++delayWrite == size) delayWrite = 0;
    }

    const auto mix = [this, mask](double input)
    {
        if (juce::exactlyEqual(gainS, gainRest)) return input * gainS;
        const auto s = input * mask;
        return s * gainS + (input - s) * gainRest;
    };
    left = mix(left);
    right = mix(right);
}
}
