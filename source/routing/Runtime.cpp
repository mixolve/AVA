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
    const auto found = std::find_if(topology.nodes.begin(), topology.nodes.end(),
                                    [id] (const auto& node) { return node.id == id; });
    return found == topology.nodes.end() ? nullptr : &*found;
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
        instance.processor = std::make_shared<AvaAudioProcessor>(true);
        instance.processor->routingOwner = &owner;
        instance.processor->parentStateChanged = [this]
        {
            owner.notifyHostOfStateChange();
            owner.triggerAsyncUpdate();
        };
        instance.processor->setBusesLayout(owner.getBusesLayout());
        prepareBuffers(instance);
        if (preparedSampleRate > 0.0)
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
    preparedChannels = juce::jlimit(1, 2, channels);
    rootInput.setSize(preparedChannels, preparedBlockSize);
    rootWork.setSize(preparedChannels, preparedBlockSize);
    rootInput.clear();
    rootWork.clear();
    for (auto& [id, instance] : instances)
    {
        juce::ignoreUnused(id);
        instance.processor->setBusesLayout(owner.getBusesLayout());
        prepareBuffers(instance);
        instance.processor->prepareToPlay(sampleRate, preparedBlockSize);
    }
    refreshDelayCapacity();
}

void Runtime::release()
{
    for (auto& [id, instance] : instances)
    {
        juce::ignoreUnused(id);
        instance.processor->releaseResources();
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
        instance.processor->reset();
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
    if (! hasMultipleInstances() || preparedBlockSize <= 0)
    {
        owner.processOwnBlock(buffer);
        return;
    }

    const auto channels = juce::jmin(buffer.getNumChannels(), preparedChannels);
    if (channels <= 0)
        return;

    for (int offset = 0; offset < buffer.getNumSamples(); offset += preparedBlockSize)
    {
        const auto samples = juce::jmin(preparedBlockSize, buffer.getNumSamples() - offset);
        float* channelData[2] {};
        for (int channel = 0; channel < channels; ++channel)
            channelData[channel] = buffer.getWritePointer(channel, offset);
        juce::AudioBuffer<float> chunk(channelData, channels, samples);
        processGroup(topology.entryInstanceId, chunk, chunk, midi);
    }
}

void Runtime::processGroup(const int firstId,
                           juce::AudioBuffer<float>& input,
                           juce::AudioBuffer<float>& output,
                           juce::MidiBuffer& midi)
{
    const auto* first = findNode(firstId);
    const auto scratch = instances.find(firstId);
    if (first == nullptr)
        return;

    // The root node has no child processor, so its scratch storage belongs to
    // a separate root buffer below.
    auto& groupInputStorage = firstId == topology.rootInstanceId
        ? rootInput : scratch->second.input;
    const auto channels = input.getNumChannels();
    const auto samples = input.getNumSamples();
    juce::AudioBuffer<float> groupInput(groupInputStorage.getArrayOfWritePointers(), channels, samples);
    for (int channel = 0; channel < channels; ++channel)
        groupInput.copyFrom(channel, 0, input, channel, 0, samples);

    output.clear();
    const auto maximumLatency = parallelLatency(firstId);
    for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
    {
        const auto found = instances.find(node->id);
        auto& workStorage = node->id == topology.rootInstanceId
            ? rootWork : found->second.work;
        juce::AudioBuffer<float> work(workStorage.getArrayOfWritePointers(), channels, samples);
        for (int channel = 0; channel < channels; ++channel)
            work.copyFrom(channel, 0, groupInput, channel, 0, samples);

        if (node->id == topology.rootInstanceId)
            owner.processOwnBlock(work);
        else
            found->second.processor->processBlock(work, midi);

        if (node->serialNext != 0)
            processGroup(node->serialNext, work, work, midi);

        const auto branchLatency = nodeLatency(node->id) + groupLatency(node->serialNext);
        if (maximumLatency > branchLatency)
            applyDelay(node->id, work);

        for (int channel = 0; channel < channels; ++channel)
            output.addFrom(channel, 0, work, channel, 0, samples);
    }

    if (first->groupNext != 0)
        processGroup(first->groupNext, output, output, midi);
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
        return owner.globalBypassParam != nullptr
                && owner.globalBypassParam->load(std::memory_order_relaxed) >= 0.5f
            ? 0 : owner.getActiveModuleLatencySamples();
    const auto found = instances.find(id);
    return found == instances.end() ? 0
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

int Runtime::getLatencySamples() const noexcept
{
    return groupLatency(topology.entryInstanceId);
}

void Runtime::writeProcessorStates(juce::ValueTree& state, const int excludedId) const
{
    if (instances.empty() || (instances.size() == 1 && instances.contains(excludedId)))
    {
        state.removeProperty(processorStatesKey, nullptr);
        return;
    }

    juce::ValueTree stored("routing_processors");
    for (const auto& [id, instance] : instances)
    {
        if (id == excludedId)
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
        if (found == instances.end())
            continue;
        juce::MemoryBlock binary;
        if (binary.fromBase64Encoding(item.getProperty("state").toString()))
            found->second.processor->setStateInformation(binary.getData(),
                                                         static_cast<int>(binary.getSize()));
    }
}
}
