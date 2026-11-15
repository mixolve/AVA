#include "Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../routing/Runtime.h"

#include <cmath>

void AvaAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    processingPrepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(processingLock);
    const auto wasRestoring = restoringModel.exchange(true);
    const juce::ScopeGuard preparedModel {[this, wasRestoring] { restoringModel.store(wasRestoring); }};

    currentSampleRate = sampleRate;
    resetRenderTransition();
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    preparedNumChannels = static_cast<int>(maxSupportedChannels);
    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->prepareToPlay(sampleRate, preparedBlockSize);

    if (blockChainModuleProcessor != nullptr)
        blockChainModuleProcessor->prepareToPlay(sampleRate, preparedBlockSize);
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->prepareToPlay(sampleRate, preparedBlockSize);
    for (auto& [uid, copy] : moduleCopies) {
        juce::ignoreUnused(uid);
        if (copy->blocks) copy->blocks->prepareToPlay(sampleRate, preparedBlockSize);
        if (copy->dyn) copy->dyn->prepareToPlay(sampleRate, preparedBlockSize);
        if (copy->eql) copy->eql->prepareToPlay(sampleRate, preparedBlockSize);
    }

    if (getRenderRouting() != nullptr)
        getRenderRouting()->prepare(sampleRate, preparedBlockSize, routingChannelCount);

    publishRenderGraph(true);
    // REAPER may replace/restore render-side DSP objects when changing
    // between offline and realtime operation. Re-resolve the saved slot IDs
    // against the current processors before processing resumes.
    if (!routingInstance)
    {
        queueCurrentHostSlotValues();
        refreshHostSlotTargets();
    }
    updateShellLatency();
    processingPrepared.store(true, std::memory_order_release);
}

void AvaAudioProcessor::releaseResources()
{
    processingPrepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(processingLock);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->releaseResources();

    if (blockChainModuleProcessor != nullptr)
        blockChainModuleProcessor->releaseResources();
    if (dynModuleProcessor != nullptr)
        dynModuleProcessor->releaseResources();
    for (auto& [uid, copy] : moduleCopies) {
        juce::ignoreUnused(uid);
        if (copy->blocks) copy->blocks->releaseResources();
        if (copy->dyn) copy->dyn->releaseResources();
        if (copy->eql) copy->eql->releaseResources();
    }

    if (getRenderRouting() != nullptr)
        getRenderRouting()->release();

    if (const RenderingScope scope(*this); scope) audioBuffers().crossoverRouter.reset();

    resetRenderTransition();
    requestLatencySamples(0);
    currentSampleRate = 0.0;
}

void AvaAudioProcessor::reset()
{
    const RenderingScope scope(*this);
    if (!scope) return;

    if (auto* processor = getEqlProcessorBank())
        processor->resetProcessingState();

    if (auto* processor = getBlockChainProcessor())
        processor->resetProcessingState();
    if (auto* processor = getDynModuleProcessor())
        processor->resetProcessingState();
    if (renderingGraph != nullptr)
        for (const auto& [uid, copy] : renderingGraph->moduleCopies) {
            juce::ignoreUnused(uid);
            if (copy->blocks) copy->blocks->resetProcessingState();
            if (copy->dyn) copy->dyn->resetProcessingState();
            if (copy->eql) copy->eql->resetProcessingState();
        }

    if (getRenderRouting() != nullptr)
        getRenderRouting()->reset();

    audioBuffers().crossoverRouter.reset();
    audioBuffers().unselectedDelay.clear();
    audioBuffers().unselectedDelayPosition = 0;
    audioBuffers().lastProcessingMask = 0;

}

ava::crossover::BufferRouter::RangeLatencies AvaAudioProcessor::getModuleChainRangeLatencies(unsigned bypassMask) const noexcept
{
    ava::crossover::BufferRouter::RangeLatencies result {};
    if (bypassMask == ~0u) bypassMask = getModuleBypassMask();
    const auto& stacks = renderingOwner == this && renderingGraph != nullptr ? renderingGraph->moduleStacks : moduleStacks;
    const auto& copies = renderingOwner == this && renderingGraph != nullptr ? renderingGraph->moduleCopies : moduleCopies;
    for(size_t band=0;band<stacks.size();++band)
        for(const auto& slot:stacks[band]) {
            if(!slot.enabled)continue;
            const auto id=slot.type;
            const bool original=slot.uid<ava::module_stack::originalCount;
            if(module_order::isChain(id) && (bypassMask & 1u)==0) {
                const auto* chain=original ? getBlockChainProcessor()
                    : (copies.contains(slot.uid) ? copies.at(slot.uid)->blocks.get() : nullptr);
                if(chain!=nullptr)
                    result[band]+=chain->getBlockRangeLatency(band,ava::modules::chain::blocks::Id(module_order::chainIndex(id)));
            } else if(id==module_order::eql && (bypassMask & 2u)==0
                && (!original || !isModuleHiddenForRange(band,id))) {
                const auto* eql=original?getEqlProcessorBank()
                    : (copies.contains(slot.uid)?copies.at(slot.uid)->eql.get():nullptr);
                if(eql!=nullptr && band<eql->getCreatedRangeCount())result[band]+=eql->getRangeLatencies()[band];
            } else if(id==module_order::dyn && (bypassMask & 4u)==0
                && (!original || !isModuleHiddenForRange(band,id))) {
                const auto* dyn=original?getDynModuleProcessor()
                    : (copies.contains(slot.uid)?copies.at(slot.uid)->dyn.get():nullptr);
                if(dyn!=nullptr && band<dyn->getCreatedRangeCount())result[band]+=dyn->getRangeLatencies()[band];
            }
        }
    return result;
}

int AvaAudioProcessor::getModuleChainLatencySamples() const noexcept
{
    const auto latencies = getModuleChainRangeLatencies();
    const auto ranges = getCrossoverSettings().activeSplitCount + 1;
    return *std::max_element(latencies.begin(), latencies.begin() + static_cast<std::ptrdiff_t>(ranges));
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
    // Prepared model changes are accepted by render at its zero-level switch.
    if (processingPrepared.load(std::memory_order_acquire) && renderingOwner != this) return;
    auto totalLatencySamples = getRenderRouting() != nullptr && getRenderRouting()->hasMultipleInstances()
        ? getRenderRouting()->getLatencySamples() : getModuleChainLatencySamples();

    if (isGlobalBypassed()) totalLatencySamples = 0;
    else if ((getRenderRouting() == nullptr || !getRenderRouting()->hasMultipleInstances()) && isInstanceBypassed()) totalLatencySamples = 0;

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
    const auto choice = renderingOwner == this && renderingGraph != nullptr && renderingGraph->buffers != nullptr
        ? audioBuffers().inputChoice : processingInputParam != nullptr ? juce::roundToInt(processingInputParam->load()) : 0;
    return ava::channels::decode(choice);
}

void AvaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    if (!processingPrepared.load(std::memory_order_acquire)) return;
    // Read the actual host parameters even if REAPER's offline-render worker
    // did not dispatch the normal APVTS callback or a GUI AsyncUpdater. This
    // must happen before any DSP processing in this block.
    if (!routingInstance)
    {
        pollHostSlotValuesFromAudioCallback();
        if (hostSlotAutomationPending.load(std::memory_order_acquire))
            syncHostSlotValues(true);
    }
    auto output = getBusBuffer(buffer, false, 0);
    for (int offset = 0; offset < buffer.getNumSamples(); offset += preparedBlockSize) {
        const RenderingScope scope(*this);
        if (!scope) return;
        const auto samples = juce::jmin(audioBuffers().blockSize, buffer.getNumSamples() - offset);
        auto& channels = audioBuffers().routingChannels;
        channels.setSize(routingChannelCount, samples, false, false, true);
        channels.clear();
        for (int bus = 0; bus < getBusCount(true); ++bus) {
            auto input = getBusBuffer(buffer, true, bus);
            for (int channel = 0; channel < input.getNumChannels(); ++channel)
                channels.copyFrom(bus * 2 + channel, 0, input, channel, offset, samples);
        }
        processChannelBlock(channels, midi);
        auto* runtime = getRenderRouting();
        auto* monitor = runtime != nullptr ? runtime->getSoloProcessor(true)
            : soloChannelParam != nullptr && soloChannelParam->load() >= 0.5f ? this : nullptr;
        if (isGlobalBypassed()) monitor = nullptr;
        const auto selected = monitor != nullptr ? monitor->getProcessingInputChannels() : std::array<int, 2> {0, 1};
        const auto right = selected[1] >= 0 && getChannelCountOfBus(true, selected[1] / 2) > selected[1] % 2 ? selected[1] : selected[0];
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.copyFrom(channel, offset, channels, channel == 0 ? selected[0] : right, 0, samples);
        if (monitor != nullptr && output.getNumChannels() == 1 && right != selected[0]) {
            output.applyGain(0, offset, samples, 0.5f);
            output.addFrom(0, offset, channels, right, 0, samples, 0.5f);
        }
    }

}

void AvaAudioProcessor::processChannelBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const RenderingScope scope(*this);
    if (!scope) return;

    if (! processingPrepared.load(std::memory_order_acquire)) return;
    const juce::ScopeGuard transition {[this, &buffer] {
        if (!routingInstance) applyRenderTransition(buffer);
    }};
    if (isGlobalBypassed()) {
        requestLatencySamples(0);
        // Global bypass keeps the input dry. Compensate its level only here,
        // before the early return, so this gain cannot affect wet processing.
        if (!routingInstance && bypassCompensationParam != nullptr)
        {
            const auto gainDb = juce::jlimit(-96.0f, 96.0f,
                bypassCompensationParam->load(std::memory_order_relaxed));
            if (gainDb != 0.0f)
            {
                const auto gain = juce::Decibels::decibelsToGain(gainDb);
                for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
                    buffer.applyGain(channel, 0, buffer.getNumSamples(), gain);
            }
        }
        if (routingInstance && audioBuffers().instanceMuted)
            for (const auto channel : getProcessingInputChannels())
                if (juce::isPositiveAndBelow(channel, buffer.getNumChannels())) buffer.clear(channel, 0, buffer.getNumSamples());
        return;
    }

    if (getRenderRouting() != nullptr && getRenderRouting()->hasMultipleInstances())
    {
        getRenderRouting()->process(buffer, midi);
        recordClippedBlock(buffer);

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
    audioBuffers().selectedInputBuffer.setSize(preparedNumChannels, samples, false, false, true);
    audioBuffers().routingInputSnapshot.setSize(routingChannelCount, samples, false, false, true);
    const auto* host = routingOwner != nullptr ? routingOwner : this;
    const auto secondAvailable = selected[1] >= 0
        && host->getChannelCountOfBus(true, selected[1] / 2) > selected[1] % 2;
    for (int channel = 0; channel < preparedNumChannels; ++channel)
        audioBuffers().selectedInputBuffer.copyFrom(channel, 0, channels,
            channel == 0 || !secondAvailable ? selected[0] : selected[1], 0, samples);
    for (int channel = 0; channel < routingChannelCount; ++channel)
        audioBuffers().routingInputSnapshot.copyFrom(channel, 0, channels, channel, 0, samples);

    if (audioBuffers().lastProcessingMask != 0 && mask != audioBuffers().lastProcessingMask)
    {
        if (auto* processor = getEqlProcessorBank()) processor->resetProcessingState();
        if (auto* processor = getBlockChainProcessor()) processor->resetProcessingState();
        if (auto* processor = getDynModuleProcessor()) processor->resetProcessingState();
        audioBuffers().crossoverRouter.reset();
        audioBuffers().unselectedDelay.clear();
        audioBuffers().unselectedDelayPosition = 0;
    }
    audioBuffers().lastProcessingMask = mask;
    processOwnBlock(audioBuffers().selectedInputBuffer, &audioBuffers().routingInputSnapshot);
    if (audioBuffers().instanceMuted) audioBuffers().selectedInputBuffer.clear();
    channels.copyFrom(selected[0], 0, audioBuffers().selectedInputBuffer, 0, 0, samples);
    if (selected[1] >= 0 && secondAvailable)
        channels.copyFrom(selected[1], 0, audioBuffers().selectedInputBuffer, preparedNumChannels > 1 ? 1 : 0, 0, samples);
    else if (selected[1] < 0 && preparedNumChannels > 1)
    {
        // A mono destination receives the mono fold of the module's stereo output.
        channels.applyGain(selected[0], 0, samples, 0.5f);
        channels.addFrom(selected[0], 0, audioBuffers().selectedInputBuffer, 1, 0, samples, 0.5f);
    }
    delayUnselectedChannels(channels, mask, isInstanceBypassed() ? 0 : getModuleChainLatencySamples());
    return mask;
}

void AvaAudioProcessor::delayUnselectedChannels(juce::AudioBuffer<float>& channels,
                                                const unsigned mask, const int latency)
{
    const auto length = audioBuffers().unselectedDelay.getNumSamples();
    if (length == 0) return;
    const auto delay = juce::jlimit(0, length - 1, latency);
    if (delay != audioBuffers().unselectedLatency)
    {
        audioBuffers().unselectedDelay.clear();
        audioBuffers().unselectedDelayPosition = 0;
        audioBuffers().unselectedLatency = delay;
    }
    if (delay == 0) return;
    for (int sample = 0; sample < channels.getNumSamples(); ++sample)
    {
        const auto read = (audioBuffers().unselectedDelayPosition + length - delay) % length;
        for (int channel = 0; channel < channels.getNumChannels(); ++channel)
        {
            if ((mask & (1u << channel)) != 0) continue;
            const auto input = channels.getSample(channel, sample);
            const auto output = audioBuffers().unselectedDelay.getSample(channel, read);
            audioBuffers().unselectedDelay.setSample(channel, audioBuffers().unselectedDelayPosition, input);
            channels.setSample(channel, sample, output);
        }
        audioBuffers().unselectedDelayPosition = (audioBuffers().unselectedDelayPosition + 1) % length;
    }
}

void AvaAudioProcessor::processOwnBlock(juce::AudioBuffer<float>& buffer,
                                        const juce::AudioBuffer<float>* routingInputs)
{
    auto crossoverSettings = getCrossoverSettings();

    if (isInstanceBypassed()) {
        requestLatencySamples(0);
        return;
    }
    const auto bypassMask = getModuleBypassMask();
    const auto changed = bypassMask ^ audioBuffers().lastModuleBypassMask;
    if ((changed & 1u) != 0)
        if (auto* processor = getBlockChainProcessor()) processor->resetProcessingState();
    if ((changed & 2u) != 0)
        if (auto* processor = getEqlProcessorBank()) processor->resetProcessingState();
    if ((changed & 4u) != 0)
        if (auto* processor = getDynModuleProcessor()) processor->resetProcessingState();
    audioBuffers().lastModuleBypassMask = bypassMask;
    syncAudioModuleParameters();

    auto availableRangeCount = ava::crossover::BufferRouter::numRanges;
    if (auto* processor = getBlockChainProcessor())
    {
        availableRangeCount = std::min(availableRangeCount, processor->getCreatedRangeCount());
    }
    if (auto* processor = getDynModuleProcessor())
    {
        availableRangeCount = std::min(availableRangeCount, processor->getCreatedRangeCount());
    }
    if (auto* processor = getEqlProcessorBank()) {
        availableRangeCount = std::min(availableRangeCount, processor->getCreatedRangeCount());
    }

    crossoverSettings.activeSplitCount = availableRangeCount > 0
        ? std::min(crossoverSettings.activeSplitCount, availableRangeCount - 1)
        : 0;
    audioBuffers().crossoverRouter.setSettings(crossoverSettings);
    audioBuffers().crossoverRouter.setRangeLatencies(getModuleChainRangeLatencies(bypassMask));
    updateShellLatency();

    audioBuffers().crossoverRouter.process(buffer, [this, routingInputs, bypassMask] (const size_t rangeIndex,
                                                   juce::AudioBuffer<float>& rangeBuffer, const int blockStart)
    {
        // Each crossover BAND has its own chain: the selected editor band does
        // not alter processing order for any other range.
        const auto& stacks = renderingGraph->moduleStacks;
        for (const auto& slot : stacks[rangeIndex])
        {
            if (!slot.enabled) continue;
            const auto id = slot.type;
            if (slot.uid < ava::module_stack::originalCount)
            {
                if (module_order::isChain(id))
                {
                    if ((bypassMask & 1u) == 0)
                        if (auto* processor = getBlockChainProcessor())
                            processor->processBlockRange(rangeIndex, ava::modules::chain::blocks::Id(module_order::chainIndex(id)), rangeBuffer);
                }
                else if (id == module_order::dyn && (bypassMask & 4u) == 0
                         && !isModuleHiddenForRange(rangeIndex, id))
                {
                    if (auto* processor = getDynModuleProcessor()) processor->processRange(rangeIndex, rangeBuffer, routingInputs, blockStart);
                }
                else if (id == module_order::eql && (bypassMask & 2u) == 0
                         && !isModuleHiddenForRange(rangeIndex, id))
                {
                    if (auto* processor = getEqlProcessorBank()) processor->processRange(rangeIndex, rangeBuffer);
                }
                continue;
            }
            if (const auto found = renderingGraph->moduleCopies.find(slot.uid);
                found != renderingGraph->moduleCopies.end())
            {
                const auto& copy = *found->second;
                if (module_order::isChain(id) && (bypassMask & 1u) == 0 && copy.blocks != nullptr)
                    copy.blocks->processBlockRange(rangeIndex, ava::modules::chain::blocks::Id(module_order::chainIndex(id)), rangeBuffer);
                else if (id == module_order::dyn && (bypassMask & 4u) == 0 && copy.dyn != nullptr)
                    copy.dyn->processRange(rangeIndex, rangeBuffer, routingInputs, blockStart);
                else if (id == module_order::eql && (bypassMask & 2u) == 0 && copy.eql != nullptr)
                    copy.eql->processRange(rangeIndex, rangeBuffer);
            }
        }
    });

    recordClippedBlock(buffer);
    updateShellLatency();
}

void AvaAudioProcessor::recordClippedBlock(const juce::AudioBuffer<float>& buffer) noexcept
{
    for (int channel = 0; channel < juce::jmin(buffer.getNumChannels(), preparedNumChannels); ++channel)
        if (buffer.getMagnitude(channel, 0, buffer.getNumSamples()) >= 1.0f)
        {
            globalClipEventCount.fetch_add(1, std::memory_order_relaxed);
            return;
        }
}
