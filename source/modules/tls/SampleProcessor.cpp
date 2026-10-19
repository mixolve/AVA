#include "SampleProcessor.h"

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
    orderFadeLength = juce::jmax(1, int(currentSampleRate * 0.003));
    combEngine.prepare(currentSampleRate);
    initialisePhaseFilterCoefficients();
    clearState();
    updateDerivedParameters();
    combEngine.reset();
}

void DspCore::reset()
{
    clearState();
    updateDerivedParameters();
}

void DspCore::setParameters(const Parameters& newParameters)
{
    const auto wasCombEnabled=derived.combEnabled;
    parameters = newParameters;
    if (! blocks::valid(parameters.blockOrder)) parameters.blockOrder = blocks::defaultOrder;
    if (! gain::valid(parameters.gainOrder)) parameters.gainOrder = gain::defaultOrder;
    if (! hasProcessedSamples) {
        activeOrder = parameters.blockOrder;
        activeGainOrder = parameters.gainOrder;
    }
    else if ((activeOrder != parameters.blockOrder || activeGainOrder != parameters.gainOrder) && ! orderFadingOut) {
        orderFadingOut = true;
        orderFadeRemaining = orderFadeLength;
    }
    updateDerivedParameters();
    if(wasCombEnabled!=derived.combEnabled) combEngine.reset();
}

int DspCore::getLatencySamples() const noexcept
{
    return derived.latencySamples;
}

}
