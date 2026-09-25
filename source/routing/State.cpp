#include "State.h"
#include "../shell/OscSettings.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <map>
#include <set>

namespace ava::routing
{
namespace
{
bool parseNumber(const juce::String& text, int& value, const bool positive)
{
    if (text.isEmpty() || ! text.containsOnly("0123456789"))
        return false;
    const auto parsed = text.getLargeIntValue();
    if (parsed < (positive ? 1 : 0) || parsed > std::numeric_limits<int>::max())
        return false;
    value = static_cast<int>(parsed);
    return juce::String(value) == text;
}

State::Node* findNode(State& state, const int id)
{
    const auto found = std::find_if(state.nodes.begin(), state.nodes.end(),
                                    [id] (const State::Node& node) { return node.id == id; });
    return found == state.nodes.end() ? nullptr : &*found;
}

bool decodeTopology(const juce::String& encoded, State& state)
{
    state.nodes.clear();
    const auto separator = encoded.indexOfChar(';');
    if (separator >= 0)
    {
        if (! parseNumber(encoded.substring(0, separator), state.entryInstanceId, true))
            return false;
        const auto recordsText = encoded.substring(separator + 1);
        const auto records = juce::StringArray::fromTokens(recordsText, ",", "");
        if (records.isEmpty() || records.joinIntoString(",") != recordsText)
            return false;
        for (const auto& record : records)
        {
            const auto fields = juce::StringArray::fromTokens(record, ":", "");
            if ((fields.size() != 3 && fields.size() != 4) || fields.joinIntoString(":") != record)
                return false;
            State::Node node;
            if (! parseNumber(fields[0], node.id, true)
                || ! parseNumber(fields[1], node.serialNext, false)
                || ! parseNumber(fields[2], node.parallelNext, false)
                || (fields.size() == 4 && ! parseNumber(fields[3], node.groupNext, false)))
                return false;
            state.nodes.push_back(node);
        }
    }
    else
    {
        // Earlier routing layouts stored whole rows; retain those positions on load.
        const auto stages = juce::StringArray::fromTokens(encoded, ",", "");
        if (stages.isEmpty() || stages.joinIntoString(",") != encoded)
            return false;
        int previousStageStart = 0;
        for (const auto& stage : stages)
        {
            const auto lanes = juce::StringArray::fromTokens(stage, "|", "");
            if (lanes.isEmpty() || lanes.joinIntoString("|") != stage)
                return false;
            int firstId = 0;
            int previousId = 0;
            for (const auto& lane : lanes)
            {
                int id = 0;
                if (! parseNumber(lane, id, true))
                    return false;
                state.nodes.push_back({ id, 0, 0 });
                if (firstId == 0)
                    firstId = id;
                if (auto* previous = findNode(state, previousId))
                    previous->parallelNext = id;
                previousId = id;
            }
            if (auto* previous = findNode(state, previousStageStart))
                previous->serialNext = firstId;
            else
                state.entryInstanceId = firstId;
            previousStageStart = firstId;
        }
    }

    if (state.nodes.empty() || state.nodes.size() > maximumInstanceCount)
        return false;
    std::map<int, int> incoming;
    std::set<int> parallelChildren;
    for (const auto& node : state.nodes)
        if (! incoming.emplace(node.id, 0).second)
            return false;
    for (const auto& node : state.nodes)
    {
        if (node.parallelNext != 0)
            parallelChildren.insert(node.parallelNext);
        for (const auto child : { node.serialNext, node.parallelNext, node.groupNext })
            if (child != 0)
            {
                const auto found = incoming.find(child);
                if (found == incoming.end() || ++found->second > 1)
                    return false;
            }
    }
    if (incoming.find(state.entryInstanceId) == incoming.end())
        return false;
    for (const auto& [id, count] : incoming)
        if (count != (id == state.entryInstanceId ? 0 : 1))
            return false;
    for (const auto& node : state.nodes)
        if (node.groupNext != 0 && parallelChildren.contains(node.id))
            return false;

    std::set<int> visited;
    const std::function<bool(int)> visit = [&] (const int id)
    {
        if (! visited.insert(id).second)
            return false;
        const auto* node = findNode(state, id);
        return node != nullptr
            && (node->serialNext == 0 || visit(node->serialNext))
            && (node->parallelNext == 0 || visit(node->parallelNext))
            && (node->groupNext == 0 || visit(node->groupNext));
    };
    return visit(state.entryInstanceId) && visited.size() == state.nodes.size();
}

bool decode(const juce::ValueTree& tree, State& result)
{
    const auto hasInstances = tree.hasProperty(instancesStateKey);
    const auto hasNextId = tree.hasProperty(nextInstanceIdStateKey);
    if (! hasInstances && ! hasNextId)
        return ! tree.hasProperty(rootInstanceIdStateKey) && ! tree.hasProperty(namesStateKey);
    if (! hasInstances || ! hasNextId)
        return false;

    State decoded;
    if (! decodeTopology(tree.getProperty(instancesStateKey).toString(), decoded)
        || ! parseNumber(tree.getProperty(nextInstanceIdStateKey).toString(), decoded.nextInstanceId, true))
        return false;
    for (const auto& node : decoded.nodes)
        if (decoded.nextInstanceId <= node.id)
            return false;
    if (tree.hasProperty(rootInstanceIdStateKey)
        && ! parseNumber(tree.getProperty(rootInstanceIdStateKey).toString(), decoded.rootInstanceId, true))
        return false;
    if (findNode(decoded, decoded.rootInstanceId) == nullptr)
        return false;

    if (tree.hasProperty(namesStateKey))
    {
        const auto parsed = juce::JSON::parse(tree.getProperty(namesStateKey).toString());
        const auto* entries = parsed.getArray();
        if (entries == nullptr || entries->size() > static_cast<int>(decoded.nodes.size()))
            return false;
        for (const auto& entry : *entries)
        {
            const auto* object = entry.getDynamicObject();
            if (object == nullptr)
                return false;
            const auto idValue = object->getProperty("id");
            const auto nameValue = object->getProperty("name");
            int id = 0;
            if (! idValue.isInt() || ! parseNumber(idValue.toString(), id, true)
                || findNode(decoded, id) == nullptr || ! nameValue.isString())
                return false;
            const auto name = nameValue.toString();
            if (name.isEmpty() || name.length() > 48 || name != name.trim()
                || name != name.toUpperCase() || name.containsAnyOf("\r\n")
                || ! decoded.names.emplace(id, name).second)
                return false;
        }
    }
    result = std::move(decoded);
    return true;
}

void write(juce::ValueTree& tree, const State& state)
{
    juce::StringArray records;
    for (const auto& node : state.nodes)
        records.add(juce::String(node.id) + ":" + juce::String(node.serialNext)
                    + ":" + juce::String(node.parallelNext)
                    + ":" + juce::String(node.groupNext));
    tree.setProperty(instancesStateKey,
                     juce::String(state.entryInstanceId) + ";" + records.joinIntoString(","), nullptr);
    tree.setProperty(nextInstanceIdStateKey, state.nextInstanceId, nullptr);
    tree.setProperty(rootInstanceIdStateKey, state.rootInstanceId, nullptr);
    if (state.names.empty())
        tree.removeProperty(namesStateKey, nullptr);
    else
    {
        juce::Array<juce::var> entries;
        for (const auto& [id, name] : state.names)
        {
            auto* entry = new juce::DynamicObject();
            entry->setProperty("id", id);
            entry->setProperty("name", name);
            entries.add(juce::var(entry));
        }
        tree.setProperty(namesStateKey, juce::JSON::toString(juce::var(entries), false), nullptr);
    }
}

bool canAdd(const State& state)
{
    return state.nodes.size() < maximumInstanceCount
        && state.nextInstanceId < std::numeric_limits<int>::max();
}

bool appendContinuation(State& state, const int firstId, const int continuationId)
{
    auto* first = findNode(state, firstId);
    if (first == nullptr)
        return false;
    if (first->groupNext != 0)
        return appendContinuation(state, first->groupNext, continuationId);
    if (first->parallelNext != 0)
    {
        first->groupNext = continuationId;
        return true;
    }
    if (first->serialNext != 0)
        return appendContinuation(state, first->serialNext, continuationId);
    first->serialNext = continuationId;
    return true;
}

bool detachForMove(State& state, const int id)
{
    auto* source = findNode(state, id);
    if (source == nullptr)
        return false;
    const auto serial = source->serialNext;
    const auto parallel = source->parallelNext;
    const auto group = source->groupNext;
    const auto replacement = parallel != 0 ? parallel : (serial != 0 ? serial : group);
    if (replacement == 0 && state.entryInstanceId == id)
        return false;
    if (parallel != 0 && serial != 0)
    {
        auto* tail = findNode(state, parallel);
        while (tail->serialNext != 0)
            tail = findNode(state, tail->serialNext);
        tail->serialNext = serial;
    }
    if (parallel != 0 && group != 0)
        findNode(state, parallel)->groupNext = group;
    else if (parallel == 0 && serial != 0 && group != 0
             && ! appendContinuation(state, serial, group))
        return false;
    if (state.entryInstanceId == id)
        state.entryInstanceId = replacement;
    else
    {
        auto found = false;
        for (auto& node : state.nodes)
        {
            if (node.serialNext == id)
            {
                node.serialNext = replacement;
                found = true;
                break;
            }
            if (node.parallelNext == id)
            {
                node.parallelNext = replacement;
                found = true;
                break;
            }
            if (node.groupNext == id)
            {
                node.groupNext = replacement;
                found = true;
                break;
            }
        }
        if (! found)
            return false;
    }
    source->serialNext = 0;
    source->parallelNext = 0;
    source->groupNext = 0;
    return true;
}
}

bool isCurrentState(const juce::ValueTree& tree)
{
    State ignored;
    return decode(tree, ignored);
}

bool hasCurrentProcessorStates(const juce::ValueTree& tree)
{
    if (! tree.hasProperty(processorStatesKey))
        return true;

    const auto xml = juce::XmlDocument::parse(tree.getProperty(processorStatesKey).toString());
    if (xml == nullptr || ! xml->hasTagName("routing_processors"))
        return false;

    const auto topology = readState(tree);
    const auto stored = juce::ValueTree::fromXml(*xml);
    std::set<int> seen;
    if (stored.getNumChildren() > maximumInstanceCount - 1)
        return false;
    for (const auto item : stored)
    {
        const auto id = static_cast<int>(item.getProperty("id", 0));
        if (! item.hasType("instance") || id <= 0 || id == topology.rootInstanceId
            || ! seen.insert(id).second
            || std::none_of(topology.nodes.begin(), topology.nodes.end(),
                            [id] (const auto& node) { return node.id == id; }))
            return false;
        juce::MemoryBlock binary;
        if (! binary.fromBase64Encoding(item.getProperty("state").toString())
            || binary.isEmpty())
            return false;
        const auto childXml = juce::AudioProcessor::getXmlFromBinary(binary.getData(),
                                                                     static_cast<int>(binary.getSize()));
        if (childXml == nullptr || ! childXml->hasTagName("ava_state"))
            return false;
    }
    return true;
}

State readState(const juce::ValueTree& tree)
{
    State state;
    decode(tree, state);
    return state;
}

juce::String getDisplayName(const State& state, const int instanceId)
{
    const auto found = std::find_if(state.nodes.begin(), state.nodes.end(),
                                    [instanceId] (const auto& node) { return node.id == instanceId; });
    if (found == state.nodes.end())
        return {};
    if (const auto custom = state.names.find(instanceId); custom != state.names.end())
        return custom->second;

    const auto root = std::find_if(state.nodes.begin(), state.nodes.end(),
                                   [&state] (const auto& node) { return node.id == state.rootInstanceId; });
    auto rootInParallel = root != state.nodes.end() && root->parallelNext != 0;
    for (const auto& node : state.nodes)
        rootInParallel = rootInParallel || node.parallelNext == state.rootInstanceId;

    std::vector<int> ids;
    for (const auto& node : state.nodes)
        ids.push_back(node.id);
    std::sort(ids.begin(), ids.end());

    auto number = 1;
    for (const auto id : ids)
    {
        if (id == state.rootInstanceId && ! rootInParallel)
        {
            if (id == instanceId)
                return "ROOT";
        }
        else
        {
            const auto name = "INS-" + juce::String(number++);
            if (id == instanceId)
                return name;
        }
    }
    return {};
}

bool insertSerialInstance(juce::ValueTree& tree, const int afterInstanceId)
{
    State state;
    if (! decode(tree, state) || ! canAdd(state))
        return false;
    auto* target = findNode(state, afterInstanceId);
    if (target == nullptr)
        return false;
    const auto id = state.nextInstanceId++;
    const auto previousNext = target->serialNext;
    target->serialNext = id;
    state.nodes.push_back({ id, previousNext, 0 });
    write(tree, state);
    return true;
}

bool insertParallelInstance(juce::ValueTree& tree, const int targetInstanceId)
{
    State state;
    if (! decode(tree, state) || ! canAdd(state))
        return false;
    auto* target = findNode(state, targetInstanceId);
    if (target == nullptr)
        return false;
    const auto id = state.nextInstanceId++;
    const auto previousNext = target->parallelNext;
    target->parallelNext = id;
    state.nodes.push_back({ id, 0, previousNext });
    write(tree, state);
    return true;
}

bool insertInstanceAfterGroup(juce::ValueTree& tree, const int groupFirstInstanceId)
{
    State state;
    if (! decode(tree, state) || ! canAdd(state))
        return false;
    auto* first = findNode(state, groupFirstInstanceId);
    if (first == nullptr || (first->parallelNext == 0 && first->groupNext == 0))
        return false;
    if (std::any_of(state.nodes.begin(), state.nodes.end(),
                    [groupFirstInstanceId] (const auto& node)
                    {
                        return node.parallelNext == groupFirstInstanceId;
                    }))
        return false;
    const auto id = state.nextInstanceId++;
    const auto previousNext = first->groupNext;
    first->groupNext = id;
    state.nodes.push_back({ id, previousNext, 0, 0 });
    write(tree, state);
    return true;
}

bool removeInstance(juce::ValueTree& tree, const int instanceId)
{
    State state;
    if (! decode(tree, state) || state.nodes.size() <= 1)
        return false;
    const auto* source = findNode(state, instanceId);
    if (source == nullptr)
        return false;
    const auto serial = source->serialNext;
    const auto parallel = source->parallelNext;
    const auto group = source->groupNext;
    const auto replacement = parallel != 0 ? parallel : (serial != 0 ? serial : group);
    if (parallel != 0 && serial != 0)
    {
        auto* tail = findNode(state, parallel);
        while (tail->serialNext != 0)
            tail = findNode(state, tail->serialNext);
        tail->serialNext = serial;
    }
    if (parallel != 0 && group != 0)
        findNode(state, parallel)->groupNext = group;
    else if (parallel == 0 && serial != 0 && group != 0
             && ! appendContinuation(state, serial, group))
        return false;
    if (state.entryInstanceId == instanceId)
        state.entryInstanceId = replacement;
    else
        for (auto& node : state.nodes)
        {
            if (node.serialNext == instanceId)
            {
                node.serialNext = replacement;
                break;
            }
            if (node.parallelNext == instanceId)
            {
                node.parallelNext = replacement;
                break;
            }
            if (node.groupNext == instanceId)
            {
                node.groupNext = replacement;
                break;
            }
        }
    state.nodes.erase(std::remove_if(state.nodes.begin(), state.nodes.end(),
                                    [instanceId] (const State::Node& node) { return node.id == instanceId; }),
                      state.nodes.end());
    state.names.erase(instanceId);
    if (state.rootInstanceId == instanceId)
        state.rootInstanceId = state.entryInstanceId;
    write(tree, state);
    return true;
}

bool moveInstanceSerialAfter(juce::ValueTree& tree, const int instanceId, const int targetInstanceId)
{
    if (instanceId == targetInstanceId)
        return false;
    State state;
    if (! decode(tree, state) || ! detachForMove(state, instanceId))
        return false;
    auto* source = findNode(state, instanceId);
    auto* target = findNode(state, targetInstanceId);
    if (source == nullptr || target == nullptr)
        return false;
    source->serialNext = target->serialNext;
    target->serialNext = instanceId;
    write(tree, state);
    return true;
}

bool moveInstanceParallelTo(juce::ValueTree& tree, const int instanceId, const int targetInstanceId)
{
    if (instanceId == targetInstanceId)
        return false;
    State state;
    if (! decode(tree, state) || ! detachForMove(state, instanceId))
        return false;
    auto* source = findNode(state, instanceId);
    auto* target = findNode(state, targetInstanceId);
    if (source == nullptr || target == nullptr)
        return false;
    source->parallelNext = target->parallelNext;
    target->parallelNext = instanceId;
    write(tree, state);
    return true;
}

bool renameInstance(juce::ValueTree& tree, const int instanceId, const juce::String& name)
{
    State state;
    if (! decode(tree, state) || findNode(state, instanceId) == nullptr)
        return false;
    const auto normalized = name.trim().toUpperCase();
    if (! isValidOscInstanceName(normalized))
        return false;
    if (normalized == "ROOT"
        || (normalized.startsWith("INS-")
            && normalized.substring(4).isNotEmpty()
            && normalized.substring(4).containsOnly("0123456789")))
        return false;
    for (const auto& node : state.nodes)
        if (node.id != instanceId && getDisplayName(state, node.id) == normalized)
            return false;
    state.names[instanceId] = normalized;
    write(tree, state);
    return true;
}
}
