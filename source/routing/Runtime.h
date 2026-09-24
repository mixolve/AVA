#pragma once

#include "State.h"

#include <JuceHeader.h>
#include <map>
#include <memory>

class AvaAudioProcessor;

namespace ava::routing
{
class Runtime final
{
public:
    explicit Runtime(AvaAudioProcessor&);
    ~Runtime();

    void synchronize(const juce::ValueTree&);
    void prepare(double sampleRate, int samplesPerBlock, int channels);
    void release();
    void reset();
    void refreshDelayCapacity();
    void process(juce::AudioBuffer<float>&, juce::MidiBuffer&);
    int getLatencySamples() const noexcept;
    std::shared_ptr<AvaAudioProcessor> getInstanceHandle(int id) noexcept;
    bool hasMultipleInstances() const noexcept { return topology.nodes.size() > 1; }
    int getRootInstanceId() const noexcept { return topology.rootInstanceId; }
    void writeProcessorStates(juce::ValueTree&, int excludedId = 0) const;
    void restoreProcessorStates(const juce::ValueTree&);

private:
    struct Instance
    {
        std::shared_ptr<AvaAudioProcessor> processor;
        juce::AudioBuffer<float> input;
        juce::AudioBuffer<float> work;
    };
    struct Delay
    {
        juce::AudioBuffer<float> samples;
        int writePosition = 0;
        int delaySamples = 0;
    };

    const State::Node* findNode(int id) const noexcept;
    void prepareBuffers(Instance&);
    void processGroup(int firstId, juce::AudioBuffer<float>& input,
                      juce::AudioBuffer<float>& output, juce::MidiBuffer&);
    void planGroupDelays(int firstId);
    void applyDelay(int id, juce::AudioBuffer<float>&);
    int nodeLatency(int id) const noexcept;
    int groupLatency(int firstId) const noexcept;

    AvaAudioProcessor& owner;
    State topology;
    std::map<int, Instance> instances;
    std::map<int, Delay> delays;
    juce::AudioBuffer<float> rootInput;
    juce::AudioBuffer<float> rootWork;
    int preparedChannels = 0;
    int preparedBlockSize = 0;
    double preparedSampleRate = 0.0;
};
}
