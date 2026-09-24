#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace ava::routing
{
inline constexpr auto instancesStateKey = "ava.routing.instances";
inline constexpr auto nextInstanceIdStateKey = "ava.routing.next_instance_id";
inline constexpr auto rootInstanceIdStateKey = "ava.routing.root_instance_id";
inline constexpr auto namesStateKey = "ava.routing.names";
inline constexpr auto processorStatesKey = "ava.routing.processor_states";
inline constexpr int maximumInstanceCount = 64;

struct State
{
    struct Node
    {
        int id = 0;
        int serialNext = 0;
        int parallelNext = 0;

        bool operator==(const Node&) const = default;
    };

    std::vector<Node> nodes { { 1, 0, 0 } };
    int entryInstanceId = 1;
    int nextInstanceId = 2;
    int rootInstanceId = 1;
    std::map<int, juce::String> names;

    bool operator==(const State&) const = default;
};

bool isCurrentState(const juce::ValueTree& tree);
bool hasCurrentProcessorStates(const juce::ValueTree& tree);
State readState(const juce::ValueTree& tree);
juce::String getDisplayName(const State& state, int instanceId);
bool insertSerialInstance(juce::ValueTree& tree, int afterInstanceId);
bool insertParallelInstance(juce::ValueTree& tree, int targetInstanceId);
bool removeInstance(juce::ValueTree& tree, int instanceId);
bool moveInstanceSerialAfter(juce::ValueTree& tree, int instanceId, int targetInstanceId);
bool moveInstanceParallelTo(juce::ValueTree& tree, int instanceId, int targetInstanceId);
bool renameInstance(juce::ValueTree& tree, int instanceId, const juce::String& name);
}
