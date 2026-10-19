#include "Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../routing/Runtime.h"

#include <cmath>

void AvaAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    processingPrepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(processingLock);

    currentSampleRate = sampleRate;
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    preparedNumChannels = static_cast<int>(maxSupportedChannels);
    const auto maximumRangeLatencySamples = static_cast<int>(std::ceil(sampleRate * 0.25)) + 16384;
    crossoverRouter.prepare(sampleRate,
                            preparedBlockSize,
                            preparedNumChannels,
                            maximumRangeLatencySamples);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->prepareToPlay(sampleRate, preparedBlockSize);

    if (tlsModuleProcessor != nullptr)
        tlsModuleProcessor->prepareToPlay(sampleRate, preparedBlockSize);
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->prepareToPlay(sampleRate, preparedBlockSize);

    routingChannels.setSize(routingChannelCount, preparedBlockSize);
    selectedInputBuffer.setSize(preparedNumChannels, preparedBlockSize);
    routingInputSnapshot.setSize(routingChannelCount, preparedBlockSize);
    unselectedDelay.setSize(routingChannelCount, maximumRangeLatencySamples + 1);
    unselectedDelay.clear();
    unselectedDelayPosition = 0;
    unselectedLatency = 0;
    lastProcessingMask = 0;

    if (routingRuntime != nullptr)
        routingRuntime->prepare(sampleRate, preparedBlockSize, routingChannelCount);

    updateShellLatency();
    processingPrepared.store(true, std::memory_order_release);
}

void AvaAudioProcessor::releaseResources()
{
    processingPrepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(processingLock);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->releaseResources();

    if (tlsModuleProcessor != nullptr)
        tlsModuleProcessor->releaseResources();
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->releaseResources();

    if (routingRuntime != nullptr)
        routingRuntime->release();

    crossoverRouter.reset();

    requestLatencySamples(0);
    currentSampleRate = 0.0;
}

void AvaAudioProcessor::reset()
{
    const juce::ScopedLock lock(processingLock);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->resetProcessingState();

    if (tlsModuleProcessor != nullptr)
        tlsModuleProcessor->resetProcessingState();
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->resetProcessingState();

    if (routingRuntime != nullptr)
        routingRuntime->reset();

    crossoverRouter.reset();
    unselectedDelay.clear();
    unselectedDelayPosition = 0;
    lastProcessingMask = 0;

    globalClipIndicator.store(0.0f, std::memory_order_relaxed);
}

int AvaAudioProcessor::getActiveModuleLatencySamples() const noexcept
{
    const auto module = activeModule.load(std::memory_order_acquire);
    const auto requestedRangeCount = getCrossoverSettings().activeSplitCount + 1;
    const auto maxActiveLatency = [requestedRangeCount] (const auto& processor)
    {
        const auto latencies = processor.getRangeLatencies();
        const auto activeRangeCount = std::min(requestedRangeCount, processor.getCreatedRangeCount());

        if (activeRangeCount == 0)
            return 0;

        return *std::max_element(latencies.begin(),
                                 latencies.begin() + static_cast<std::ptrdiff_t>(activeRangeCount));
    };

    switch (module)
    {
        case ActiveModule::tls:
            if (const auto* processor = getTlsModuleProcessor())
                return maxActiveLatency(*processor);
            break;

        case ActiveModule::dyn:
            if (const auto* processor = getDynModuleProcessor())
                return maxActiveLatency(*processor);
            break;

        case ActiveModule::eql:
            if (const auto* processor = getEqlProcessorBank())
                return maxActiveLatency(*processor);
            break;

        case ActiveModule::none:
            break;
    }

    return 0;
}

void AvaAudioProcessor::requestLatencySamples(const int latencySamples) noexcept
{
    const auto constrainedLatency = juce::jmax(0, latencySamples);
    const auto previousRequest = requestedLatencySamples.exchange(constrainedLatency, std::memory_order_acq_rel);

    if (previousRequest == constrainedLatency)
        return;

    if (parentStateChanged != nullptr)
        parentStateChanged();

    if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        messageManager != nullptr && messageManager->isThisTheMessageThread())
    {
        applyPendingShellUpdates();
        return;
    }

    triggerAsyncUpdate();
}

void AvaAudioProcessor::updateShellLatency() noexcept
{
    auto totalLatencySamples = routingRuntime != nullptr && routingRuntime->hasMultipleInstances()
        ? routingRuntime->getLatencySamples() : getActiveModuleLatencySamples();

    if (abCompareLatencyLocked.load(std::memory_order_acquire))
    {
        totalLatencySamples = juce::jmax(totalLatencySamples,
                                         abCompareLatencyFloorSamples.load(std::memory_order_acquire));
    }

    requestLatencySamples(totalLatencySamples);
}

bool AvaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainInput = layouts.getMainInputChannelSet();
    const auto mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput)
        return false;

    if (layouts.outputBuses.size() != 1 || layouts.inputBuses.size() != routingChannelCount / 2)
        return false;
    for (int bus = 1; bus < layouts.inputBuses.size(); ++bus)
    {
        const auto channels = layouts.getChannelSet(true, bus);
        if (!channels.isDisabled() && channels != juce::AudioChannelSet::mono()
            && channels != juce::AudioChannelSet::stereo()) return false;
    }
    return mainInput == juce::AudioChannelSet::mono()
        || mainInput == juce::AudioChannelSet::stereo();
}

juce::StringArray AvaAudioProcessor::getProcessingInputChoices()
{
    return ava::channels::choices();
}

bool AvaAudioProcessor::isProcessingInputAvailable(const int choiceIndex) const
{
    const auto* host = routingOwner != nullptr ? routingOwner : this;
    return ava::channels::isAvailable(*host, choiceIndex);
}

std::array<int, 2> AvaAudioProcessor::getProcessingInputChannels() const noexcept
{
    const auto choice = processingInputParam != nullptr ? juce::roundToInt(processingInputParam->load()) : 0;
    return ava::channels::decode(choice);
}

void AvaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedTryLock blockLock(processingLock);
    if (!blockLock.isLocked()) return;
    // Each bus owns a stable pair, including disabled/mono buses. Channel numbers
    // therefore never change when the host enables another auxiliary input.
    routingChannels.setSize(routingChannelCount, buffer.getNumSamples(), false, false, true);
    routingChannels.clear();
    for (int bus = 0; bus < getBusCount(true); ++bus)
    {
        auto input = getBusBuffer(buffer, true, bus);
        for (int channel = 0; channel < input.getNumChannels(); ++channel)
            routingChannels.copyFrom(bus * 2 + channel, 0, input, channel, 0, buffer.getNumSamples());
    }
    processChannelBlock(routingChannels, midi);
    auto output = getBusBuffer(buffer, false, 0);
    auto* monitor = routingRuntime != nullptr ? routingRuntime->getSoloProcessor(true)
        : soloChannelParam != nullptr && soloChannelParam->load() >= 0.5f ? this : nullptr;
    if (globalBypassParam != nullptr && globalBypassParam->load() >= 0.5f) monitor = nullptr;
    const auto selected = monitor != nullptr ? monitor->getProcessingInputChannels() : std::array<int, 2> { 0, 1 };
    const auto right = selected[1] >= 0 && getChannelCountOfBus(true, selected[1] / 2) > selected[1] % 2
        ? selected[1] : selected[0];
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        output.copyFrom(channel, 0, routingChannels, channel == 0 ? selected[0] : right, 0, buffer.getNumSamples());
    if (monitor != nullptr && output.getNumChannels() == 1 && right != selected[0])
    {
        output.applyGain(0.5f);
        output.addFrom(0, 0, routingChannels, right, 0, buffer.getNumSamples(), 0.5f);
    }
}

void AvaAudioProcessor::processChannelBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedTryLock lock(processingLock);

    if (! lock.isLocked())
    {
        globalClipIndicator.store(0.0f, std::memory_order_relaxed);
        return;
    }

    if (! processingPrepared.load(std::memory_order_acquire))
    {
        globalClipIndicator.store(0.0f, std::memory_order_relaxed);
        return;
    }

    if (routingRuntime != nullptr && routingRuntime->hasMultipleInstances())
    {
        if (globalBypassParam != nullptr && globalBypassParam->load(std::memory_order_relaxed) >= 0.5f)
        {
            requestLatencySamples(0);
            globalClipIndicator.store(0.0f, std::memory_order_relaxed);
            return;
        }
        routingRuntime->process(buffer, midi);
        auto clipped = false;
        for (int channel = 0; channel < juce::jmin(buffer.getNumChannels(), preparedNumChannels); ++channel)
            clipped = clipped || buffer.getMagnitude(channel, 0, buffer.getNumSamples()) >= 1.0f;
        globalClipIndicator.store(clipped ? 1.0f : 0.0f, std::memory_order_relaxed);
        updateShellLatency();
        return;
    }

    processOwnChannels(buffer);
}

unsigned AvaAudioProcessor::processOwnChannels(juce::AudioBuffer<float>& channels)
{
    const auto selected = getProcessingInputChannels();
    const auto mask = (1u << selected[0]) | (selected[1] >= 0 ? 1u << selected[1] : 0u);
    const auto samples = channels.getNumSamples();
    selectedInputBuffer.setSize(preparedNumChannels, samples, false, false, true);
    routingInputSnapshot.setSize(routingChannelCount, samples, false, false, true);
    const auto* host = routingOwner != nullptr ? routingOwner : this;
    const auto secondAvailable = selected[1] >= 0
        && host->getChannelCountOfBus(true, selected[1] / 2) > selected[1] % 2;
    for (int channel = 0; channel < preparedNumChannels; ++channel)
        selectedInputBuffer.copyFrom(channel, 0, channels,
            channel == 0 || !secondAvailable ? selected[0] : selected[1], 0, samples);
    for (int channel = 0; channel < routingChannelCount; ++channel)
        routingInputSnapshot.copyFrom(channel, 0, channels, channel, 0, samples);

    if (lastProcessingMask != 0 && mask != lastProcessingMask)
    {
        if (eqlProcessorBank != nullptr) eqlProcessorBank->resetProcessingState();
        if (tlsModuleProcessor != nullptr) tlsModuleProcessor->resetProcessingState();
        if (dynModuleProcessor != nullptr) dynModuleProcessor->resetProcessingState();
        crossoverRouter.reset();
        unselectedDelay.clear();
        unselectedDelayPosition = 0;
    }
    lastProcessingMask = mask;
    processOwnBlock(selectedInputBuffer, &routingInputSnapshot);
    channels.copyFrom(selected[0], 0, selectedInputBuffer, 0, 0, samples);
    if (selected[1] >= 0 && secondAvailable)
        channels.copyFrom(selected[1], 0, selectedInputBuffer, preparedNumChannels > 1 ? 1 : 0, 0, samples);
    else if (selected[1] < 0 && preparedNumChannels > 1)
    {
        // A mono destination receives the mono fold of the module's stereo output.
        channels.applyGain(selected[0], 0, samples, 0.5f);
        channels.addFrom(selected[0], 0, selectedInputBuffer, 1, 0, samples, 0.5f);
    }
    const auto bypassed = (globalBypassParam != nullptr && globalBypassParam->load() >= 0.5f)
        || (routingRuntime != nullptr && routingRuntime->isRootBypassed());
    delayUnselectedChannels(channels, mask, bypassed ? 0 : getActiveModuleLatencySamples());
    return mask;
}

void AvaAudioProcessor::delayUnselectedChannels(juce::AudioBuffer<float>& channels,
                                                const unsigned mask, const int latency)
{
    const auto length = unselectedDelay.getNumSamples();
    if (length == 0) return;
    const auto delay = juce::jlimit(0, length - 1, latency);
    if (delay != unselectedLatency)
    {
        unselectedDelay.clear();
        unselectedDelayPosition = 0;
        unselectedLatency = delay;
    }
    if (delay == 0) return;
    for (int sample = 0; sample < channels.getNumSamples(); ++sample)
    {
        const auto read = (unselectedDelayPosition + length - delay) % length;
        for (int channel = 0; channel < channels.getNumChannels(); ++channel)
        {
            if ((mask & (1u << channel)) != 0) continue;
            const auto input = channels.getSample(channel, sample);
            const auto output = delay > 0 ? unselectedDelay.getSample(channel, read) : input;
            unselectedDelay.setSample(channel, unselectedDelayPosition, input);
            channels.setSample(channel, sample, output);
        }
        unselectedDelayPosition = (unselectedDelayPosition + 1) % length;
    }
}

void AvaAudioProcessor::processOwnBlock(juce::AudioBuffer<float>& buffer,
                                        const juce::AudioBuffer<float>* routingInputs)
{
    const auto active = activeModule.load(std::memory_order_acquire);
    auto crossoverSettings = getCrossoverSettings();

    auto applyGlobalOutputStage = [this, &buffer]
    {
        const auto outputProcessChannels = juce::jmin(buffer.getNumChannels(), preparedNumChannels);

        if (outputProcessChannels <= 0)
        {
            globalClipIndicator.store(0.0f, std::memory_order_relaxed);
            return;
        }

        auto clipped = false;

        for (int channel = 0; channel < outputProcessChannels && ! clipped; ++channel)
            clipped = buffer.getMagnitude(channel, 0, buffer.getNumSamples()) >= 1.0f;

        globalClipIndicator.store(clipped ? 1.0f : 0.0f, std::memory_order_relaxed);
    };

    const auto globalBypassActive = globalBypassParam != nullptr
        && globalBypassParam->load(std::memory_order_relaxed) >= 0.5f;

    if (globalBypassActive || (routingRuntime != nullptr && routingRuntime->isRootBypassed()))
    {
        requestLatencySamples(0);
        globalClipIndicator.store(0.0f, std::memory_order_relaxed);
        return;
    }

    auto availableRangeCount = active == ActiveModule::none
        ? ava::crossover::BufferRouter::numRanges
        : size_t { 1 };
    ava::crossover::BufferRouter::RangeLatencies rangeLatencies {};

    switch (active)
    {
        case ActiveModule::tls:
            if (auto* processor = getTlsModuleProcessor())
            {
                processor->syncParameters();
                availableRangeCount = processor->getCreatedRangeCount();
                rangeLatencies = processor->getRangeLatencies();
            }
            break;

        case ActiveModule::dyn:
            if (auto* processor = getDynModuleProcessor())
            {
                processor->setProcessingInputChannel(juce::roundToInt(processingInputParam->load()));
                processor->syncParameters(false, true);
                availableRangeCount = processor->getCreatedRangeCount();
                rangeLatencies = processor->getRangeLatencies();
            }
            break;

        case ActiveModule::eql:
            if (auto* processor = getEqlProcessorBank())
            {
                availableRangeCount = processor->getCreatedRangeCount();
                rangeLatencies = processor->getRangeLatencies();
            }
            break;

        case ActiveModule::none:
            break;
    }

    crossoverSettings.activeSplitCount = availableRangeCount > 0
        ? std::min(crossoverSettings.activeSplitCount, availableRangeCount - 1)
        : 0;
    crossoverRouter.setSettings(crossoverSettings);
    crossoverRouter.setRangeLatencies(rangeLatencies);
    updateShellLatency();

    crossoverRouter.process(buffer, [this, active, routingInputs] (const size_t rangeIndex,
                                                   juce::AudioBuffer<float>& rangeBuffer, const int blockStart)
    {
        switch (active)
        {
            case ActiveModule::eql:
                if (auto* processor = getEqlProcessorBank())
                    processor->processRange(rangeIndex, rangeBuffer);
                break;

            case ActiveModule::tls:
                if (auto* processor = getTlsModuleProcessor())
                    processor->processRange(rangeIndex, rangeBuffer);
                break;

            case ActiveModule::dyn:
                if (auto* processor = getDynModuleProcessor())
                    processor->processRange(rangeIndex, rangeBuffer, routingInputs, blockStart);
                break;

            case ActiveModule::none:
                break;
        }
    });

    applyGlobalOutputStage();
    updateShellLatency();
}
