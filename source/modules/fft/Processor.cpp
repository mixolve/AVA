#include "Processor.h"

FftModuleProcessor::FftModuleProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
    parameters(moduleParameterHost, nullptr, "fft_state", createParameterLayout())
{
    resetAnalyserState();
    dualMonoLeftThresholdParam = parameters.getRawParameterValue(paramDualMonoLeftThresholdId);
    dualMonoRightThresholdParam = parameters.getRawParameterValue(paramDualMonoRightThresholdId);
    correlationThresholdParam = parameters.getRawParameterValue(paramCorrelationThresholdId);
    correlationSmoothingParam = parameters.getRawParameterValue(paramCorrelationSmoothingId);
    correlationAdaptiveParam = parameters.getRawParameterValue(paramCorrelationAdaptiveId);
    correlationSlopeParam = parameters.getRawParameterValue(paramCorrelationSlopeId);
    correlationImpactParam = parameters.getRawParameterValue(paramCorrelationImpactId);
    dualMonoLeftAdaptiveParam = parameters.getRawParameterValue(paramDualMonoLeftAdaptiveId);
    dualMonoRightAdaptiveParam = parameters.getRawParameterValue(paramDualMonoRightAdaptiveId);
    spectralAdaptiveOffsetParam = parameters.getRawParameterValue(paramSpectralAdaptiveOffsetId);
    spectralAdaptiveAttackParam = parameters.getRawParameterValue(paramSpectralAdaptiveAttackId);
    spectralAdaptiveHoldParam = parameters.getRawParameterValue(paramSpectralAdaptiveHoldId);
    spectralAdaptiveReleaseParam = parameters.getRawParameterValue(paramSpectralAdaptiveReleaseId);
    correlationAdaptiveOffsetParam = parameters.getRawParameterValue(paramCorrelationAdaptiveOffsetId);
    correlationAdaptiveAttackParam = parameters.getRawParameterValue(paramCorrelationAdaptiveAttackId);
    correlationAdaptiveHoldParam = parameters.getRawParameterValue(paramCorrelationAdaptiveHoldId);
    correlationAdaptiveReleaseParam = parameters.getRawParameterValue(paramCorrelationAdaptiveReleaseId);
    dualMonoLinkParam = parameters.getRawParameterValue(paramDualMonoLinkId);
    dynamicBypassParam = parameters.getRawParameterValue(paramDynamicBypassId);
    dynamicModeParam = parameters.getRawParameterValue(paramDynamicModeId);
    correlationTypeParam = parameters.getRawParameterValue(paramCorrelationTypeId);
    dynamicDirectionParam = parameters.getRawParameterValue(paramDynamicDirectionId);
    detectorLowCutParam = parameters.getRawParameterValue(paramDetectorLowCutId);
    detectorHighCutParam = parameters.getRawParameterValue(paramDetectorHighCutId);
    floorParam = parameters.getRawParameterValue(paramFloorId);
    attackParam = parameters.getRawParameterValue(paramAttackId);
    releaseParam = parameters.getRawParameterValue(paramReleaseId);
    kneeParam = parameters.getRawParameterValue(paramKneeId);
    ratioParam = parameters.getRawParameterValue(paramRatioId);
    deltaParam = parameters.getRawParameterValue(paramDeltaId);
    dspFftSizeParam = parameters.getRawParameterValue(paramDspFftSizeId);
    dspOverlapParam = parameters.getRawParameterValue(paramDspOverlapId);
    dspSlopeParam = parameters.getRawParameterValue(paramDspSlopeId);
    setDualMonoLinkListenersEnabled(true);
}

FftModuleProcessor::~FftModuleProcessor()
{
    setDualMonoLinkListenersEnabled(false);
}

void FftModuleProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    activeDynamicProcessorIndex = 0;
    activeFftSize = getSelectedDspFftSize();
    activeOverlapFactor = getSelectedDspOverlapFactor();
    pendingFftSize = 0;
    pendingOverlapFactor = 0;
    windowTransitionStage = WindowTransitionStage::none;
    windowTransitionSamples = juce::jmax(1, juce::roundToInt(sampleRate * 0.005));
    windowTransitionSamplesRemaining = 0;

    for (auto& processor : dynamicProcessors)
        processor.prepare(sampleRate, ownerProcessor.getTotalNumInputChannels());

    refreshLatencyState();
    resetDeltaDelay();
    deltaDryBuffer.setSize(ownerProcessor.getTotalNumInputChannels(), preparedBlockSize);
    windowTransitionBuffer.setSize(ownerProcessor.getTotalNumInputChannels(), preparedBlockSize);
}

void FftModuleProcessor::releaseResources()
{
    for (auto& processor : dynamicProcessors)
        processor.reset();

    windowTransitionStage = WindowTransitionStage::none;
    windowTransitionSamplesRemaining = 0;
    resetDeltaDelay();
}

void FftModuleProcessor::resetProcessingState() noexcept
{
    for (auto& processor : dynamicProcessors)
        processor.reset();

    windowTransitionStage = WindowTransitionStage::none;
    windowTransitionSamplesRemaining = 0;
    pendingFftSize = 0;
    pendingOverlapFactor = 0;
    resetDeltaDelay();
}

void FftModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    auto requestedSettings = getProcessingSettings();
    const auto deltaEnabled = isDeltaEnabled();
    const auto channelsToUse = juce::jmin(ownerProcessor.getTotalNumInputChannels(), buffer.getNumChannels());
    const auto windowConfigurationChanged = requestedSettings.fftSize != activeFftSize
                                         || requestedSettings.overlapFactor != activeOverlapFactor;

    if (windowTransitionStage == WindowTransitionStage::none && windowConfigurationChanged)
        beginWindowTransition(requestedSettings.fftSize, requestedSettings.overlapFactor);
    else if ((windowTransitionStage == WindowTransitionStage::priming
              || windowTransitionStage == WindowTransitionStage::fadeOut)
             && (requestedSettings.fftSize != pendingFftSize
                 || requestedSettings.overlapFactor != pendingOverlapFactor))
    {
        beginWindowTransition(requestedSettings.fftSize, requestedSettings.overlapFactor);
    }

    ensureDeltaDryBufferSize(channelsToUse, buffer.getNumSamples());
    jassert(deltaDryBuffer.getNumChannels() >= channelsToUse && deltaDryBuffer.getNumSamples() >= buffer.getNumSamples());
    populateAlignedDryBuffer(buffer, deltaDryBuffer, channelsToUse, activeLatencySamples);

    const auto processesPendingWindow = windowTransitionStage == WindowTransitionStage::priming
                                     || windowTransitionStage == WindowTransitionStage::fadeOut;

    if (processesPendingWindow)
    {
        if (windowTransitionBuffer.getNumChannels() < channelsToUse
            || windowTransitionBuffer.getNumSamples() < buffer.getNumSamples())
        {
            windowTransitionBuffer.setSize(channelsToUse, buffer.getNumSamples(), false, false, true);
        }

        for (auto channel = 0; channel < channelsToUse; ++channel)
            windowTransitionBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
    }

    for (auto channel = ownerProcessor.getTotalNumInputChannels(); channel < ownerProcessor.getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (deltaEnabled)
        requestedSettings.makeupDb = 0.0f;

    auto activeSettings = requestedSettings;
    activeSettings.fftSize = activeFftSize;
    activeSettings.overlapFactor = activeOverlapFactor;
    dynamicProcessors[static_cast<size_t>(activeDynamicProcessorIndex)].processBuffer(
        buffer, channelsToUse, activeSettings);

    if (processesPendingWindow)
    {
        auto pendingSettings = requestedSettings;
        pendingSettings.fftSize = pendingFftSize;
        pendingSettings.overlapFactor = pendingOverlapFactor;
        const auto pendingProcessorIndex = 1 - activeDynamicProcessorIndex;
        auto& pendingProcessor = dynamicProcessors[static_cast<size_t>(pendingProcessorIndex)];
        pendingProcessor.processBuffer(windowTransitionBuffer, channelsToUse, pendingSettings);

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

    if (deltaEnabled)
    {
        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            buffer.applyGain(channel, 0, buffer.getNumSamples(), -1.0f);
            buffer.addFrom(channel, 0, deltaDryBuffer, channel, 0, buffer.getNumSamples());
        }
    }

}

void FftModuleProcessor::beginWindowTransition(const int fftSize, const int overlapFactor) noexcept
{
    pendingFftSize = juce::jlimit(1024, DynamicProcessor::maxFftSize, fftSize);
    pendingOverlapFactor = juce::jmax(1, overlapFactor);
    windowTransitionStage = WindowTransitionStage::priming;
    windowTransitionSamplesRemaining = 0;
    dynamicProcessors[static_cast<size_t>(1 - activeDynamicProcessorIndex)].reset();
}

void FftModuleProcessor::applyWindowTransition(juce::AudioBuffer<float>& activeOutput,
                                               const juce::AudioBuffer<float>* pendingOutput,
                                               const int channelsToUse) noexcept
{
    for (auto sampleIndex = 0; sampleIndex < activeOutput.getNumSamples(); ++sampleIndex)
    {
        if (windowTransitionStage == WindowTransitionStage::fadeOut)
        {
            const auto gain = static_cast<float>(windowTransitionSamplesRemaining)
                            / static_cast<float>(windowTransitionSamples);

            for (auto channel = 0; channel < channelsToUse; ++channel)
                activeOutput.setSample(channel, sampleIndex,
                                       activeOutput.getSample(channel, sampleIndex) * gain);

            if (--windowTransitionSamplesRemaining <= 0)
                completeWindowTransition();

            continue;
        }

        if (windowTransitionStage != WindowTransitionStage::fadeIn)
            continue;

        const auto gain = 1.0f - (static_cast<float>(windowTransitionSamplesRemaining)
                                / static_cast<float>(windowTransitionSamples));

        for (auto channel = 0; channel < channelsToUse; ++channel)
        {
            const auto sample = pendingOutput != nullptr
                ? pendingOutput->getSample(channel, sampleIndex)
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

void FftModuleProcessor::completeWindowTransition() noexcept
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

int FftModuleProcessor::getLatencySamples() const noexcept
{
    return activeLatencySamples;
}

bool FftModuleProcessor::refreshLatencyState() noexcept
{
    const auto newLatencySamples = juce::jmax(0, activeFftSize - 1);
    const auto changed = activeLatencySamples != newLatencySamples;
    activeLatencySamples = newLatencySamples;
    return changed;
}

void FftModuleProcessor::resetDeltaDelay() noexcept
{
    deltaDelayWriteIndex = 0;
    deltaDryBuffer.clear();

    for (auto& channelBuffer : deltaDelayBuffers)
        channelBuffer.fill(0.0f);
}

void FftModuleProcessor::ensureDeltaDryBufferSize(const int channels, const int samples)
{
    const auto requiredChannels = juce::jmax(0, channels);
    const auto requiredSamples = juce::jmax(0, samples);

    if (deltaDryBuffer.getNumChannels() < requiredChannels
        || deltaDryBuffer.getNumSamples() < requiredSamples)
    {
        deltaDryBuffer.setSize(requiredChannels, requiredSamples, false, false, true);
    }
}

void FftModuleProcessor::populateAlignedDryBuffer(const juce::AudioBuffer<float>& inputBuffer,
                                                  juce::AudioBuffer<float>& delayedDryBuffer,
                                                  int channelsToUse,
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
