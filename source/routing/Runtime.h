#pragma once

#include "State.h"
#include "../shell/Processor.h"

#include <JuceHeader.h>
#include <map>
#include <memory>

class AvaAudioProcessor;

namespace ava::routing
{
class Runtime final
{
public:
    explicit Runtime(AvaAudioProcessor&, const Runtime* previous = nullptr);
    ~Runtime();

    void synchronize(const juce::ValueTree&);
    void prepare(double sampleRate, int samplesPerBlock, int channels);
    void release();
    void reset();
    void refreshDelayCapacity();
    bool needsDelayCapacity() const noexcept;
    unsigned process(juce::AudioBuffer<float>&, juce::MidiBuffer&);
    int getLatencySamples() const noexcept;
    int getMaximumLatencySamples() const noexcept { return maximumGroupLatency(topology.entryInstanceId); }
    AvaAudioProcessor* getSoloProcessor(bool channelSolo) const noexcept;
    void synchronizeSoloSelection();
    std::shared_ptr<AvaAudioProcessor> getInstanceHandle(int id) noexcept;
    uint64_t getRenderControlSignature() const noexcept;
    bool hasMultipleInstances() const noexcept { return getInstanceCount(topology) > 1; }
    bool isRootBypassed() const noexcept { return rootBypassed.load(std::memory_order_relaxed); }
    void setRootBypassed(bool bypassed) noexcept { rootBypassed.store(bypassed, std::memory_order_relaxed); }
    int getRootInstanceId() const noexcept { return topology.rootInstanceId; }
    void writeProcessorStates(juce::ValueTree&, int excludedId = 0) const;
    bool restoreProcessorStates(const juce::ValueTree&);

private:
    struct Instance
    {
        std::shared_ptr<AvaAudioProcessor> processor;
        std::shared_ptr<AvaAudioProcessor::RenderGraph> render;
        juce::AudioBuffer<float> input;
        juce::AudioBuffer<float> work;
    };
    struct Delay
    {
        juce::AudioBuffer<float> samples;
        int writePosition = 0;
        int delaySamples = 0, filled = 0;
    };

    const State::Node* findNode(int id) const noexcept;
    void prepareBuffers(Instance&);
    unsigned branchChannels(int id) const noexcept;
    unsigned groupChannels(int firstId) const noexcept;
    bool needsUpstreamForListen(const AvaAudioProcessor&) const noexcept;
    struct SoloListen
    {
        juce::AudioBuffer<float>& output;
        int instanceId;
        bool captured = false;
    };
    unsigned processGroup(int firstId, juce::AudioBuffer<float>& input,
                          juce::AudioBuffer<float>& output, juce::MidiBuffer&, SoloListen* = nullptr);
    int latencyToInstance(int firstId, int instanceId) const noexcept;
    void planGroupDelays(int firstId);
    void applyDelay(int id, juce::AudioBuffer<float>&);
    int nodeLatency(int id) const noexcept;
    int maximumNodeLatency(int id) const noexcept;
    int maximumGroupLatency(int firstId) const noexcept;
    int maximumParallelLatency(int firstId) const noexcept;
    void prepareGroupDelays(int firstId);
    int parallelLatency(int firstId) const noexcept;
    int groupLatency(int firstId) const noexcept;

    AvaAudioProcessor& owner;
    State topology;
    std::atomic<bool> rootBypassed { false };
    std::map<int, Instance> instances;
    std::map<int, Delay> delays;
    juce::AudioBuffer<float> rootInput;
    juce::AudioBuffer<float> rootWork;
    int preparedChannels = 0;
    int preparedBlockSize = 0;
    double preparedSampleRate = 0.0;
};
}
