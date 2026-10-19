#include "Runtime.h"
#include "../shell/Processor.h"

#include <algorithm>
#include <set>

namespace ava::routing
{
Runtime::Runtime(AvaAudioProcessor& ownerIn) : owner(ownerIn) {}
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
            instance.processor->parentStateChanged = [this]
            {
                owner.notifyHostOfStateChange();
                owner.triggerAsyncUpdate();
            };
            instance.processor->setPlayHead(owner.getPlayHead());
            instance.processor->setBusesLayout(owner.getBusesLayout());
        }
        prepareBuffers(instance);
        if (instance.processor != nullptr && preparedSampleRate > 0.0)
            instance.processor->prepareToPlay(preparedSampleRate, preparedBlockSize);
        instances.emplace(id, std::move(instance));
    }

    topology = nextTopology;
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
            instance.processor->setBusesLayout(owner.getBusesLayout());
            instance.processor->prepareToPlay(sampleRate, preparedBlockSize);
        }
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
    if (auto* solo = getSoloProcessor(false))
    {
        solo->setPlayHead(owner.getPlayHead());
        if (solo == &owner) solo->processOwnChannels(buffer);
        else solo->processChannelBlock(buffer, midi);
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
    for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
    {
        const auto found = instances.find(node->id);
        auto& workStorage = node->id == topology.rootInstanceId ? rootWork : found->second.work;
        juce::AudioBuffer<float> work(workStorage.getArrayOfWritePointers(), channels, samples);
        for (int channel = 0; channel < channels; ++channel)
            work.copyFrom(channel, 0, groupInput, channel, 0, samples);

        unsigned branchMask = 0;
        if (node->id == topology.rootInstanceId)
            branchMask = owner.processOwnChannels(work);
        else if (!node->container)
        {
            auto& processor = *found->second.processor;
            processor.setPlayHead(owner.getPlayHead());
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
        return topology.rootBypassed || (owner.globalBypassParam != nullptr
                && owner.globalBypassParam->load(std::memory_order_relaxed) >= 0.5f)
            ? 0 : owner.getActiveModuleLatencySamples();
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
        if (delay.delaySamples != needed
            || delay.samples.getNumChannels() != preparedChannels)
        {
            delay.delaySamples = needed;
            delay.writePosition = 0;
            if (preparedChannels > 0)
            {
                delay.samples.setSize(preparedChannels, needed + 1);
                delay.samples.clear();
            }
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
        planGroupDelays(topology.entryInstanceId);
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
            const auto output = delay.samples.getSample(channel, readPosition);
            delay.samples.setSample(channel, delay.writePosition, input);
            buffer.setSample(channel, sample, output);
        }
        delay.writePosition = (delay.writePosition + 1) % length;
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
                ? 0 : solo->getActiveModuleLatencySamples();
    return groupLatency(topology.entryInstanceId);
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

void Runtime::restoreProcessorStates(const juce::ValueTree& state)
{
    const auto xml = juce::XmlDocument::parse(state.getProperty(processorStatesKey).toString());
    if (xml == nullptr || ! xml->hasTagName("routing_processors"))
        return;
    const auto stored = juce::ValueTree::fromXml(*xml);
    for (const auto item : stored)
    {
        const auto id = static_cast<int>(item.getProperty("id", 0));
        const auto found = instances.find(id);
        if (found == instances.end() || found->second.processor == nullptr)
            continue;
        juce::MemoryBlock binary;
        if (binary.fromBase64Encoding(item.getProperty("state").toString()))
            found->second.processor->setStateInformation(binary.getData(),
                                                         static_cast<int>(binary.getSize()));
    }
}
}
