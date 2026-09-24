#include "Processor.h"
#include "../modules/eql/ProcessorBank.h"
#include "../modules/fft/ProcessorBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/fft/Processor.h"
#include "../modules/trs/Processor.h"
#include "../routing/Runtime.h"

#include <cmath>

void AvaAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    processingPrepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(processingLock);

    currentSampleRate = sampleRate;
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    preparedNumChannels = juce::jlimit(1, static_cast<int>(maxSupportedChannels), getTotalNumOutputChannels());
    const auto maximumRangeLatencySamples = static_cast<int>(std::ceil(sampleRate * 0.25)) + 16384;
    crossoverRouter.prepare(sampleRate,
                            preparedBlockSize,
                            preparedNumChannels,
                            maximumRangeLatencySamples);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->prepareToPlay(sampleRate, preparedBlockSize);
    if (fftProcessorBank != nullptr)
        fftProcessorBank->prepareToPlay(sampleRate, preparedBlockSize);
    if (tlsModuleProcessor != nullptr)
        tlsModuleProcessor->prepareToPlay(sampleRate, preparedBlockSize);
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->prepareToPlay(sampleRate, preparedBlockSize);
    if (trsModuleProcessor != nullptr)
        trsModuleProcessor->prepareToPlay(sampleRate, preparedBlockSize);

    if (routingRuntime != nullptr)
        routingRuntime->prepare(sampleRate, preparedBlockSize, preparedNumChannels);

    updateShellLatency();
    processingPrepared.store(true, std::memory_order_release);
}

void AvaAudioProcessor::releaseResources()
{
    processingPrepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(processingLock);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->releaseResources();
    if (fftProcessorBank != nullptr)
        fftProcessorBank->releaseResources();
    if (tlsModuleProcessor != nullptr)
        tlsModuleProcessor->releaseResources();
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->releaseResources();
    if (trsModuleProcessor != nullptr)
        trsModuleProcessor->releaseResources();

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
    if (fftProcessorBank != nullptr)
        fftProcessorBank->resetProcessingState();
    if (tlsModuleProcessor != nullptr)
        tlsModuleProcessor->resetProcessingState();
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->resetProcessingState();
    if (trsModuleProcessor != nullptr)
        trsModuleProcessor->resetProcessingState();

    if (routingRuntime != nullptr)
        routingRuntime->reset();

    crossoverRouter.reset();

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
        case ActiveModule::fft:
            if (const auto* processor = getFftProcessorBank())
                return maxActiveLatency(*processor);
            break;

        case ActiveModule::tls:
            if (const auto* processor = getTlsModuleProcessor())
                return maxActiveLatency(*processor);
            break;

        case ActiveModule::dyn:
            if (const auto* processor = getDynModuleProcessor())
                return maxActiveLatency(*processor);
            break;

        case ActiveModule::trs:
            if (const auto* processor = getTrsModuleProcessor())
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

    return mainInput == juce::AudioChannelSet::mono()
        || mainInput == juce::AudioChannelSet::stereo();
}

void AvaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

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
        routingRuntime->process(buffer, midi);
        auto clipped = false;
        for (int channel = 0; channel < juce::jmin(buffer.getNumChannels(), preparedNumChannels); ++channel)
            clipped = clipped || buffer.getMagnitude(channel, 0, buffer.getNumSamples()) >= 1.0f;
        globalClipIndicator.store(clipped ? 1.0f : 0.0f, std::memory_order_relaxed);
        updateShellLatency();
        return;
    }

    processOwnBlock(buffer);
}

void AvaAudioProcessor::processOwnBlock(juce::AudioBuffer<float>& buffer)
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

    const auto readGlobalListen = [this] (const size_t index) noexcept
    {
        if (const auto* value = globalListenParams[index])
            return value->load(std::memory_order_relaxed) >= 0.5f;

        return false;
    };

    const auto applyGlobalListen = [&buffer,
                                    listenLc = readGlobalListen(0),
                                    listenRc = readGlobalListen(1),
                                    listenMc = readGlobalListen(2),
                                    listenSc = readGlobalListen(3),
                                    listenLl = readGlobalListen(4),
                                    listenRr = readGlobalListen(5),
                                    listenSs = readGlobalListen(6)]
    {
        if (! (listenLc || listenRc || listenMc || listenSc || listenLl || listenRr || listenSs)
            || buffer.getNumChannels() < 2)
            return;

        for (int sampleIndex = 0; sampleIndex < buffer.getNumSamples(); ++sampleIndex)
        {
            const auto left = buffer.getSample(0, sampleIndex);
            const auto right = buffer.getSample(1, sampleIndex);
            const auto mid = 0.5f * (left + right);
            const auto side = 0.5f * (left - right);

            if (listenLc)
                buffer.setSample(1, sampleIndex, left);
            else if (listenRc)
                buffer.setSample(0, sampleIndex, right);
            else if (listenMc)
            {
                buffer.setSample(0, sampleIndex, mid);
                buffer.setSample(1, sampleIndex, mid);
            }
            else if (listenSc)
            {
                buffer.setSample(0, sampleIndex, side);
                buffer.setSample(1, sampleIndex, side);
            }
            else if (listenLl)
                buffer.setSample(1, sampleIndex, 0.0f);
            else if (listenRr)
                buffer.setSample(0, sampleIndex, 0.0f);
            else if (listenSs)
            {
                buffer.setSample(0, sampleIndex, side);
                buffer.setSample(1, sampleIndex, -side);
            }
        }
    };

    const auto globalBypassActive = globalBypassParam != nullptr
        && globalBypassParam->load(std::memory_order_relaxed) >= 0.5f;

    if (globalBypassActive)
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
        case ActiveModule::fft:
            if (auto* processor = getFftProcessorBank())
            {
                processor->refreshLatencyState();
                availableRangeCount = processor->getCreatedRangeCount();
                rangeLatencies = processor->getRangeLatencies();
            }
            break;

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
                processor->syncParameters();
                availableRangeCount = processor->getCreatedRangeCount();
                rangeLatencies = processor->getRangeLatencies();
            }
            break;

        case ActiveModule::trs:
            if (auto* processor = getTrsModuleProcessor())
            {
                processor->refreshLatencyState();
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

    crossoverRouter.process(buffer, [this, active] (const size_t rangeIndex,
                                                   juce::AudioBuffer<float>& rangeBuffer)
    {
        switch (active)
        {
            case ActiveModule::eql:
                if (auto* processor = getEqlProcessorBank())
                    processor->processRange(rangeIndex, rangeBuffer);
                break;

            case ActiveModule::fft:
                if (auto* processor = getFftProcessorBank())
                    processor->processRange(rangeIndex, rangeBuffer);
                break;

            case ActiveModule::tls:
                if (auto* processor = getTlsModuleProcessor())
                    processor->processRange(rangeIndex, rangeBuffer);
                break;

            case ActiveModule::dyn:
                if (auto* processor = getDynModuleProcessor())
                    processor->processRange(rangeIndex, rangeBuffer);
                break;

            case ActiveModule::trs:
                if (auto* processor = getTrsModuleProcessor())
                    processor->processRange(rangeIndex, rangeBuffer);
                break;

            case ActiveModule::none:
                break;
        }
    });

    applyGlobalListen();
    applyGlobalOutputStage();
    updateShellLatency();
}
