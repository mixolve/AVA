#include "Processor.h"
#include "Constants.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

FftModuleProcessor::DynamicProcessor::DynamicProcessor()
{
    const std::array<int, 5> fftOrders { 10, 11, 12, 13, 14 };

    for (auto i = 0; i < static_cast<int>(fftOrders.size()); ++i)
    {
        const auto fftSize = 1 << fftOrders[static_cast<size_t>(i)];
        ffts[static_cast<size_t>(i)] = std::make_unique<juce::dsp::FFT>(fftOrders[static_cast<size_t>(i)]);

        for (auto sampleIndex = 0; sampleIndex < fftSize; ++sampleIndex)
        {
            windowTables[static_cast<size_t>(i)][static_cast<size_t>(sampleIndex)]
                = 0.5f * (1.0f - std::cos((2.0f * juce::MathConstants<float>::pi * static_cast<float>(sampleIndex))
                                          / static_cast<float>(fftSize - 1)));
        }
    }
}

void FftModuleProcessor::DynamicProcessor::prepare(double newSampleRate, int numChannels)
{
    sampleRate = juce::jmax(1.0, newSampleRate);
    configuredChannels = juce::jlimit(0, maxChannels, numChannels);
    reconfigure(configuredChannels, 0, 0);
}

void FftModuleProcessor::DynamicProcessor::reset() noexcept
{
    reconfigure(configuredChannels, currentFftSize, currentHopSize);
}

void FftModuleProcessor::DynamicProcessor::copyReductionScope(std::array<float, analyserScopeSize>& leftDestination,
                                                                std::array<float, analyserScopeSize>& rightDestination) const
{
    const auto activeIndex = activeReductionScopeBuffer.load(std::memory_order_acquire);
    leftDestination = reductionScopeBuffers[static_cast<size_t>(activeIndex)][0];
    rightDestination = reductionScopeBuffers[static_cast<size_t>(activeIndex)][1];
}

void FftModuleProcessor::DynamicProcessor::processBuffer(juce::AudioBuffer<float>& buffer,
                                                           int numInputChannels,
                                                           const ProcessingSettings& settings)
{
    const auto channelsToUse = juce::jlimit(0, maxChannels, juce::jmin(numInputChannels, buffer.getNumChannels()));

    if (channelsToUse <= 0)
        return;

    const auto fftSize = juce::jlimit(1024, maxFftSize, settings.fftSize);
    const auto overlapFactor = juce::jmax(1, settings.overlapFactor);
    const auto hopSize = juce::jmax(1, fftSize / overlapFactor);

    if (channelsToUse != configuredChannels || fftSize != currentFftSize || hopSize != currentHopSize)
        reconfigure(channelsToUse, fftSize, hopSize);

    const auto fftIndex = getFftIndexForSize(fftSize);

    for (auto sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
    {
        for (auto channel = 0; channel < channelsToUse; ++channel)
            hopBuffers[static_cast<size_t>(channel)][static_cast<size_t>(hopFill)] = buffer.getSample(channel, sampleIndex);

        ++hopFill;

        if (hopFill >= hopSize)
        {
            for (auto channel = 0; channel < channelsToUse; ++channel)
            {
                auto& state = channelStates[static_cast<size_t>(channel)];
                std::move(state.analysisFifo.begin() + hopSize,
                          state.analysisFifo.begin() + fftSize,
                          state.analysisFifo.begin());
                std::copy_n(hopBuffers[static_cast<size_t>(channel)].begin(),
                            hopSize,
                            state.analysisFifo.begin() + (fftSize - hopSize));
                state.analysisFilled = juce::jmin(fftSize, state.analysisFilled + hopSize);
            }

            if (channelStates[0].analysisFilled >= fftSize)
            {
                processFrame(channelsToUse, settings, fftIndex, fftSize, hopSize);
                outputPrimed = true;
            }

            for (auto channel = 0; channel < channelsToUse; ++channel)
                pushOutputChunk(channelStates[static_cast<size_t>(channel)], fftSize, hopSize);

            hopFill = 0;
        }

        for (auto channel = 0; channel < channelsToUse; ++channel)
            buffer.setSample(channel, sampleIndex, dequeueOutputSample(channelStates[static_cast<size_t>(channel)]));
    }
}

