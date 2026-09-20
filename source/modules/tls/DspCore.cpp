#include "DspCore.h"

namespace tls::dsp
{
void DspCore::prepare(const double sampleRate, const int, const int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const auto maxDelaySamples = msToSamples(maximumCombinedDelayMs, currentSampleRate);
    auto requiredDelaySize = juce::jmax(1, maxDelaySamples + 2);
    delayBufferSize = 1;
    while (delayBufferSize < requiredDelaySize)
        delayBufferSize *= 2;
    leftDelayBuffer.assign(static_cast<size_t>(delayBufferSize), 0.0);
    rightDelayBuffer.assign(static_cast<size_t>(delayBufferSize), 0.0);
    initialisePhaseFilterCoefficients();
    clearState();
    updateDerivedParameters();
}

void DspCore::reset()
{
    clearState();
    updateDerivedParameters();
}

void DspCore::setParameters(const Parameters& newParameters)
{
    parameters = newParameters;
    updateDerivedParameters();
}

int DspCore::getLatencySamples() const noexcept
{
    return derived.latencySamples;
}

int DspCore::getMaximumLatencySamples(const double sampleRate) noexcept
{
    const auto safeSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    return msToSamples(maximumCombinedDelayMs, safeSampleRate) + phaseFilterLatency;
}
}
