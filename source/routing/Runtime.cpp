#include "Runtime.h"
#include "../shell/Processor.h"

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
    instance.input.setSize(preparedChannels, preparedBlockSize);
    instance.work.setSize(preparedChannels, preparedBlockSize);
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
        if (required.contains(it->first))
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
    rootInput.setSize(preparedChannels, preparedBlockSize);
    rootWork.setSize(preparedChannels, preparedBlockSize);
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

void Runtime::process(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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
    if (auto* solo = getSoloProcessor(false))
    {
        solo->setPlayHead(owner.getPlayHead());
        solo->setNonRealtime(owner.isNonRealtime());
        if (solo == &owner) solo->processOwnChannels(buffer);
        else {
            const auto found = instances.find(solo->getRoutingInstanceId());
            AvaAudioProcessor::RenderingScope scope(*solo, found != instances.end() ? found->second.render.get() : nullptr);
            solo->processChannelBlock(buffer, midi);
        }
        return;
    }
    if (! hasMultipleInstances() || preparedBlockSize <= 0)
    {
        owner.processOwnChannels(buffer);
        return;
    }
    const auto channels = juce::jmin(buffer.getNumChannels(), preparedChannels);
    if (channels <= 0) return;
    for (int offset = 0; offset < buffer.getNumSamples(); offset += preparedBlockSize)
    {
        const auto samples = juce::jmin(preparedBlockSize, buffer.getNumSamples() - offset);
        std::array<float*, AvaAudioProcessor::routingChannelCount> channelData {};
        for (int channel = 0; channel < channels; ++channel)
            channelData[static_cast<size_t>(channel)] = buffer.getWritePointer(channel, offset);
        juce::AudioBuffer<float> chunk(channelData.data(), channels, samples);
        processGroup(topology.entryInstanceId, chunk, chunk, midi);
    }
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
                               juce::MidiBuffer& midi)
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
            for (int channel = 0; channel < channels; ++channel)
                if ((mask & (1u << channel)) != 0) work.applyGain(channel, 0, samples, inputGain);
        }
        unsigned branchMask = 0;
        if (node->id == topology.rootInstanceId)
            branchMask = owner.processOwnChannels(work);
        else if (!node->container)
        {
            auto& processor = *found->second.processor;
            processor.setPlayHead(owner.getPlayHead());
            processor.setNonRealtime(owner.isNonRealtime());
            AvaAudioProcessor::RenderingScope scope(processor, found->second.render.get());
            processor.processChannelBlock(work, midi);
            const auto selected = processor.getProcessingInputChannels();
            branchMask = (1u << selected[0]) | (selected[1] >= 0 ? 1u << selected[1] : 0u);
        }
        if (node->serialNext != 0)
            branchMask |= processGroup(node->serialNext, work, work, midi);

        const auto branchLatency = nodeLatency(node->id) + groupLatency(node->serialNext);
        if (maximumLatency > branchLatency) applyDelay(node->id, work);

        // Only branches processing a channel contribute to its parallel sum.
        // Untouched channels pass once, rather than being duplicated per branch.
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto bit = 1u << channel;
            if (node == first || ((branchMask & bit) != 0 && (processedMask & bit) == 0))
                output.copyFrom(channel, 0, work, channel, 0, samples);
            else if ((branchMask & bit) != 0)
                output.addFrom(channel, 0, work, channel, 0, samples);
        }
        processedMask |= branchMask;
    }
    if (first->groupNext != 0)
        processedMask |= processGroup(first->groupNext, output, output, midi);
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
        if (delay.samples.getNumSamples() < capacity + 1 || delay.samples.getNumChannels() != preparedChannels) {
            delay.samples.setSize(preparedChannels, capacity + 1);
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

void Runtime::clearOtherSolo(AvaAudioProcessor& selected, const bool channelSolo)
{
    const auto clear = [&selected, channelSolo] (AvaAudioProcessor& processor)
    {
        if (&processor == &selected) return;
        auto* parameter = processor.parameters.getParameter(channelSolo
            ? AvaAudioProcessor::paramSoloChannelId : AvaAudioProcessor::paramSoloInstanceId);
        if (parameter != nullptr && parameter->getValue() >= 0.5f)
            parameter->setValueNotifyingHost(0.0f);
    };
    clear(owner);
    for (auto& [id, instance] : instances)
    {
        juce::ignoreUnused(id);
        if (instance.processor != nullptr) clear(*instance.processor);
    }
}

int Runtime::getLatencySamples() const noexcept
{
    if (auto* solo = getSoloProcessor(false))
        return solo == &owner ? nodeLatency(topology.rootInstanceId)
            : solo->globalBypassParam != nullptr && solo->globalBypassParam->load() >= 0.5f
                ? 0 : solo->getModuleChainLatencySamples();
    return groupLatency(topology.entryInstanceId);
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
