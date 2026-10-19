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
            if ((fields.size() != 3 && fields.size() != 4 && fields.size() != 5) || fields.joinIntoString(":") != record)
                return false;
            State::Node node;
            if (! parseNumber(fields[0], node.id, true)
                || ! parseNumber(fields[1], node.serialNext, false)
                || ! parseNumber(fields[2], node.parallelNext, false)
                || (fields.size() >= 4 && ! parseNumber(fields[3], node.groupNext, false))
                || (fields.size() == 5 && fields[4] != "1"))
                return false;
            node.container = fields.size() == 5;
            state.nodes.push_back(node);
        }
    }
    else
    {

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

    if (state.nodes.empty() || getInstanceCount(state) > maximumInstanceCount || state.nodes.size() > 2 * maximumInstanceCount)
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
        if ((node.container && node.serialNext == 0)
            || (node.groupNext != 0 && parallelChildren.contains(node.id)))
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

void groupLooseInstances(State&);

bool decode(const juce::ValueTree& tree, State& result)
{
    const auto hasInstances = tree.hasProperty(instancesStateKey);
    const auto hasNextId = tree.hasProperty(nextInstanceIdStateKey);
    if (! hasInstances && ! hasNextId)
        return ! tree.hasProperty(rootInstanceIdStateKey) && ! tree.hasProperty(namesStateKey)
            && ! tree.hasProperty(rootBypassStateKey);
    if (! hasInstances || ! hasNextId)
        return false;

    State decoded;
    if (! decodeTopology(tree.getProperty(instancesStateKey).toString(), decoded)
        || ! parseNumber(tree.getProperty(nextInstanceIdStateKey).toString(), decoded.nextInstanceId, true))
        return false;
    for (const auto& node : decoded.nodes)
        if (! node.container && decoded.nextInstanceId <= node.id)
            return false;
    if (tree.hasProperty(rootInstanceIdStateKey)
        && ! parseNumber(tree.getProperty(rootInstanceIdStateKey).toString(), decoded.rootInstanceId, true))
        return false;
    if (findNode(decoded, decoded.rootInstanceId) == nullptr
        || findNode(decoded, decoded.rootInstanceId)->container)
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
                || findNode(decoded, id) == nullptr || findNode(decoded, id)->container || ! nameValue.isString())
                return false;
            const auto name = nameValue.toString();
            if (name.isEmpty() || name.length() > 48 || name != name.trim()
                || name != name.toUpperCase() || name.containsAnyOf("\r\n")
                || ! decoded.names.emplace(id, name).second)
                return false;
        }
    }
    if (tree.hasProperty(rootBypassStateKey))
    {
        const auto bypass = tree.getProperty(rootBypassStateKey).toString();
        if (bypass != "0" && bypass != "1")
            return false;
        decoded.rootBypassed = bypass == "1";
    }
    groupLooseInstances(decoded);
    result = std::move(decoded);
    return true;
}

void replaceIncoming(State& state, const int from, const int to)
{
    if (state.entryInstanceId == from) state.entryInstanceId = to;
    for (auto& node : state.nodes)
        for (auto link : { &State::Node::serialNext, &State::Node::parallelNext, &State::Node::groupNext })
            if (node.*link == from) node.*link = to;
}

// In an outer group, adjacent loose instances form their own inner group.
void groupLooseInstances(State& state)
{
    std::vector<int> heads;
    for (const auto& node : state.nodes)
        if (node.parallelNext != 0 && std::none_of(state.nodes.begin(), state.nodes.end(),
            [id = node.id](const auto& parent) { return parent.parallelNext == id; }))
            heads.push_back(node.id);
    for (const auto headId : heads)
    {
        bool mixed = false;
        for (auto* node = findNode(state, headId); node != nullptr; node = findNode(state, node->parallelNext))
            mixed = mixed || node->container;
        if (! mixed) continue;
        for (int id = headId; id != 0;)
        {
            const auto* first = findNode(state, id);
            if (first->container) { id = first->parallelNext; continue; }
            auto lastId = id;
            int count = 1;
            for (auto* next = findNode(state, first->parallelNext); next != nullptr && ! next->container;
                 next = findNode(state, next->parallelNext))
            { lastId = next->id; ++count; }
            const auto nextId = findNode(state, lastId)->parallelNext;
            if (count > 1)
            {
                const auto continuation = first->groupNext;
                int wrapperId = std::numeric_limits<int>::max();
                while (findNode(state, wrapperId) != nullptr) --wrapperId;
                replaceIncoming(state, id, wrapperId);
                findNode(state, id)->groupNext = 0;
                findNode(state, lastId)->parallelNext = 0;
                state.nodes.push_back({ wrapperId, id, nextId, continuation, true });
            }
            id = nextId;
        }
    }
}

bool appendContinuation(State&, int, int);

void normalizeContainers(State& state)
{
    for (;;)
    {
        const auto found = std::find_if(state.nodes.begin(), state.nodes.end(),
            [&state](const auto& node) { return node.container && (node.serialNext == 0
                || (node.parallelNext == 0 && std::none_of(state.nodes.begin(), state.nodes.end(),
                    [id = node.id](const auto& parent) { return parent.parallelNext == id; }))); });
        if (found == state.nodes.end()) break;
        const auto node = *found;
        if (node.serialNext == 0)
        {
            const auto replacement = node.parallelNext != 0 ? node.parallelNext : node.groupNext;
            if (node.parallelNext != 0) findNode(state, node.parallelNext)->groupNext = node.groupNext;
            replaceIncoming(state, node.id, replacement);
        }
        else
        {
            if (node.groupNext != 0) appendContinuation(state, node.serialNext, node.groupNext);
            replaceIncoming(state, node.id, node.serialNext);
        }
        state.nodes.erase(std::remove_if(state.nodes.begin(), state.nodes.end(),
            [id = node.id](const auto& value) { return value.id == id; }), state.nodes.end());
    }
    if (const auto* root = findNode(state, state.rootInstanceId); root == nullptr || root->container)
    {
        auto* first = findNode(state, state.entryInstanceId);
        while (first != nullptr && first->container) first = findNode(state, first->serialNext);
        if (first != nullptr) state.rootInstanceId = first->id;
    }
}

void write(juce::ValueTree& tree, State state)
{
    normalizeContainers(state);
    juce::StringArray records;
    for (const auto& node : state.nodes)
        records.add(juce::String(node.id) + ":" + juce::String(node.serialNext)
                    + ":" + juce::String(node.parallelNext)
                    + ":" + juce::String(node.groupNext) + (node.container ? ":1" : ""));
    tree.setProperty(instancesStateKey,
                     juce::String(state.entryInstanceId) + ";" + records.joinIntoString(","), nullptr);
    tree.setProperty(nextInstanceIdStateKey, state.nextInstanceId, nullptr);
    tree.setProperty(rootInstanceIdStateKey, state.rootInstanceId, nullptr);
    if (state.rootBypassed)
        tree.setProperty(rootBypassStateKey, true, nullptr);
    else
        tree.removeProperty(rootBypassStateKey, nullptr);
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
    return getInstanceCount(state) < maximumInstanceCount
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

bool attachBefore(State& state, const int instanceId, const int targetId, const bool parallel, const bool wholeGroup = false)
{
    auto* source = findNode(state, instanceId);
    auto* target = findNode(state, targetId);
    if (source == nullptr || target == nullptr || instanceId == targetId)
        return false;
    auto attached = false;
    if (state.entryInstanceId == targetId)
    {
        state.entryInstanceId = instanceId;
        attached = true;
    }
    else
        for (auto& node : state.nodes)
            for (auto link : { &State::Node::serialNext, &State::Node::parallelNext, &State::Node::groupNext })
                if (node.*link == targetId)
                {
                    node.*link = instanceId;
                    attached = true;
                    break;
                }
    if (! attached)
        return false;
    if (wholeGroup)
    {
        source->serialNext = targetId;
        return true;
    }
    source->groupNext = target->groupNext;
    target->groupNext = 0;
    if (parallel)
        source->parallelNext = targetId;
    else
    {
        source->serialNext = targetId;
        source->parallelNext = target->parallelNext;
        target->parallelNext = 0;
    }
    return true;
}

bool detachForMove(State& state, const int id)
{
    auto* source = findNode(state, id);
    if (source == nullptr || source->container)
        return false;
    const auto serial = source->serialNext;
    const auto parallel = source->parallelNext;
    const auto group = source->groupNext;
    // Promote the remainder of this branch, rather than an adjacent branch.
    const auto replacement = serial != 0 ? serial : (parallel != 0 ? parallel : group);
    if (replacement == 0 && state.entryInstanceId == id)
        return false;
    if (serial != 0 && parallel != 0)
    {
        auto* promoted = findNode(state, serial);
        // A nested group's continuation cannot become a continuation of its
        // outer siblings without changing the signal path.
        if (promoted->groupNext != 0)
            return false;
        auto* siblingTail = promoted;
        while (siblingTail->parallelNext != 0)
            siblingTail = findNode(state, siblingTail->parallelNext);
        siblingTail->parallelNext = parallel;
        promoted->groupNext = group;
    }
    else if (serial == 0 && parallel != 0 && group != 0)
        findNode(state, parallel)->groupNext = group;
    else if (serial != 0 && group != 0 && ! appendContinuation(state, serial, group))
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
                            [id] (const auto& node) { return node.id == id && ! node.container; }))
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

    std::vector<int> ids;
    for (const auto& node : state.nodes)
        ids.push_back(node.id);
    std::sort(ids.begin(), ids.end());

    auto number = 1;
    for (const auto id : ids)
    {
        const auto name = "INS-" + juce::String(number++);
        if (id == instanceId)
            return name;
    }
    return {};
}

// Extend an existing parallel group; create a stage only for a lone instance.
static bool attachParallelAtInstance(State& state, const int id, const int targetId, const bool before)
{
    auto* target = findNode(state, targetId);
    auto* source = findNode(state, id);
    if (target == nullptr || source == nullptr || target->container || source->container || id == targetId)
        return false;
    auto groupHead = targetId;
    for (;;)
    {
        const auto parent = std::find_if(state.nodes.begin(), state.nodes.end(),
            [groupHead](const auto& node) { return node.parallelNext == groupHead; });
        if (parent == state.nodes.end()) break;
        groupHead = parent->id;
    }
    auto alreadyGrouped = findNode(state, groupHead)->parallelNext != 0;
    for (auto* member = findNode(state, groupHead); member != nullptr; member = findNode(state, member->parallelNext))
        if (member->container) alreadyGrouped = false;
    if (alreadyGrouped)
    {
        if (before) return attachBefore(state, id, targetId, true);
        source->parallelNext = target->parallelNext;
        target->parallelNext = id;
        return true;
    }
    const auto continuation = target->serialNext;
    const auto siblings = target->parallelNext;
    const auto commonContinuation = target->groupNext;
    int wrapperId = std::numeric_limits<int>::max();
    while (findNode(state, wrapperId) != nullptr) --wrapperId;
    replaceIncoming(state, targetId, wrapperId);
    target->serialNext = 0;
    target->parallelNext = before ? 0 : id;
    target->groupNext = before ? 0 : continuation;
    source->parallelNext = before ? targetId : 0;
    source->groupNext = before ? continuation : 0;
    state.nodes.push_back({ wrapperId, before ? id : targetId, siblings, commonContinuation, true });
    return true;
}

static bool insertParallelAtInstance(juce::ValueTree& tree, const int targetId, const bool before)
{
    State state;
    if (! decode(tree, state) || ! canAdd(state)) return false;
    const auto id = state.nextInstanceId++;
    state.nodes.push_back({ id, 0, 0, 0 });
    if (! attachParallelAtInstance(state, id, targetId, before)) return false;
    auto candidate = tree.createCopy();
    write(candidate, state);
    if (! isCurrentState(candidate)) return false;
    write(tree, state);
    return true;
}

static bool moveParallelAtInstance(juce::ValueTree& tree, const int id, const int targetId, const bool before)
{
    State state;
    if (id == targetId || ! decode(tree, state) || ! detachForMove(state, id)
        || ! attachParallelAtInstance(state, id, targetId, before)) return false;
    auto candidate = tree.createCopy();
    write(candidate, state);
    if (! isCurrentState(candidate)) return false;
    write(tree, state);
    return true;
}

static bool insertInstanceBefore(juce::ValueTree& tree, const int targetId, const bool parallel)
{
    State state;
    if (! decode(tree, state) || ! canAdd(state) || findNode(state, targetId) == nullptr)
        return false;
    const auto id = state.nextInstanceId++;
    state.nodes.push_back({ id, 0, 0, 0 });
    if (! attachBefore(state, id, targetId, parallel))
        return false;
    write(tree, state);
    return true;
}

bool insertSerialInstanceBefore(juce::ValueTree& tree, const int targetId)
{
    return insertInstanceBefore(tree, targetId, false);
}

bool insertParallelInstanceBefore(juce::ValueTree& tree, const int targetId)
{
    return insertParallelAtInstance(tree, targetId, true);
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
    return insertParallelAtInstance(tree, targetInstanceId, false);
}

bool insertInstanceBeforeGroup(juce::ValueTree& tree, const int firstId)
{
    State state;
    if (! decode(tree, state) || ! canAdd(state) || findNode(state, firstId) == nullptr)
        return false;
    const auto id = state.nextInstanceId++;
    state.nodes.push_back({ id, 0, 0, 0 });
    if (! attachBefore(state, id, firstId, false, true))
        return false;
    write(tree, state);
    return true;
}

static bool moveInstanceAtGroup(juce::ValueTree& tree, const int id, int firstId, const bool before)
{
    State state;
    if (! decode(tree, state) || findNode(state, firstId) == nullptr || findNode(state, id) == nullptr)
        return false;
    if (id == firstId)
    {
        const auto* first = findNode(state, firstId);
        firstId = first->serialNext != 0 ? first->serialNext : first->parallelNext;
        if (firstId == 0)
            return false;
    }
    if (! detachForMove(state, id))
        return false;
    if (before)
    {
        if (! attachBefore(state, id, firstId, false, true))
            return false;
    }
    else
    {
        auto* first = findNode(state, firstId);
        auto* source = findNode(state, id);
        source->serialNext = first->groupNext;
        first->groupNext = id;
    }
    auto candidate = tree.createCopy();
    write(candidate, state);
    if (! isCurrentState(candidate))
        return false;
    write(tree, state);
    return true;
}

bool moveInstanceBeforeGroup(juce::ValueTree& tree, const int id, const int firstId)
{
    return moveInstanceAtGroup(tree, id, firstId, true);
}

bool moveInstanceAfterGroup(juce::ValueTree& tree, const int id, const int firstId)
{
    return moveInstanceAtGroup(tree, id, firstId, false);
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

// A structural branch keeps the existing group intact inside an outer parallel group.
static bool attachParallelOutsideGroup(State& state, const int id, const int firstId, const bool before)
{
    auto* first = findNode(state, firstId);
    if (first == nullptr || id == firstId
        || std::any_of(state.nodes.begin(), state.nodes.end(),
            [firstId](const auto& node) { return node.parallelNext == firstId; })) return false;
    const auto continuation = first->groupNext;
    first->groupNext = 0;
    int wrapperId = std::numeric_limits<int>::max();
    while (findNode(state, wrapperId) != nullptr) --wrapperId;
    replaceIncoming(state, firstId, before ? id : wrapperId);
    state.nodes.push_back({ wrapperId, firstId, before ? 0 : id, before ? 0 : continuation, true });
    auto* source = findNode(state, id);
    if (before)
    {
        source->parallelNext = wrapperId;
        source->groupNext = continuation;
    }
    return true;
}

bool insertParallelOutsideGroup(juce::ValueTree& tree, const int firstId, const bool before)
{
    State state;
    if (! decode(tree, state) || ! canAdd(state)) return false;
    const auto id = state.nextInstanceId++;
    state.nodes.push_back({ id, 0, 0, 0 });
    if (! attachParallelOutsideGroup(state, id, firstId, before)) return false;
    auto candidate = tree.createCopy();
    write(candidate, state);
    if (! isCurrentState(candidate)) return false;
    write(tree, state);
    return true;
}

bool moveParallelOutsideGroup(juce::ValueTree& tree, const int id, int firstId, const bool before)
{
    State state;
    if (! decode(tree, state) || findNode(state, id) == nullptr || findNode(state, firstId) == nullptr) return false;
    if (id == firstId)
    {
        const auto* first = findNode(state, firstId);
        firstId = first->serialNext != 0 ? first->serialNext : first->parallelNext;
        if (firstId == 0) return false;
    }
    if (! detachForMove(state, id) || ! attachParallelOutsideGroup(state, id, firstId, before)) return false;
    auto candidate = tree.createCopy();
    write(candidate, state);
    if (! isCurrentState(candidate)) return false;
    write(tree, state);
    return true;
}

bool removeInstance(juce::ValueTree& tree, const int instanceId)
{
    State state;
    if (! decode(tree, state) || getInstanceCount(state) <= 1)
        return false;
    const auto* source = findNode(state, instanceId);
    if (source == nullptr || source->container)
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
    {
        state.rootInstanceId = state.entryInstanceId;
        state.rootBypassed = false;
    }
    write(tree, state);
    return true;
}

static bool moveInstanceLink(juce::ValueTree& tree,
                             const int instanceId,
                             const int targetInstanceId,
                             int State::Node::* link)
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
    source->*link = target->*link;
    target->*link = instanceId;
    auto candidate = tree.createCopy();
    write(candidate, state);
    if (! isCurrentState(candidate))
        return false;
    write(tree, state);
    return true;
}

static bool moveInstanceBefore(juce::ValueTree& tree, const int instanceId, const int targetId, const bool parallel)
{
    State state;
    if (instanceId == targetId || ! decode(tree, state) || findNode(state, targetId) == nullptr
        || ! detachForMove(state, instanceId) || ! attachBefore(state, instanceId, targetId, parallel))
        return false;
    auto candidate = tree.createCopy();
    write(candidate, state);
    if (! isCurrentState(candidate))
        return false;
    write(tree, state);
    return true;
}

bool moveInstanceSerialBefore(juce::ValueTree& tree, const int instanceId, const int targetId)
{
    return moveInstanceBefore(tree, instanceId, targetId, false);
}

bool moveInstanceParallelBefore(juce::ValueTree& tree, const int instanceId, const int targetId)
{
    return moveParallelAtInstance(tree, instanceId, targetId, true);
}

bool moveInstanceSerialAfter(juce::ValueTree& tree, const int instanceId, const int targetInstanceId)
{
    return moveInstanceLink(tree, instanceId, targetInstanceId, &State::Node::serialNext);
}

bool moveInstanceParallelTo(juce::ValueTree& tree, const int instanceId, const int targetInstanceId)
{
    return moveParallelAtInstance(tree, instanceId, targetInstanceId, false);
}

bool renameInstance(juce::ValueTree& tree, const int instanceId, const juce::String& name)
{
    State state;
    if (! decode(tree, state) || findNode(state, instanceId) == nullptr)
        return false;
    const auto normalized = name.trim().toUpperCase();
    if (normalized == getDisplayName(state, instanceId))
        return true;
    if (! isValidOscInstanceName(normalized))
        return false;
    if (normalized.startsWith("INS-")
            && normalized.substring(4).isNotEmpty()
            && normalized.substring(4).containsOnly("0123456789"))
        return false;
    for (const auto& node : state.nodes)
        if (node.id != instanceId && getDisplayName(state, node.id) == normalized)
            return false;
    state.names[instanceId] = normalized;
    write(tree, state);
    return true;
}
}
