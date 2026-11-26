#include "Runtime.h"
#include "../shell/Processor.h"
#include "../modules/dyn/Processor.h"

#include <algorithm>
#include <set>

namespace ava::routing
{
Runtime::Runtime(AvaAudioProcessor& ownerIn, const Runtime* previous) : owner(ownerIn)
{
    if (previous != nullptr)
        for (const auto& [id, instance] : previous->instances) {
            Instance next;
            next.processor = instance.processor;
            instances.emplace(id, std::move(next));
        }
}
Runtime::~Runtime() = default;

const State::Node* Runtime::findNode(const int id) const noexcept
{
    return findStateNode(topology.nodes, id);
}

void Runtime::prepareBuffers(Instance& instance)
{
    if (preparedChannels <= 0 || preparedBlockSize <= 0)
        return;
    instance.input.setSize(2 * preparedChannels, preparedBlockSize);
    instance.work.setSize(2 * preparedChannels, preparedBlockSize);
    instance.input.clear();
    instance.work.clear();
}

void Runtime::synchronize(const juce::ValueTree& state)
{
    owner.invalidateHostSlotTargets();
    const auto nextTopology = readState(state);
    std::set<int> required;
    for (const auto& node : nextTopology.nodes)
        if (node.id != nextTopology.rootInstanceId)
            required.insert(node.id);

    for (auto it = instances.begin(); it != instances.end();)
    {
        const auto* next = findStateNode(nextTopology.nodes, it->first);
        if (required.contains(it->first) && (it->second.processor != nullptr) == !next->container)
            ++it;
        else
            it = instances.erase(it);
    }
    for (auto it = delays.begin(); it != delays.end();)
        if (std::any_of(nextTopology.nodes.begin(), nextTopology.nodes.end(),
                        [id = it->first] (const auto& node) { return node.id == id; }))
            ++it;
        else
            it = delays.erase(it);

    for (const auto id : required)
    {
        if (instances.contains(id))
            continue;
        Instance instance;
        const auto* node = findStateNode(nextTopology.nodes, id);
        if (! node->container)
        {
            instance.processor = std::make_shared<AvaAudioProcessor>(true);
            instance.processor->routingOwner = &owner;
            instance.processor->routingNodeId = id;
            instance.processor->parentStateChanged = [parent = &owner]
            {
                parent->notifyHostOfStateChange();
                parent->triggerAsyncUpdate();
            };
            instance.processor->setPlayHead(owner.getPlayHead());
            instance.processor->setNonRealtime(owner.isNonRealtime());
            instance.processor->setBusesLayout(owner.getBusesLayout());
        }
        prepareBuffers(instance);
        if (instance.processor != nullptr && preparedSampleRate > 0.0)
            instance.processor->prepareToPlay(preparedSampleRate, preparedBlockSize);
        instances.emplace(id, std::move(instance));
    }

    topology = nextTopology;
    rootBypassed.store(topology.rootBypassed);
    refreshDelayCapacity();
}

void Runtime::prepare(const double sampleRate, const int samplesPerBlock, const int channels)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize = juce::jmax(1, samplesPerBlock);
    preparedChannels = juce::jlimit(1, AvaAudioProcessor::routingChannelCount, channels);
    rootInput.setSize(2 * preparedChannels, preparedBlockSize);
    rootWork.setSize(2 * preparedChannels, preparedBlockSize);
    rootInput.clear();
    rootWork.clear();
    for (auto& [id, instance] : instances)
    {
        juce::ignoreUnused(id);
        if (instance.processor != nullptr)
        {
            instance.processor->setPlayHead(owner.getPlayHead());
            instance.processor->setNonRealtime(owner.isNonRealtime());
            instance.processor->setBusesLayout(owner.getBusesLayout());
            if (!juce::approximatelyEqual(instance.processor->currentSampleRate, sampleRate) || instance.processor->preparedBlockSize != preparedBlockSize)
                instance.processor->prepareToPlay(sampleRate, preparedBlockSize);
        }
        if (instance.processor != nullptr) instance.render = instance.processor->publishedRenderGraph;
        prepareBuffers(instance);
    }
    refreshDelayCapacity();
}

void Runtime::release()
{
    for (auto& [id, instance] : instances)
    {
        juce::ignoreUnused(id);
        if (instance.processor != nullptr) instance.processor->releaseResources();
    }
    preparedSampleRate = 0.0;
}

void Runtime::reset()
{
    rootInput.clear();
    rootWork.clear();
    for (auto& [id, delay] : delays)
    {
        juce::ignoreUnused(id);
        delay.samples.clear();
        delay.writePosition = 0;
    }
    for (auto& [id, instance] : instances)
    {
        juce::ignoreUnused(id);
        if (instance.processor != nullptr) instance.processor->reset();
        instance.input.clear();
        instance.work.clear();
    }
}

std::shared_ptr<AvaAudioProcessor> Runtime::getInstanceHandle(const int id) noexcept
{
    const auto found = instances.find(id);
    return found == instances.end() ? nullptr : found->second.processor;
}

bool Runtime::needsUpstreamForListen(const AvaAudioProcessor& processor) const noexcept
{
    const auto found = instances.find(processor.getRoutingInstanceId());
    const auto* graph = &processor == &owner ? AvaAudioProcessor::renderingGraph
        : found != instances.end() ? found->second.render.get() : nullptr;
    if (graph == nullptr || graph->buffers == nullptr || graph->buffers->globalBypass
        || graph->buffers->instanceBypass || (processor.getModuleBypassMask() & 4u) != 0)
        return false;

    const auto& buffers = *graph->buffers;
    for (size_t band = 0; band <= buffers.settings.activeSplitCount; ++band)
        for (const auto& slot : graph->moduleStacks[band])
        {
            if (slot.type != module_order::dyn || !ava::module_stack::processingActive(slot))
                continue;
            if (slot.uid < ava::module_stack::originalCount)
            {
                const auto* dyn = processor.getDynModuleProcessor();
                if (dyn != nullptr && !processor.isModuleHiddenForRange(band, slot.type)
                    && dyn->listensToOtherInput(band, buffers.inputChoice))
                    return true;
            }
            else if (const auto copy = graph->moduleCopies.find(slot.uid); copy != graph->moduleCopies.end())
            {
                const auto* dyn = copy->second->dyn.get();
                if (dyn != nullptr && dyn->listensToOtherInput(band, buffers.inputChoice))
                    return true;
            }
        }
    return false;
}

unsigned Runtime::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    owner.syncAudioModuleParameters();
    for (auto& [id, instance] : instances) {
        juce::ignoreUnused(id);
        if (instance.processor != nullptr) {
            AvaAudioProcessor::RenderingScope scope(*instance.processor, instance.render.get());
            if (scope) { instance.processor->syncAudioModuleParameters(); instance.processor->updateShellLatency(); }
        }
    }
    planGroupDelays(topology.entryInstanceId);
    auto* solo = getSoloProcessor(false);
    const bool soloListen = solo != nullptr && needsUpstreamForListen(*solo);
    if (solo != nullptr && !soloListen)
    {
        juce::AudioBuffer<float> routing(buffer.getArrayOfWritePointers(), preparedChannels, buffer.getNumSamples());
        solo->setPlayHead(owner.getPlayHead());
        solo->setNonRealtime(owner.isNonRealtime());
        if (solo == &owner)
        {
            const auto mask = solo->processOwnChannels(routing);
            owner.writeOutputChannels(buffer, mask, solo->audioBuffers().outputMuted);
            return mask;
        }
        const auto found = instances.find(solo->getRoutingInstanceId());
        AvaAudioProcessor::RenderingScope scope(*solo, found != instances.end() ? found->second.render.get() : nullptr);
        if (!scope) return 0;
        const auto mask = solo->processChannelBlock(routing, midi);
        owner.writeOutputChannels(buffer, mask, solo->audioBuffers().outputMuted);
        return mask;
    }
    if (preparedChannels <= 0 || preparedBlockSize <= 0) return 0;
    const auto channels = 2 * preparedChannels;
    jassert(channels == buffer.getNumChannels());
    unsigned processedMask = 0;
    for (int offset = 0; offset < buffer.getNumSamples(); offset += preparedBlockSize)
    {
        const auto samples = juce::jmin(preparedBlockSize, buffer.getNumSamples() - offset);
        std::array<float*, 2 * AvaAudioProcessor::routingChannelCount> channelData {};
        for (int channel = 0; channel < channels; ++channel)
            channelData[static_cast<size_t>(channel)] = buffer.getWritePointer(channel, offset);
        juce::AudioBuffer<float> chunk(channelData.data(), channels, samples);
        SoloListen listen {chunk, soloListen ? solo->getRoutingInstanceId() : 0};
        processedMask |= processGroup(topology.entryInstanceId, chunk, chunk, midi, soloListen ? &listen : nullptr);
    }
    return processedMask;
}

unsigned Runtime::branchChannels(int id) const noexcept
{
    const auto* node = findNode(id);
    if (node == nullptr) return 0;
    const auto found = instances.find(id);
    const auto* processor = id == topology.rootInstanceId ? &owner
        : node->container || found == instances.end() ? nullptr : found->second.processor.get();
    unsigned mask = 0;
    if (processor != nullptr) {
        const auto selected = processor->getProcessingInputChannels();
        mask = (1u << selected[0]) | (selected[1] >= 0 ? 1u << selected[1] : 0u);
    }
    return mask | groupChannels(node->serialNext);
}

unsigned Runtime::groupChannels(int firstId) const noexcept
{
    unsigned mask = 0;
    for (auto* node = findNode(firstId); node != nullptr; node = findNode(node->parallelNext)) mask |= branchChannels(node->id);
    if (const auto* first = findNode(firstId)) mask |= groupChannels(first->groupNext);
    return mask;
}

unsigned Runtime::processGroup(const int firstId,
                               juce::AudioBuffer<float>& input,
                               juce::AudioBuffer<float>& output,
                               juce::MidiBuffer& midi,
                               SoloListen* soloListen)
{
    const auto* first = findNode(firstId);
    const auto scratch = instances.find(firstId);
    if (first == nullptr) return 0;

    auto& groupInputStorage = firstId == topology.rootInstanceId ? rootInput : scratch->second.input;
    const auto channels = input.getNumChannels();
    const auto samples = input.getNumSamples();
    juce::AudioBuffer<float> groupInput(groupInputStorage.getArrayOfWritePointers(), channels, samples);
    for (int channel = 0; channel < channels; ++channel)
        groupInput.copyFrom(channel, 0, input, channel, 0, samples);

    unsigned processedMask = 0;
    const auto maximumLatency = parallelLatency(firstId);
    int branchCount = 0;
    for (auto* node = first; node != nullptr; node = findNode(node->parallelNext)) ++branchCount;
    const auto inputGain = first->compensated ? 1.0f / float(juce::jmax(1, branchCount)) : 1.0f;
    for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
    {
        const auto found = instances.find(node->id);
        auto& workStorage = node->id == topology.rootInstanceId ? rootWork : found->second.work;
        juce::AudioBuffer<float> work(workStorage.getArrayOfWritePointers(), channels, samples);
        for (int channel = 0; channel < channels; ++channel)
            work.copyFrom(channel, 0, groupInput, channel, 0, samples);

        if (inputGain != 1.0f) {
            const auto mask = branchChannels(node->id);
            for (int channel = 0; channel < preparedChannels; ++channel)
                if ((mask & (1u << channel)) != 0) work.applyGain(channel, 0, samples, inputGain);
        }
        // The first half carries the full signal for DSP and sidechain readers.
        // The second half tracks only what each branch contributes to the output.
        juce::AudioBuffer<float> routing(work.getArrayOfWritePointers(), preparedChannels, samples);
        unsigned branchMask = 0;
        if (node->id == topology.rootInstanceId)
        {
            branchMask = owner.processOwnChannels(routing);
            owner.writeOutputChannels(work, branchMask, owner.audioBuffers().outputMuted);
        }
        else if (!node->container)
        {
            auto& processor = *found->second.processor;
            processor.setPlayHead(owner.getPlayHead());
            processor.setNonRealtime(owner.isNonRealtime());
            AvaAudioProcessor::RenderingScope scope(processor, found->second.render.get());
            if (scope)
            {
                branchMask = processor.processChannelBlock(routing, midi);
                owner.writeOutputChannels(work, branchMask, processor.audioBuffers().outputMuted);
            }
        }
        if (soloListen != nullptr && node->id == soloListen->instanceId)
        {
            for (int channel = 0; channel < channels; ++channel)
                soloListen->output.copyFrom(channel, 0, work, channel, 0, samples);
            soloListen->captured = true;
            return branchMask;
        }
        if (node->serialNext != 0)
        {
            const auto nextMask = processGroup(node->serialNext, work, work, midi, soloListen);
            if (soloListen != nullptr && soloListen->captured) return nextMask;
            branchMask |= nextMask;
        }

        const auto branchLatency = nodeLatency(node->id) + groupLatency(node->serialNext);
        if (maximumLatency > branchLatency) applyDelay(node->id, work);

        // Only branches processing a channel contribute to its parallel sum.
        // Untouched channels pass once, rather than being duplicated per branch.
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto bit = 1u << (channel % preparedChannels);
            if (node == first || ((branchMask & bit) != 0 && (processedMask & bit) == 0))
                output.copyFrom(channel, 0, work, channel, 0, samples);
            else if ((branchMask & bit) != 0)
                output.addFrom(channel, 0, work, channel, 0, samples);
        }
        processedMask |= branchMask;
    }
    if (first->groupNext != 0)
    {
        const auto nextMask = processGroup(first->groupNext, output, output, midi, soloListen);
        if (soloListen != nullptr && soloListen->captured) return nextMask;
        processedMask |= nextMask;
    }
    return processedMask;
}

int Runtime::parallelLatency(const int firstId) const noexcept
{
    auto result = 0;
    for (auto* node = findNode(firstId); node != nullptr; node = findNode(node->parallelNext))
    {
        result = juce::jmax(result, nodeLatency(node->id) + groupLatency(node->serialNext));
    }
    return result;
}

int Runtime::groupLatency(const int firstId) const noexcept
{
    const auto* first = findNode(firstId);
    if (first == nullptr)
        return 0;
    return parallelLatency(firstId) + groupLatency(first->groupNext);
}

int Runtime::nodeLatency(const int id) const noexcept
{
    if (id == topology.rootInstanceId)
        return owner.isInstanceBypassed() || owner.isGlobalBypassed() ? 0 : owner.getModuleChainLatencySamples();
    const auto found = instances.find(id);
    return found == instances.end() || found->second.processor == nullptr ? 0
        : found->second.processor->requestedLatencySamples.load(std::memory_order_acquire);
}

void Runtime::planGroupDelays(const int firstId)
{
    const auto maximumLatency = parallelLatency(firstId);
    for (auto* node = findNode(firstId); node != nullptr; node = findNode(node->parallelNext))
    {
        const auto branchLatency = nodeLatency(node->id) + groupLatency(node->serialNext);
        const auto needed = juce::jmax(0, maximumLatency - branchLatency);
        auto& delay = delays[node->id];
        jassert(needed < delay.samples.getNumSamples());
        if (delay.delaySamples != needed) {
            delay.delaySamples = needed;
            delay.writePosition = 0;
            delay.filled = 0;
        }
        if (node->serialNext != 0)
            planGroupDelays(node->serialNext);
    }
    if (const auto* first = findNode(firstId); first != nullptr && first->groupNext != 0)
        planGroupDelays(first->groupNext);
}

void Runtime::refreshDelayCapacity()
{
    if (preparedChannels > 0)
        prepareGroupDelays(topology.entryInstanceId);
}

int Runtime::maximumNodeLatency(int id) const noexcept
{
    if (id == topology.rootInstanceId) return owner.getMaximumChainLatencySamples();
    const auto found = instances.find(id);
    return found != instances.end() && found->second.render != nullptr ? found->second.render->maximumLatency : 0;
}
int Runtime::maximumParallelLatency(int firstId) const noexcept
{
    int result = 0;
    for (auto* node = findNode(firstId); node != nullptr; node = findNode(node->parallelNext))
        result = juce::jmax(result, maximumNodeLatency(node->id) + maximumGroupLatency(node->serialNext));
    return result;
}
int Runtime::maximumGroupLatency(int firstId) const noexcept
{
    const auto* first = findNode(firstId);
    return first != nullptr ? maximumParallelLatency(firstId) + maximumGroupLatency(first->groupNext) : 0;
}
void Runtime::prepareGroupDelays(int firstId)
{
    const auto capacity = maximumParallelLatency(firstId);
    for (auto* node = findNode(firstId); node != nullptr; node = findNode(node->parallelNext)) {
        auto& delay = delays[node->id];
        if (delay.samples.getNumSamples() < capacity + 1 || delay.samples.getNumChannels() != 2 * preparedChannels) {
            delay.samples.setSize(2 * preparedChannels, capacity + 1);
            delay.writePosition = delay.delaySamples = delay.filled = 0;
        }
        if (node->serialNext != 0) prepareGroupDelays(node->serialNext);
    }
    if (const auto* first = findNode(firstId); first != nullptr && first->groupNext != 0) prepareGroupDelays(first->groupNext);
}
bool Runtime::needsDelayCapacity() const noexcept
{
    if (preparedChannels <= 0) return false;
    for (const auto& [id, instance] : instances) {
        juce::ignoreUnused(id);
        if (instance.processor != nullptr && instance.processor->publishedRenderGraph != instance.render) return true;
    }
    const auto check = [&](const auto& self, int firstId) -> bool {
        const auto capacity = maximumParallelLatency(firstId);
        for (auto* node = findNode(firstId); node != nullptr; node = findNode(node->parallelNext)) {
            const auto found = delays.find(node->id);
            if (found == delays.end() || found->second.samples.getNumSamples() < capacity + 1) return true;
            if (node->serialNext != 0 && self(self, node->serialNext)) return true;
        }
        const auto* first = findNode(firstId);
        return first != nullptr && first->groupNext != 0 && self(self, first->groupNext);
    };
    return check(check, topology.entryInstanceId);
}

void Runtime::applyDelay(const int id, juce::AudioBuffer<float>& buffer)
{
    const auto found = delays.find(id);
    if (found == delays.end() || found->second.delaySamples <= 0)
        return;
    auto& delay = found->second;
    const auto length = delay.samples.getNumSamples();
    if (length <= delay.delaySamples)
        return;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto readPosition = (delay.writePosition + length - delay.delaySamples) % length;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto input = buffer.getSample(channel, sample);
            const auto output = delay.filled >= delay.delaySamples ? delay.samples.getSample(channel, readPosition) : 0.0f;
            delay.samples.setSample(channel, delay.writePosition, input);
            buffer.setSample(channel, sample, output);
        }
        delay.writePosition = (delay.writePosition + 1) % length;
        delay.filled = juce::jmin(length, delay.filled + 1);
    }
}

AvaAudioProcessor* Runtime::getSoloProcessor(const bool channelSolo) const noexcept
{
    const auto selected = owner.requestedSolo[channelSolo ? 1u : 0u].load(std::memory_order_acquire);
    if (selected == -2) return nullptr;
    if (selected == 0) return &owner;
    if (selected > 0) {
        const auto found = instances.find(selected);
        return found != instances.end() ? found->second.processor.get() : nullptr;
    }
    const auto active = [channelSolo] (const AvaAudioProcessor& processor)
    {
        const auto* value = channelSolo ? processor.soloChannelParam : processor.soloInstanceParam;
        return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f;
    };
    if (active(owner)) return &owner;
    for (const auto& [id, instance] : instances)
    {
        juce::ignoreUnused(id);
        if (instance.processor != nullptr && active(*instance.processor)) return instance.processor.get();
    }
    return nullptr;
}

void Runtime::synchronizeSoloSelection()
{
    const juce::ScopedValueSetter<AvaAudioProcessor*> guard(AvaAudioProcessor::updatingSoloOwner, &owner);
    for (size_t mode = 0; mode < owner.requestedSolo.size(); ++mode)
    {
        auto selected = owner.requestedSolo[mode].load(std::memory_order_acquire);
        if (selected == -1) {
            auto* active = getSoloProcessor(mode == 1);
            const auto restored = active == nullptr ? -2 : active == &owner ? 0 : active->routingNodeId;
            auto expected = -1;
            owner.requestedSolo[mode].compare_exchange_strong(expected, restored, std::memory_order_acq_rel);
            selected = owner.requestedSolo[mode].load(std::memory_order_acquire);
        }
        const auto apply = [&](AvaAudioProcessor& processor, int id)
        {
            if (owner.requestedSolo[mode].load(std::memory_order_acquire) != selected) return;
            auto* parameter = processor.parameters.getParameter(mode == 1
                ? AvaAudioProcessor::paramSoloChannelId : AvaAudioProcessor::paramSoloInstanceId);
            const float wanted = selected == id ? 1.0f : 0.0f;
            if (parameter != nullptr && (parameter->getValue() >= 0.5f) != (wanted >= 0.5f))
                parameter->setValueNotifyingHost(wanted);
        };
        apply(owner, 0);
        for (auto& [id, instance] : instances)
            if (instance.processor != nullptr) apply(*instance.processor, id);
        if (owner.requestedSolo[mode].load(std::memory_order_acquire) != selected) {
            owner.soloStatePending.store(true, std::memory_order_release);
            owner.triggerAsyncUpdate();
        }
    }
}

int Runtime::getLatencySamples() const noexcept
{
    if (auto* solo = getSoloProcessor(false))
    {
        if (needsUpstreamForListen(*solo))
            if (const auto latency = latencyToInstance(topology.entryInstanceId, solo->getRoutingInstanceId()); latency >= 0)
                return latency;
        return solo == &owner ? nodeLatency(topology.rootInstanceId)
            : solo->globalBypassParam != nullptr && solo->globalBypassParam->load() >= 0.5f
                ? 0 : solo->getModuleChainLatencySamples();
    }
    return groupLatency(topology.entryInstanceId);
}

int Runtime::latencyToInstance(const int firstId, const int instanceId) const noexcept
{
    const auto* first = findNode(firstId);
    if (first == nullptr) return -1;
    for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
    {
        if (node->id == instanceId) return nodeLatency(node->id);
        if (const auto latency = latencyToInstance(node->serialNext, instanceId); latency >= 0)
            return nodeLatency(node->id) + latency;
    }
    if (const auto latency = latencyToInstance(first->groupNext, instanceId); latency >= 0)
        return parallelLatency(firstId) + latency;
    return -1;
}

uint64_t Runtime::getRenderControlSignature() const noexcept
{
    uint64_t result = rootBypassed.load() ? 1u : 0u;
    for (const auto& [id, instance] : instances) {
        if (instance.processor == nullptr || instance.render == nullptr) continue;
        const auto value = instance.processor->getRenderControlSignature(*instance.render);
        result ^= value + uint64_t(unsigned(id)) + 0x9e3779b97f4a7c15ULL + (result << 6) + (result >> 2);
    }
    return result;
}

void Runtime::writeProcessorStates(juce::ValueTree& state, const int excludedId) const
{
    if (std::none_of(instances.begin(), instances.end(),
        [excludedId](const auto& entry) { return entry.first != excludedId && entry.second.processor != nullptr; }))
    {
        state.removeProperty(processorStatesKey, nullptr);
        return;
    }

    juce::ValueTree stored("routing_processors");
    for (const auto& [id, instance] : instances)
    {
        if (id == excludedId || instance.processor == nullptr)
            continue;
        juce::MemoryBlock binary;
        instance.processor->getStateInformation(binary);
        juce::ValueTree item("instance");
        item.setProperty("id", id, nullptr);
        item.setProperty("state", binary.toBase64Encoding(), nullptr);
        stored.addChild(item, -1, nullptr);
    }
    if (const auto xml = stored.createXml())
        state.setProperty(processorStatesKey, xml->toString(), nullptr);
}

bool Runtime::restoreProcessorStates(const juce::ValueTree& state)
{
    const auto serialized = state.getProperty(processorStatesKey).toString();
    if (serialized.isEmpty())
        return std::none_of(instances.begin(), instances.end(),
            [] (const auto& entry) { return entry.second.processor != nullptr; });
    const auto xml = juce::XmlDocument::parse(serialized);
    if (xml == nullptr || ! xml->hasTagName("routing_processors"))
        return false;
    bool success = true;
    std::set<int> restoredInstanceIds;
    const auto stored = juce::ValueTree::fromXml(*xml);
    for (const auto item : stored)
    {
        const auto id = static_cast<int>(item.getProperty("id", 0));
        if (!restoredInstanceIds.insert(id).second)
        {
            juce::Logger::writeToLog("AVA: duplicate routing processor state: " + juce::String(id));
            success = false;
            continue;
        }
        const auto found = instances.find(id);
        if (found == instances.end() || found->second.processor == nullptr)
        {
            juce::Logger::writeToLog("AVA: routing processor missing during restore: " + juce::String(id));
            success = false;
            continue;
        }
        juce::MemoryBlock binary;
        if (!binary.fromBase64Encoding(item.getProperty("state").toString())
            || !found->second.processor->applyClipboardStateInformation(
                binary.getData(), static_cast<int>(binary.getSize())))
        {
            juce::Logger::writeToLog("AVA: unable to restore routing processor " + juce::String(id));
            success = false;
        }
    }
    for (const auto& [id, instance] : instances)
    {
        if (instance.processor != nullptr && !restoredInstanceIds.contains(id))
        {
            juce::Logger::writeToLog("AVA: routing instance missing from snapshot: " + juce::String(id));
            success = false;
        }
    }
    return success;
}
}
