#include "Processor.h"
#include "Constants.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

void FftModuleProcessor::DynamicProcessor::enqueueOutputSample(ChannelState& state, float sample) noexcept
{
    if (state.readyOutputCount >= maxQueueSize)
        return;

    state.readyOutput[static_cast<size_t>(state.readyOutputWrite)] = sample;
    state.readyOutputWrite = (state.readyOutputWrite + 1) % maxQueueSize;
    ++state.readyOutputCount;
}

float FftModuleProcessor::DynamicProcessor::dequeueOutputSample(ChannelState& state) noexcept
{
    if (state.readyOutputCount <= 0)
        return 0.0f;

    const auto sample = state.readyOutput[static_cast<size_t>(state.readyOutputRead)];
    state.readyOutputRead = (state.readyOutputRead + 1) % maxQueueSize;
    --state.readyOutputCount;
    return sample;
}


void FftModuleProcessor::DynamicProcessor::pushOutputChunk(ChannelState& state, int fftSize, int hopSize) noexcept
{
    for (auto sampleIndex = 0; sampleIndex < hopSize; ++sampleIndex)
    {
        const auto normalization = state.normalizationAccum[static_cast<size_t>(sampleIndex)];
        const auto outputSample = normalization > 1.0e-6f
                                ? state.outputAccum[static_cast<size_t>(sampleIndex)] / normalization
                                : 0.0f;
        enqueueOutputSample(state, outputSample);
    }

    std::move(state.outputAccum.begin() + hopSize,
              state.outputAccum.begin() + fftSize,
              state.outputAccum.begin());
    std::fill(state.outputAccum.begin() + (fftSize - hopSize),
              state.outputAccum.begin() + fftSize,
              0.0f);

    std::move(state.normalizationAccum.begin() + hopSize,
              state.normalizationAccum.begin() + fftSize,
              state.normalizationAccum.begin());
    std::fill(state.normalizationAccum.begin() + (fftSize - hopSize),
              state.normalizationAccum.begin() + fftSize,
              0.0f);
}

void FftModuleProcessor::DynamicProcessor::reconfigure(int channelsToUse, int fftSize, int hopSize) noexcept
{
    configuredChannels = juce::jlimit(0, maxChannels, channelsToUse);
    currentFftSize = fftSize;
    currentHopSize = hopSize;
    hopFill = 0;
    currentDetectorLowCutHz = -1.0f;
    currentDetectorHighCutHz = -1.0f;
    dualMonoAdaptiveReferenceDb.fill(0.0f);
    correlationAdaptiveReference.fill(-1.0f);
    dualMonoAdaptiveHoldRemainingMs.fill(0.0f);
    correlationAdaptiveHoldRemainingMs.fill(0.0f);
    for (auto& channelReduction : dualMonoSmoothedReductionDb)
        std::fill(channelReduction.begin(), channelReduction.end(), 0.0f);
    for (auto& channelReduction : phaseSmoothedReductionRadians)
        std::fill(channelReduction.begin(), channelReduction.end(), 0.0f);
    frequencySmoothedBalanceRotation.fill(0.0f);
    signedSmoothedMidSideRotation.fill(0.0f);
    for (auto& channelReduction : correlationChanges)
        std::fill(channelReduction.begin(), channelReduction.end(), 0.0f);
    correlationDetector.fill(0.0f);
    detectedCorrelation.fill(0.0f);
    correlationColumnSums.fill(0.0f);
    smoothedCorrelationColumns.fill(0.0f);
    correlationColumnCounts.fill(0);
    outputPrimed = false;
    lastWasCorrelationMode = false;
    lastCorrelationType = CorrelationType::phase;
    activeReductionScopeBuffer.store(0, std::memory_order_relaxed);
    for (auto& reductionScope : reductionScopeBuffers)
        for (auto& channelReductionScope : reductionScope)
            std::fill(channelReductionScope.begin(), channelReductionScope.end(), 0.0f);
    for (auto& channelReductionScope : smoothedReductionScopes)
        std::fill(channelReductionScope.begin(), channelReductionScope.end(), 0.0f);

    for (auto channel = 0; channel < maxChannels; ++channel)
    {
        auto& state = channelStates[static_cast<size_t>(channel)];
        std::fill(state.analysisFifo.begin(), state.analysisFifo.end(), 0.0f);
        std::fill(state.outputAccum.begin(), state.outputAccum.end(), 0.0f);
        std::fill(state.normalizationAccum.begin(), state.normalizationAccum.end(), 0.0f);
        std::fill(state.readyOutput.begin(), state.readyOutput.end(), 0.0f);
        std::fill(hopBuffers[static_cast<size_t>(channel)].begin(),
                  hopBuffers[static_cast<size_t>(channel)].end(),
                  0.0f);

        for (auto& value : state.frequencyData)
            value = {};

        state.readyOutputRead = 0;
        state.readyOutputWrite = 0;
        state.readyOutputCount = 0;
        state.analysisFilled = 0;
    }
}

int FftModuleProcessor::DynamicProcessor::getFftIndexForSize(int fftSize) const noexcept
{
    switch (fftSize)
    {
        case 1024: return 0;
        case 2048: return 1;
        case 4096: return 2;
        case 8192: return 3;
        case 16384: return 4;
        default: return 3;
    }
}

float FftModuleProcessor::calculateReduction(float detectorValue,
                                              float threshold,
                                              float ratio,
                                              float knee) noexcept
{
    const auto safeRatio = juce::jmax(1.0f, ratio);
    const auto safeKnee = juce::jmax(0.0f, knee);
    const auto ratioFactor = 1.0f - (1.0f / safeRatio);
    const auto distanceAboveThreshold = detectorValue - threshold;

    if (safeKnee > 0.0f)
    {
        const auto halfKnee = safeKnee * 0.5f;

        if (distanceAboveThreshold <= -halfKnee)
            return 0.0f;

        if (distanceAboveThreshold >= halfKnee)
            return ratioFactor * juce::jmax(0.0f, distanceAboveThreshold);

        const auto kneePosition = distanceAboveThreshold + halfKnee;
        return ratioFactor * (kneePosition * kneePosition) / (2.0f * safeKnee);
    }

    return ratioFactor * juce::jmax(0.0f, distanceAboveThreshold);
}

float FftModuleProcessor::calculateGainChange(const float detectorValue,
                                             const float threshold,
                                             const float ratio,
                                             const float knee,
                                             const bool upward) noexcept
{
    // The upward knee starts at the threshold, never below it.
    const auto amount = calculateReduction(detectorValue, threshold + (upward ? knee * 0.5f : 0.0f), ratio, knee);
    return upward ? amount : -amount;
}

float FftModuleProcessor::calculateTimeCoefficient(float timeMs,
                                                     float frameDurationSeconds) noexcept
{
    if (timeMs <= 0.0f)
        return 0.0f;

    const auto timeSeconds = timeMs * 0.001f;
    return std::exp(-frameDurationSeconds / timeSeconds);
}
