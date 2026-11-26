#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>

namespace ava::routing
{
inline constexpr auto instancesStateKey = "ava.routing.instances";
inline constexpr auto nextInstanceIdStateKey = "ava.routing.next_instance_id";
inline constexpr auto rootInstanceIdStateKey = "ava.routing.root_instance_id";
inline constexpr auto rootBypassStateKey = "ava.routing.root_bypass";
inline constexpr auto namesStateKey = "ava.routing.names";
inline constexpr auto processorStatesKey = "ava.routing.processor_states";
inline constexpr int maximumInstanceCount = 64;

inline juce::String parameterTargetId(int id, const juce::String& parameterId)
{
    return "instance-" + juce::String(id) + "_" + parameterId;
}
inline bool parseParameterTarget(const juce::String& target, int& id, juce::String& parameterId)
{
    if (!target.startsWith("instance-")) return false;
    const auto separator = target.indexOfChar('_');
    if (separator < 10) return false;
    const auto number = target.substring(9, separator);
    id = number.getIntValue();
    parameterId = target.substring(separator + 1);
    return id > 0 && number == juce::String(id) && parameterId.isNotEmpty()
        && !parameterId.startsWith("instance-");
}
inline constexpr auto compensationParameterId = "compensation";
inline juce::String compensationTargetId(int id) { return parameterTargetId(id, compensationParameterId); }
inline juce::String bypassTargetId(int id) { return parameterTargetId(id, "bp"); }
inline int bypassTargetInstance(const juce::String& target)
{
    int id = 0;
    juce::String parameter;
    return parseParameterTarget(target, id, parameter) && parameter == "bp" ? id : 0;
}

struct State
{
    struct Node
    {
        int id = 0;
        int serialNext = 0;
        int parallelNext = 0;
        int groupNext = 0;

        bool container = false;
        bool compensated = false;

        bool operator==(const Node&) const = default;
    };

    std::vector<Node> nodes { { 1, 0, 0 } };
    int entryInstanceId = 1;
    int nextInstanceId = 2;
    int rootInstanceId = 1;
    bool rootBypassed = false;
    std::map<int, juce::String> names;

    bool operator==(const State&) const = default;
};

inline int getInstanceCount(const State& state) noexcept
{
    return static_cast<int>(std::count_if(state.nodes.begin(), state.nodes.end(),
        [](const auto& node) { return ! node.container; }));
}

inline const State::Node* findStateNode(const std::vector<State::Node>& nodes, const int id) noexcept
{
    const auto found = std::find_if(nodes.begin(), nodes.end(),
                                    [id] (const auto& node) { return node.id == id; });
    return found == nodes.end() ? nullptr : &*found;
}

bool isParallelHead(const State&, int id) noexcept;
bool setGroupCompensated(juce::ValueTree&, int id, bool enabled);
bool isCurrentState(const juce::ValueTree& tree);
bool hasCurrentProcessorStates(const juce::ValueTree& tree);
State readState(const juce::ValueTree& tree);
juce::String getDisplayName(const State& state, int instanceId);
bool insertSerialInstanceBefore(juce::ValueTree& tree, int targetInstanceId);
bool insertParallelInstanceBefore(juce::ValueTree& tree, int targetInstanceId);
bool insertSerialInstance(juce::ValueTree& tree, int afterInstanceId);
bool insertParallelInstance(juce::ValueTree& tree, int targetInstanceId);
bool insertInstanceBeforeGroup(juce::ValueTree& tree, int groupFirstInstanceId);
bool moveInstanceBeforeGroup(juce::ValueTree& tree, int instanceId, int groupFirstInstanceId);
bool moveInstanceAfterGroup(juce::ValueTree& tree, int instanceId, int groupFirstInstanceId);
bool insertInstanceAfterGroup(juce::ValueTree& tree, int groupFirstInstanceId);
bool insertParallelOutsideGroup(juce::ValueTree& tree, int groupFirstInstanceId, bool before);
bool moveParallelOutsideGroup(juce::ValueTree& tree, int instanceId, int groupFirstInstanceId, bool before);
bool removeInstance(juce::ValueTree& tree, int instanceId);
bool moveInstanceSerialBefore(juce::ValueTree& tree, int instanceId, int targetInstanceId);
bool moveInstanceParallelBefore(juce::ValueTree& tree, int instanceId, int targetInstanceId);
bool moveInstanceSerialAfter(juce::ValueTree& tree, int instanceId, int targetInstanceId);
bool moveInstanceParallelTo(juce::ValueTree& tree, int instanceId, int targetInstanceId);
bool renameInstance(juce::ValueTree& tree, int instanceId, const juce::String& name,
                    bool allowAutomaticName = false);
}
