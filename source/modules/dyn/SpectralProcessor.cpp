#include "SpectralProcessor.h"

DynSpectralProcessor::DynSpectralProcessor(juce::AudioProcessor& owner) : ownerProcessor(owner)
{
}

DynSpectralProcessor::~DynSpectralProcessor() = default;

void DynSpectralProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    maxLookaheadSamples = juce::jmax(0, juce::roundToInt(sampleRate * 0.03));
    deltaDelayBufferSize = DynamicProcessor::maxFftSize + maxLookaheadSamples + 1;
    for (auto& values : deltaDelayBuffers)
        values.assign(static_cast<size_t>(deltaDelayBufferSize), 0.0f);
    for (auto& values : sidechainDelayBuffers)
        values.assign(static_cast<size_t>(deltaDelayBufferSize), 0.0f);
    for (auto& values : lookaheadDelayBuffers)
        values.assign(static_cast<size_t>(maxLookaheadSamples + 2), 0.0f);
    lookaheadDelay.reset(sampleRate, 0.005);
    activeDynamicProcessorIndex = 0;
    activeFftSize = 4096;
    activeOverlapFactor = 32;
    pendingFftSize = 0;
    pendingOverlapFactor = 0;
    windowTransitionStage = WindowTransitionStage::none;
    windowTransitionSamples = juce::jmax(1, juce::roundToInt(sampleRate * 0.005));
    windowTransitionSamplesRemaining = 0;

    for (auto& processor : dynamicProcessors)
        processor.prepare(sampleRate, DynamicProcessor::maxChannels);

    refreshLatencyState();
    resetDeltaDelay();
    deltaDryBuffer.setSize(DynamicProcessor::maxChannels, preparedBlockSize);
    sidechainListenBuffer.setSize(DynamicProcessor::maxChannels, preparedBlockSize);
    internalDetectorBuffer.setSize(DynamicProcessor::maxChannels, preparedBlockSize);
    windowTransitionBuffer.setSize(DynamicProcessor::maxChannels, preparedBlockSize);
}

void DynSpectralProcessor::releaseResources()
{
    for (auto& processor : dynamicProcessors)
        processor.reset();

    windowTransitionStage = WindowTransitionStage::none;
    windowTransitionSamplesRemaining = 0;
    resetDeltaDelay();
}

void DynSpectralProcessor::resetProcessingState() noexcept
{
    for (auto& processor : dynamicProcessors)
        processor.reset();

    windowTransitionStage = WindowTransitionStage::none;
    windowTransitionSamplesRemaining = 0;
    pendingFftSize = 0;
    pendingOverlapFactor = 0;
    resetDeltaDelay();
}

void DynSpectralProcessor::processBlock(juce::AudioBuffer<float>& buffer, ProcessingSettings requestedSettings,
                                        const bool deltaEnabled, const juce::AudioBuffer<float>* sidechain,
                                        const int sidechainOffset)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.applyGain(juce::Decibels::decibelsToGain(requestedSettings.inputDb));
    const auto makeupGain = juce::Decibels::decibelsToGain(requestedSettings.makeupDb);
    const auto channelsToUse = juce::jmin(DynamicProcessor::maxChannels, buffer.getNumChannels());
    const auto windowConfigurationChanged =
        requestedSettings.fftSize != activeFftSize || requestedSettings.overlapFactor != activeOverlapFactor;

    if (windowTransitionStage == WindowTransitionStage::none && windowConfigurationChanged)
        beginWindowTransition(requestedSettings.fftSize, requestedSettings.overlapFactor);
    else if ((windowTransitionStage == WindowTransitionStage::priming ||
              windowTransitionStage == WindowTransitionStage::fadeOut) &&
             (requestedSettings.fftSize != pendingFftSize || requestedSettings.overlapFactor != pendingOverlapFactor))
    {
        beginWindowTransition(requestedSettings.fftSize, requestedSettings.overlapFactor);
    }

    ensureDeltaDryBufferSize(channelsToUse, buffer.getNumSamples());
    jassert(deltaDryBuffer.getNumChannels() >= channelsToUse &&
            deltaDryBuffer.getNumSamples() >= buffer.getNumSamples());
    const auto lookaheadSamples =
        juce::jlimit(0, maxLookaheadSamples,
                     juce::roundToInt(static_cast<float>(maxLookaheadSamples) * requestedSettings.lookaheadMs / 30.0f));
    const auto totalLatency = activeLatencySamples + lookaheadSamples;
    populateAlignedDryBuffer(buffer, deltaDryBuffer, channelsToUse, totalLatency);
    const juce::AudioBuffer<float>* detectorInput = sidechain;
    auto detectorOffset = sidechainOffset;

    populateAlignedSidechainBuffer(sidechain, sidechainOffset, channelsToUse, buffer.getNumSamples(), totalLatency);
    for (int channel = 0; channel < channelsToUse; ++channel)
        internalDetectorBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    if (!requestedSettings.useSidechainInput)
    {
        detectorInput = &internalDetectorBuffer;
        detectorOffset = 0;
    }
    requestedSettings.separateDetector =
        requestedSettings.useSidechainInput || lookaheadSamples > 0 || lookaheadDelay.getCurrentValue() > 0.0f;
    delayMainForLookahead(buffer, channelsToUse, lookaheadSamples);

    const auto processesPendingWindow = windowTransitionStage == WindowTransitionStage::priming ||
                                        windowTransitionStage == WindowTransitionStage::fadeOut;

    if (processesPendingWindow)
    {
        if (windowTransitionBuffer.getNumChannels() < channelsToUse ||
            windowTransitionBuffer.getNumSamples() < buffer.getNumSamples())
        {
            windowTransitionBuffer.setSize(channelsToUse, buffer.getNumSamples(), false, false, true);
        }

        for (auto channel = 0; channel < channelsToUse; ++channel)
            windowTransitionBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    }

    for (auto channel = DynamicProcessor::maxChannels; channel < ownerProcessor.getTotalNumOutputChannels();
         ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    auto activeSettings = requestedSettings;
    activeSettings.fftSize = activeFftSize;
    activeSettings.overlapFactor = activeOverlapFactor;
    dynamicProcessors[static_cast<size_t>(activeDynamicProcessorIndex)].processBuffer(
        buffer, channelsToUse, activeSettings, detectorInput, detectorOffset);

    if (processesPendingWindow)
    {
        auto pendingSettings = requestedSettings;
        pendingSettings.fftSize = pendingFftSize;
        pendingSettings.overlapFactor = pendingOverlapFactor;
        const auto pendingProcessorIndex = 1 - activeDynamicProcessorIndex;
        auto& pendingProcessor = dynamicProcessors[static_cast<size_t>(pendingProcessorIndex)];
        pendingProcessor.processBuffer(windowTransitionBuffer, channelsToUse, pendingSettings, detectorInput,
                                       detectorOffset);

        if (windowTransitionStage == WindowTransitionStage::fadeOut)
            applyWindowTransition(buffer, &windowTransitionBuffer, channelsToUse);
        else if (pendingProcessor.hasProducedOutput())
        {
            windowTransitionStage = WindowTransitionStage::fadeOut;
            windowTransitionSamplesRemaining = windowTransitionSamples;
        }
    }
    else if (windowTransitionStage == WindowTransitionStage::fadeIn)
    {
        applyWindowTransition(buffer, nullptr, channelsToUse);
    }

    const auto outputGain = juce::Decibels::decibelsToGain(requestedSettings.outputDb);
    const auto mix = juce::jlimit(0.0f, 1.0f, requestedSettings.mix);
    for (int channel = 0; channel < channelsToUse; ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = deltaDryBuffer.getSample(channel, i);
            const auto wet = buffer.getSample(channel, i);
            if (requestedSettings.sidechainListen)
            {
                buffer.setSample(channel, i,
                                 outputGain * (requestedSettings.useSidechainInput
                                                   ? sidechainListenBuffer.getSample(channel, i)
                                                   : dry));
                continue;
            }
            buffer.setSample(channel, i,
                             outputGain * (deltaEnabled ? (dry - wet) * mix : dry + (wet * makeupGain - dry) * mix));
        }
}

void DynSpectralProcessor::beginWindowTransition(const int fftSize, const int overlapFactor) noexcept
{
    pendingFftSize = juce::jlimit(1024, DynamicProcessor::maxFftSize, fftSize);
    pendingOverlapFactor = juce::jmax(1, overlapFactor);
    windowTransitionStage = WindowTransitionStage::priming;
    windowTransitionSamplesRemaining = 0;
    dynamicProcessors[static_cast<size_t>(1 - activeDynamicProcessorIndex)].reset();
}

void DynSpectralProcessor::applyWindowTransition(juce::AudioBuffer<float>& activeOutput,
                                                 const juce::AudioBuffer<float>* pendingOutput,
                                                 const int channelsToUse) noexcept
{
    for (auto sampleIndex = 0; sampleIndex < activeOutput.getNumSamples(); ++sampleIndex)
    {
        if (windowTransitionStage == WindowTransitionStage::fadeOut)
        {
            const auto gain =
                static_cast<float>(windowTransitionSamplesRemaining) / static_cast<float>(windowTransitionSamples);

            for (auto channel = 0; channel < channelsToUse; ++channel)
                activeOutput.setSample(channel, sampleIndex, activeOutput.getSample(channel, sampleIndex) * gain);

            if (--windowTransitionSamplesRemaining <= 0)
                completeWindowTransition();

            continue;
        }

        if (windowTransitionStage != WindowTransitionStage::fadeIn)
            continue;

        const auto gain =
            1.0f - (static_cast<float>(windowTransitionSamplesRemaining) / static_cast<float>(windowTransitionSamples));

        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            const auto sample = pendingOutput != nullptr ? pendingOutput->getSample(channel, sampleIndex)
                                                         : activeOutput.getSample(channel, sampleIndex);
            activeOutput.setSample(channel, sampleIndex, sample * gain);
        }

        if (--windowTransitionSamplesRemaining <= 0)
        {
            windowTransitionStage = WindowTransitionStage::none;
            windowTransitionSamplesRemaining = 0;
        }
    }
}

void DynSpectralProcessor::completeWindowTransition() noexcept
{
    activeDynamicProcessorIndex = 1 - activeDynamicProcessorIndex;
    activeFftSize = pendingFftSize;
    activeOverlapFactor = pendingOverlapFactor;
    activeLatencySamples = juce::jmax(0, activeFftSize - 1);
    pendingFftSize = 0;
    pendingOverlapFactor = 0;
    windowTransitionStage = WindowTransitionStage::fadeIn;
    windowTransitionSamplesRemaining = windowTransitionSamples;
}

int DynSpectralProcessor::getLatencySamples(const int lookaheadSamples) const noexcept
{
    return activeLatencySamples + juce::jlimit(0, maxLookaheadSamples, lookaheadSamples);
}

bool DynSpectralProcessor::refreshLatencyState() noexcept
{
    const auto newLatencySamples = juce::jmax(0, activeFftSize - 1);
    const auto changed = activeLatencySamples != newLatencySamples;
    activeLatencySamples = newLatencySamples;
    return changed;
}

void DynSpectralProcessor::resetDeltaDelay() noexcept
{
    deltaDelayWriteIndex = 0;
    sidechainDelayWriteIndex = 0;
    sidechainListenBuffer.clear();
    for (auto& channel : sidechainDelayBuffers)
        std::fill(channel.begin(), channel.end(), 0.0f);
    for (auto& channel : lookaheadDelayBuffers)
        std::fill(channel.begin(), channel.end(), 0.0f);
    lookaheadWriteIndex = 0;
    lookaheadDelay.setCurrentAndTargetValue(0.0f);
    deltaDryBuffer.clear();

    for (auto& channelBuffer : deltaDelayBuffers)
        std::fill(channelBuffer.begin(), channelBuffer.end(), 0.0f);
}

void DynSpectralProcessor::ensureDeltaDryBufferSize(const int channels, const int samples)
{
    const auto requiredChannels = juce::jmax(0, channels);
    const auto requiredSamples = juce::jmax(0, samples);
    if (internalDetectorBuffer.getNumChannels() < requiredChannels ||
        internalDetectorBuffer.getNumSamples() < requiredSamples)
        internalDetectorBuffer.setSize(requiredChannels, requiredSamples, false, false, true);
    if (sidechainListenBuffer.getNumChannels() < requiredChannels ||
        sidechainListenBuffer.getNumSamples() < requiredSamples)
        sidechainListenBuffer.setSize(requiredChannels, requiredSamples, false, false, true);

    if (deltaDryBuffer.getNumChannels() < requiredChannels || deltaDryBuffer.getNumSamples() < requiredSamples)
    {
        deltaDryBuffer.setSize(requiredChannels, requiredSamples, false, false, true);
    }
}

void DynSpectralProcessor::delayMainForLookahead(juce::AudioBuffer<float>& buffer, const int channels,
                                                 const int lookaheadSamples) noexcept
{
    lookaheadDelay.setTargetValue(static_cast<float>(lookaheadSamples));
    const auto capacity = maxLookaheadSamples + 2;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto delay = lookaheadDelay.getNextValue();
        const auto whole = static_cast<int>(delay);
        const auto fraction = delay - static_cast<float>(whole);
        const auto newer = (lookaheadWriteIndex + capacity - whole) % capacity;
        const auto older = (newer + capacity - 1) % capacity;
        for (int channel = 0; channel < channels; ++channel)
        {
            auto& history = lookaheadDelayBuffers[static_cast<size_t>(channel)];
            history[static_cast<size_t>(lookaheadWriteIndex)] = buffer.getSample(channel, sample);
            const auto value = whole == 0 && fraction == 0.0f
                                   ? buffer.getSample(channel, sample)
                                   : history[static_cast<size_t>(newer)] * (1.0f - fraction) +
                                         history[static_cast<size_t>(older)] * fraction;
            buffer.setSample(channel, sample, value);
        }
        lookaheadWriteIndex = (lookaheadWriteIndex + 1) % capacity;
    }
}

void DynSpectralProcessor::populateAlignedSidechainBuffer(const juce::AudioBuffer<float>* sidechain, const int offset,
                                                          const int channels, const int samples,
                                                          const int latencySamples) noexcept
{
    const auto delay = juce::jlimit(0, deltaDelayBufferSize - 1, latencySamples);
    const auto available = sidechain != nullptr && sidechain->getNumChannels() > 0 && offset >= 0 &&
                           offset + samples <= sidechain->getNumSamples();
    for (int sample = 0; sample < samples; ++sample)
    {
        const auto read = (sidechainDelayWriteIndex + deltaDelayBufferSize - delay) % deltaDelayBufferSize;
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto key =
                available ? sidechain->getSample(juce::jmin(channel, sidechain->getNumChannels() - 1), offset + sample)
                          : 0.0f;
            auto& history = sidechainDelayBuffers[static_cast<size_t>(channel)];
            sidechainListenBuffer.setSample(channel, sample, delay > 0 ? history[static_cast<size_t>(read)] : key);
            history[static_cast<size_t>(sidechainDelayWriteIndex)] = key;
        }
        sidechainDelayWriteIndex = (sidechainDelayWriteIndex + 1) % deltaDelayBufferSize;
    }
}

void DynSpectralProcessor::populateAlignedDryBuffer(const juce::AudioBuffer<float>& inputBuffer,
                                                    juce::AudioBuffer<float>& delayedDryBuffer, int channelsToUse,
                                                    int latencySamples) noexcept
{
    delayedDryBuffer.clear();

    if (channelsToUse <= 0)
        return;

    const auto delaySamples = juce::jlimit(0, deltaDelayBufferSize - 1, juce::jmax(0, latencySamples));

    if (delaySamples == 0)
    {
        for (auto channel = 0; channel < channelsToUse; ++channel)
            delayedDryBuffer.copyFrom(channel, 0, inputBuffer, channel, 0, inputBuffer.getNumSamples());

        return;
    }

    for (auto sampleIndex = 0; sampleIndex < inputBuffer.getNumSamples(); ++sampleIndex)
    {
        auto readIndex = deltaDelayWriteIndex - delaySamples;

        if (readIndex < 0)
            readIndex += deltaDelayBufferSize;

        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            auto& delayBuffer = deltaDelayBuffers[static_cast<size_t>(channel)];
            delayedDryBuffer.setSample(channel, sampleIndex, delayBuffer[static_cast<size_t>(readIndex)]);
            delayBuffer[static_cast<size_t>(deltaDelayWriteIndex)] = inputBuffer.getSample(channel, sampleIndex);
        }

        deltaDelayWriteIndex = (deltaDelayWriteIndex + 1) % deltaDelayBufferSize;
    }
}
