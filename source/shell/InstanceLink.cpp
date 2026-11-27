#include "InstanceLink.h"
#include "Processor.h"
#include "Editor.h"
#include "../routing/State.h"
#include "ABCompareSerialization.h"
#include "UnitSyncDelta.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/sibil/Synchronization.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/eql/Processor.h"

#include <algorithm>
#include <cmath>
#include <compare>
#include <map>
#include <limits>
#include <mutex>
#include <set>
#include <utility>

namespace
{
struct ModuleKey
{
    int band = 0, type = 0, serial = 1;
    auto operator<=>(const ModuleKey&) const = default;
};

struct SlotInfo { int uid = -1; bool enabled = true; bool shown = false; };

struct ParameterValue
{
    int owner = 0;
    ModuleKey module;
    juce::String id;
    float normalized = 0.0f;
};

struct Snapshot
{
    std::map<juce::String, ParameterValue> values;
    std::map<ModuleKey, SlotInfo> slots;
    std::map<int, std::vector<ModuleKey>> order;
    std::map<ModuleKey, int> filterCounts;
    struct ChainOrder
    {
        ava::modules::chain::gain::Order gains;
    };
    std::map<ModuleKey, ChainOrder> chainOrders;
};

void appendParameters(Snapshot& result, juce::AudioProcessorValueTreeState& state,
                      int owner, ModuleKey key, const juce::String& prefix)
{
    for (auto* base : state.processor.getParameters())
    {
        auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(base);
        if (parameter == nullptr) continue;
        const auto id = parameter->getParameterID();
        // Host-slot *assignments* are private to each plugin instance. They
        // must not be linked as though they were ordinary audio parameters.
        if (owner == 0 && id.startsWith(AvaAudioProcessor::paramHostSlotPrefix)) continue;
        result.values.emplace(prefix + id, ParameterValue {owner, key, id, parameter->getValue()});
    }
}
}

// The access shim is the only part of LINK allowed to inspect processor-owned
// module stores. It never exposes an internal pointer to the UI or registry.
class InstanceLinkAccess
{
public:
    static Snapshot capture(AvaAudioProcessor&);
    static juce::ValueTree captureUnit(AvaAudioProcessor&);
    static bool applyUnit(AvaAudioProcessor&, const juce::ValueTree&);
    static juce::AudioProcessorValueTreeState* destinationState(
        AvaAudioProcessor&, const ParameterValue&, const std::map<ModuleKey, int>&);
    static EqlModuleProcessor* destinationEql(
        AvaAudioProcessor&, const ModuleKey&, const std::map<ModuleKey, int>&);
};

namespace
{
constexpr const char* unitSyncProperties[] {
    AvaAudioProcessor::linkUuidStateKey, AvaAudioProcessor::linkCountStateKey,
    AvaAudioProcessor::linkTargetsStateKey, AvaAudioProcessor::linkSendStateKey,
    AvaAudioProcessor::linkReceiveStateKey, AvaAudioProcessor::linkPausedStateKey,
    AvaAudioProcessor::linkUnitNumberStateKey,
    "ava.editor.ui.collapsed.shell.unit.sync.sync"
};

int readUnitNumber(const juce::ValueTree& state)
{
    return juce::jlimit(1, InstanceLinkRegistry::maxUnitNumber,
                        int(state.getProperty(AvaAudioProcessor::linkUnitNumberStateKey, 1)));
}

bool canTransfer(const juce::ValueTree& state, const char* direction)
{
    return bool(state.getProperty(direction, false))
        && !bool(state.getProperty(AvaAudioProcessor::linkPausedStateKey, false));
}
}

juce::ValueTree InstanceLinkAccess::captureUnit(AvaAudioProcessor& processor)
{
    juce::MemoryBlock data;
    processor.getStateInformation(data);
    const auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize()));
    if (xml == nullptr) return {};
    auto state = juce::ValueTree::fromXml(*xml);
    for (const auto* key : unitSyncProperties) state.removeProperty(key, nullptr);
    shell_state_serialization::removeABCompareStateProperties(state);
    return state;
}

bool InstanceLinkAccess::applyUnit(AvaAudioProcessor& target, const juce::ValueTree& state)
{
    const auto xml = state.createXml();
    if (xml == nullptr) return false;
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(*xml, data);
    return target.restoreStateInformation(data.getData(), static_cast<int>(data.getSize()), false, false);
}

namespace
{
using NodeMap = std::map<int, int>;
using RoutingState = ava::routing::State;

NodeMap matchRouting(const RoutingState& from, const RoutingState& to)
{
    NodeMap result {{from.rootInstanceId, to.rootInstanceId}};
    NodeMap byPosition;
    const auto match = [&](const auto& self, int source, int target) -> bool
    {
        if (source == 0 || target == 0) return source == target;
        const auto* a = ava::routing::findStateNode(from.nodes, source);
        const auto* b = ava::routing::findStateNode(to.nodes, target);
        if (a == nullptr || b == nullptr || a->container != b->container) return false;
        byPosition.emplace(source, target);
        return self(self, a->serialNext, b->serialNext)
            && self(self, a->parallelNext, b->parallelNext)
            && self(self, a->groupNext, b->groupNext);
    };
    if (match(match, from.entryInstanceId, to.entryInstanceId)
        && byPosition.at(from.rootInstanceId) == to.rootInstanceId)
        return byPosition;

    // A name is a useful identity across differently arranged units only
    // when it identifies exactly one node in each of them.
    for (const auto& [id, name] : from.names)
    {
        if (id == from.rootInstanceId
            || std::count_if(from.names.begin(), from.names.end(),
                [&](const auto& node) { return node.second == name; }) != 1)
            continue;
        int candidate = 0;
        for (const auto& [local, localName] : to.names)
            if (localName == name)
            {
                if (candidate != 0) { candidate = 0; break; }
                candidate = local;
            }
        if (candidate != 0 && candidate != to.rootInstanceId) result.emplace(id, candidate);
    }
    return result;
}

int nodeOnTarget(const NodeMap& mapping, const RoutingState& from, const RoutingState& to, int id)
{
    if (id == 0) return 0;
    if (const auto found = mapping.find(id); found != mapping.end()
        && ava::routing::findStateNode(to.nodes, found->second) != nullptr) return found->second;
    const auto* node = ava::routing::findStateNode(from.nodes, id);
    if (node == nullptr || !node->container) return 0;
    const auto child = nodeOnTarget(mapping, from, to, node->serialNext);
    for (const auto& local : to.nodes)
        if (local.container && local.serialNext == child) return local.id;
    return 0;
}

int findNamedInstance(const RoutingState& state, const juce::String& name)
{
    if (name.isEmpty()) return 0;
    int result = 0;
    for (const auto& node : state.nodes)
        if (!node.container && ava::routing::getDisplayName(state, node.id) == name)
        {
            if (result != 0) return 0;
            result = node.id;
        }
    return result;
}

int namedRoutingTarget(const RoutingState& from, const RoutingState& to, int id)
{
    const auto* node = ava::routing::findStateNode(from.nodes, id);
    if (node == nullptr) return 0;
    if (!node->container)
        return findNamedInstance(to, ava::routing::getDisplayName(from, id));
    const auto child = namedRoutingTarget(from, to, node->serialNext);
    for (const auto& candidate : to.nodes)
        if (candidate.container && candidate.serialNext == child) return candidate.id;
    return 0;
}

juce::ValueTree routingProcessors(const juce::ValueTree& unit)
{
    const auto xml = juce::XmlDocument::parse(unit.getProperty(ava::routing::processorStatesKey).toString());
    return xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree("routing_processors");
}

bool isLocalUnitProperty(const juce::Identifier& key)
{
    const auto name = key.toString();
    return name.startsWith("ava.routing.") || name.startsWith("ava.link.")
        || name.startsWith("ava.host_slot.") || name.startsWith("ava.editor.ui.collapsed.shell.unit.sync.");
}

juce::ValueTree processorSettings(const juce::ValueTree& unit, const juce::ValueTree& children, int id, int root)
{
    auto state = id == root ? unit.createCopy() : ava::unit_sync::decode(
        children.getChildWithProperty("id", juce::String(id)).getProperty("state").toString());
    if (!state.isValid()) return {};
    for (int i = state.getNumProperties(); --i >= 0;)
        if (const auto name = state.getPropertyName(i); isLocalUnitProperty(name))
            state.removeProperty(name, nullptr);
    shell_state_serialization::removeABCompareStateProperties(state);
    for (int i = state.getNumChildren(); --i >= 0;)
        if (state.getChild(i).getProperty("id").toString().startsWith(AvaAudioProcessor::paramHostSlotPrefix))
            state.removeChild(i, nullptr);
    return state;
}

bool mergeUnit(const juce::ValueTree& before, const juce::ValueTree& after,
               juce::ValueTree& target, const NodeMap& mapping)
{
    const auto previous = ava::routing::readState(before);
    const auto fresh = ava::routing::readState(after);
    const auto local = ava::routing::readState(target);
    const auto oldChildren = routingProcessors(before);
    const auto newChildren = routingProcessors(after);
    auto localChildren = routingProcessors(target);
    bool changed = false;
    bool childrenChanged = false;
    for (const auto& [id, dest] : mapping)
    {
        const auto* oldNode = ava::routing::findStateNode(previous.nodes, id);
        const auto* nextNode = ava::routing::findStateNode(fresh.nodes, id);
        const auto* localNode = ava::routing::findStateNode(local.nodes, dest);
        if (oldNode == nullptr || nextNode == nullptr || localNode == nullptr
            || oldNode->container || nextNode->container || localNode->container) continue;

        const auto oldState = processorSettings(before, oldChildren, id, previous.rootInstanceId);
        const auto newState = processorSettings(after, newChildren, id, fresh.rootInstanceId);
        if (dest == local.rootInstanceId)
            changed |= ava::unit_sync::merge(oldState, newState, target);
        else
        {
            auto item = localChildren.getChildWithProperty("id", juce::String(dest));
            if (!item.isValid()) continue;
            auto state = ava::unit_sync::decode(item.getProperty("state").toString());
            if (ava::unit_sync::merge(oldState, newState, state))
            {
                item.setProperty("state", ava::unit_sync::encode(state), nullptr);
                childrenChanged = true;
            }
        }
    }
    if (childrenChanged)
        if (const auto xml = localChildren.createXml())
        {
            target.setProperty(ava::routing::processorStatesKey, xml->toString(), nullptr);
            changed = true;
        }
    return changed;
}
}

Snapshot InstanceLinkAccess::capture(AvaAudioProcessor& p)
{
    Snapshot snapshot;
    // Processing and module-copy mutations take this same lock. Capture a
    // coherent set of parameter owners without keeping stale pointers.
    {
        const juce::ScopedLock lock(p.processingLock);
    appendParameters(snapshot, p.parameters, 0, {}, "S|");
    if (p.blockChainModuleProcessor)
        appendParameters(snapshot, p.blockChainModuleProcessor->getValueTreeState(), 1, {}, "C|");
    if (p.dynModuleProcessor)
        appendParameters(snapshot, p.dynModuleProcessor->getValueTreeState(), 2, {}, "D|");

    const auto bandCount = size_t(p.getCrossoverSettings().activeSplitCount + 1);
    if (p.blockChainModuleProcessor)
        for (size_t band = 0; band < bandCount; ++band)
            snapshot.chainOrders.emplace(ModuleKey {int(band), -1, 1}, Snapshot::ChainOrder {
                p.blockChainModuleProcessor->getGainOrder(band)});
    if (p.eqlProcessorBank)
        for (size_t band = 0; band < bandCount; ++band)
            if (auto* eql = p.eqlProcessorBank->getProcessor(band))
            {
                ModuleKey key {int(band), module_order::eql, 1};
                snapshot.filterCounts.emplace(key, eql->getActiveFilterCount());
                appendParameters(snapshot, eql->getValueTreeState(), 3, key,
                                 "E|" + juce::String(int(band)) + "|");
            }

    for (size_t band = 0; band < bandCount && band < p.moduleStacks.size(); ++band)
        for (const auto& slot : p.moduleStacks[band])
        {
            ModuleKey key {int(band), slot.type, slot.created};
            snapshot.order[int(band)].push_back(key);
            snapshot.slots.emplace(key, SlotInfo {slot.uid, slot.enabled, slot.shown});
        }

    for (const auto& [uid, copy] : p.moduleCopies)
    {
        juce::ignoreUnused(uid);
        if (copy == nullptr) continue;
        ModuleKey key {copy->range, copy->type, copy->serial};
        const auto prefix = "X|" + juce::String(key.band) + "|" + juce::String(key.type)
                          + "|" + juce::String(key.serial) + "|";
        if (copy->blocks)
        {
            appendParameters(snapshot, copy->blocks->getValueTreeState(), 4, key, prefix);
            snapshot.chainOrders.emplace(key, Snapshot::ChainOrder {
                copy->blocks->getGainOrder(size_t(key.band))});
        }
        if (copy->dyn)
            appendParameters(snapshot, copy->dyn->getValueTreeState(), 5, key, prefix);
        if (copy->eql)
        {
            if (auto* eql = copy->eql->getProcessor(size_t(key.band)))
            {
                snapshot.filterCounts.emplace(key, eql->getActiveFilterCount());
                appendParameters(snapshot, eql->getValueTreeState(), 6, key, prefix);
            }
        }
    }
    } // Release DSP/module lock before accessing routing handles and editor UI.
    return snapshot;
}

juce::AudioProcessorValueTreeState* InstanceLinkAccess::destinationState(AvaAudioProcessor& p,
                                                       const ParameterValue& value,
                                                       const std::map<ModuleKey, int>& mappings)
{
    switch (value.owner)
    {
        case 0: return &p.parameters;
        case 1: return p.blockChainModuleProcessor ? &p.blockChainModuleProcessor->getValueTreeState() : nullptr;
        case 2: return p.dynModuleProcessor ? &p.dynModuleProcessor->getValueTreeState() : nullptr;
        case 3:
            if (p.eqlProcessorBank)
                if (auto* eql = p.eqlProcessorBank->getProcessor(size_t(value.module.band)))
                    return &eql->getValueTreeState();
            return nullptr;
        case 4: case 5: case 6:
        {
            const auto it = mappings.find(value.module);
            if (it == mappings.end()) return nullptr;
            const auto copy = p.moduleCopies.find(it->second);
            if (copy == p.moduleCopies.end() || !copy->second) return nullptr;
            if (value.owner == 4 && copy->second->blocks)
                return &copy->second->blocks->getValueTreeState();
            if (value.owner == 5 && copy->second->dyn)
                return &copy->second->dyn->getValueTreeState();
            if (value.owner == 6 && copy->second->eql)
                if (auto* eql = copy->second->eql->getProcessor(size_t(value.module.band)))
                    return &eql->getValueTreeState();
            return nullptr;
        }
        default: return nullptr;
    }
}

EqlModuleProcessor* InstanceLinkAccess::destinationEql(AvaAudioProcessor& p, const ModuleKey& key,
                                  const std::map<ModuleKey, int>& mappings)
{
    if (key.serial == 1)
        return p.eqlProcessorBank ? p.eqlProcessorBank->getProcessor(size_t(key.band)) : nullptr;
    const auto it = mappings.find(key);
    if (it == mappings.end()) return nullptr;
    const auto copy = p.moduleCopies.find(it->second);
    if (copy == p.moduleCopies.end() || !copy->second || !copy->second->eql) return nullptr;
    return copy->second->eql->getProcessor(size_t(key.band));
}

struct InstanceLinkRegistry::Impl
{
    // The processor destructor detaches before touching its owned state.
    // Protect that interval without holding the registry lock while acquiring
    // routing/runtime handles or processor locks.
    struct RootLifetime
    {
        std::mutex mutex;
        AvaAudioProcessor* processor = nullptr;
    };
    struct Entry
    {
        AvaAudioProcessor* processor = nullptr;
        std::shared_ptr<RootLifetime> lifetime;
        uint64_t observedRevision = 0;
        uint64_t observedViewRevision = 0;
        int quietTicks = 0;
        Activity activity;
        // Only nested routing nodes are synthetic entries. The root UNIT
        // uses its existing registry ID for both external and internal links.
        bool internal = false;
        uint64_t rootSessionId = 0;
        int routingNodeId = 0;
        std::shared_ptr<AvaAudioProcessor> routingHandle;
        struct Destination
        {
            std::shared_ptr<const Snapshot> baseline;
            std::map<ModuleKey, int> mapping;
            // Each source filter index maps to its own position in the
            // recipient. Existing, unmatched recipient filters are untouched.
            std::map<ModuleKey, std::map<int, int>> eqlFilterIndices;
            juce::ValueTree unitBaseline;
            NodeMap nodes;
            struct PendingRoutingEdit
            {
                RoutingEdit edit;
                RoutingState before;
                int first = 0, second = 0, added = 0;
                bool removed = false;
                juce::String addedName;
            };
            std::vector<PendingRoutingEdit> routingEdits;
        };
        std::map<uint64_t, Destination> linked;
    };
    mutable std::mutex mutex;
    uint64_t nextId = 1;
    uint64_t nextInternalId = uint64_t(1) << 62; // Never consume UNIT numbers.
    std::map<uint64_t, Entry> entries;
};

InstanceLinkRegistry::InstanceLinkRegistry() : impl(std::make_unique<Impl>()) {}

InstanceLinkRegistry::~InstanceLinkRegistry()
{
    stopTimer();
}

void InstanceLinkRegistry::timerCallback()
{
    std::vector<std::shared_ptr<Impl::RootLifetime>> roots;
    {
        std::lock_guard lock(impl->mutex);
        for (const auto& [id, entry] : impl->entries)
        {
            juce::ignoreUnused(id);
            if (!entry.internal && entry.lifetime) roots.push_back(entry.lifetime);
        }
    }
    for (const auto& root : roots)
    {
        std::lock_guard lifetimeLock(root->mutex);
        if (root->processor != nullptr) pollRouting(*root->processor);
    }
}

InstanceLinkRegistry& InstanceLinkRegistry::instance()
{
    static InstanceLinkRegistry registry;
    return registry;
}

uint64_t InstanceLinkRegistry::attach(AvaAudioProcessor& processor)
{
    std::lock_guard lock(impl->mutex);
    const auto id = impl->nextId++;
    Impl::Entry entry;
    entry.processor = &processor;
    entry.lifetime = std::make_shared<Impl::RootLifetime>();
    entry.lifetime->processor = &processor;
    impl->entries.emplace(id, std::move(entry));
    if (!isTimerRunning()) startTimerHz(60);
    return id;
}

void InstanceLinkRegistry::detach(uint64_t id)
{
    if (!impl || id == 0) return;
    std::shared_ptr<Impl::RootLifetime> lifetime;
    {
        std::lock_guard lock(impl->mutex);
        const auto it = impl->entries.find(id);
        if (it == impl->entries.end()) return;
        lifetime = it->second.lifetime;
    }
    std::unique_lock<std::mutex> lifetimeLock;
    if (lifetime) lifetimeLock = std::unique_lock(lifetime->mutex);
    std::lock_guard lock(impl->mutex);
    if (lifetime) lifetime->processor = nullptr;
    impl->entries.erase(id);
    ava::modules::sibil::forgetRouting(id);
    for (auto it = impl->entries.begin(); it != impl->entries.end();)
        if (it->second.internal && it->second.rootSessionId == id)
            it = impl->entries.erase(it);
        else ++it;
    for (auto& [peerId, peer] : impl->entries)
    {
        juce::ignoreUnused(peerId);
        peer.linked.erase(id);
    }
    if (impl->entries.empty()) stopTimer();
}

std::vector<InstanceLinkRegistry::Peer> InstanceLinkRegistry::peers(uint64_t source)
{
    std::vector<Peer> result;
    if (!impl) return result;
    std::lock_guard lock(impl->mutex);
    reconcileSavedLinks();
    const auto sender = impl->entries.find(source);
    if (sender == impl->entries.end()) return result;
    for (const auto& [id, entry] : impl->entries)
        if (id != source && !entry.internal)
        {
            const auto& state = entry.processor->getValueTreeState().state;
            result.push_back({id, formatUnitNumber(readUnitNumber(state)), sender->second.linked.contains(id),
                state.getProperty(AvaAudioProcessor::linkUuidStateKey).toString()});
        }
    return result;
}

namespace
{
juce::StringArray readUnitLinkTargets(const juce::ValueTree& state)
{
    const auto text = state.getProperty(AvaAudioProcessor::linkTargetsStateKey).toString();
    juce::StringArray targets;
    int start = 0;
    for (;;)
    {
        const auto end = text.indexOfChar(start, ';');
        targets.add(end < 0 ? text.substring(start) : text.substring(start, end));
        if (end < 0) break;
        start = end + 1;
    }
    return targets;
}
int readUnitLinkCount(const juce::ValueTree& state)
{
    return juce::jlimit(1, InstanceLinkRegistry::maxUnitLinks,
        int(state.getProperty(AvaAudioProcessor::linkCountStateKey, readUnitLinkTargets(state).size())));
}
juce::StringArray sizedUnitLinkTargets(const juce::ValueTree& state, int count)
{
    auto targets = readUnitLinkTargets(state);
    while (targets.size() < count) targets.add(juce::String());
    if (targets.size() > count) targets.removeRange(count, targets.size() - count);
    return targets;
}
}

int InstanceLinkRegistry::unitLinkCount(uint64_t source)
{
    if (!impl) return 1;
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(source);
    return it == impl->entries.end() ? 1 : readUnitLinkCount(it->second.processor->getValueTreeState().state);
}

juce::StringArray InstanceLinkRegistry::unitLinkTargets(uint64_t source)
{
    if (!impl) return { juce::String() };
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(source);
    if (it == impl->entries.end()) return { juce::String() };
    const auto& state = it->second.processor->getValueTreeState().state;
    return sizedUnitLinkTargets(state, readUnitLinkCount(state));
}

void InstanceLinkRegistry::setUnitLinkCount(uint64_t source, int count)
{
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(source);
    if (it == impl->entries.end() || it->second.internal) return;
    auto& state = it->second.processor->getValueTreeState().state;
    count = juce::jlimit(1, maxUnitLinks, count);
    const auto targets = sizedUnitLinkTargets(state, count);
    state.setProperty(AvaAudioProcessor::linkCountStateKey, count, nullptr);
    state.setProperty(AvaAudioProcessor::linkTargetsStateKey, targets.joinIntoString(";"), nullptr);
    reconcileSavedLinks();
}

bool InstanceLinkRegistry::setUnitLinkTarget(uint64_t source, int index, uint64_t target)
{
    if (!impl || source == target) return false;
    std::lock_guard lock(impl->mutex);
    reconcileSavedLinks();
    const auto from = impl->entries.find(source);
    if (from == impl->entries.end() || from->second.internal) return false;
    auto& state = from->second.processor->getValueTreeState().state;
    const auto count = readUnitLinkCount(state);
    if (index < 0 || index >= count) return false;
    juce::String uuid;
    if (target != 0)
    {
        const auto to = impl->entries.find(target);
        if (to == impl->entries.end() || to->second.internal) return false;
        uuid = to->second.processor->getValueTreeState().state
            .getProperty(AvaAudioProcessor::linkUuidStateKey).toString();
    }
    auto targets = sizedUnitLinkTargets(state, count);
    for (int row = 0; row < count; ++row)
        if (row != index && uuid.isNotEmpty() && targets[row] == uuid) return false;
    targets.set(index, uuid);
    state.setProperty(AvaAudioProcessor::linkCountStateKey, count, nullptr);
    state.setProperty(AvaAudioProcessor::linkTargetsStateKey, targets.joinIntoString(";"), nullptr);
    reconcileSavedLinks();
    return true;
}

bool InstanceLinkRegistry::linkDraggedUnit(uint64_t source, uint64_t target)
{
    if (!impl || source == target) return false;
    std::lock_guard lock(impl->mutex);
    reconcileSavedLinks();
    const auto from = impl->entries.find(source), to = impl->entries.find(target);
    if (from == impl->entries.end() || to == impl->entries.end() || from->second.internal || to->second.internal)
        return false;
    auto& senderState = from->second.processor->parameters.state;
    auto& receiverState = to->second.processor->parameters.state;
    const auto uuid = receiverState.getProperty(AvaAudioProcessor::linkUuidStateKey).toString();
    auto targets = sizedUnitLinkTargets(senderState, readUnitLinkCount(senderState));
    if (!targets.contains(uuid))
    {
        const auto free = targets.indexOf(juce::String());
        if (free >= 0) targets.set(free, uuid);
        else if (targets.size() < maxUnitLinks) targets.add(uuid);
        else return false;
        senderState.setProperty(AvaAudioProcessor::linkCountStateKey, targets.size(), nullptr);
        senderState.setProperty(AvaAudioProcessor::linkTargetsStateKey, targets.joinIntoString(";"), nullptr);
    }
    const bool newSend = !bool(senderState.getProperty(AvaAudioProcessor::linkSendStateKey, false));
    const bool newReceive = !bool(receiverState.getProperty(AvaAudioProcessor::linkReceiveStateKey, false));
    senderState.setProperty(AvaAudioProcessor::linkSendStateKey, true, nullptr);
    receiverState.setProperty(AvaAudioProcessor::linkReceiveStateKey, true, nullptr);
    reconcileSavedLinks();
    if (newSend || newReceive)
        for (auto& [id, entry] : impl->entries)
        {
            if (entry.internal) continue;
            for (auto& [destination, relation] : entry.linked)
                if ((newSend && id == source) || (newReceive && destination == target))
                    if (const auto peer = impl->entries.find(destination);
                        peer != impl->entries.end() && !peer->second.internal)
                    {
                        relation.unitBaseline = InstanceLinkAccess::captureUnit(*entry.processor);
                        relation.routingEdits.clear();
                    }
        }
    return true;
}

bool InstanceLinkRegistry::editRouting(AvaAudioProcessor& processor, RoutingEdit edit, int first, int second)
{
    auto& root = processor.getRoutingOwner();
    auto& tree = root.getValueTreeState().state;
    const auto before = ava::routing::readState(tree);
    if (!edit(tree, first, second)) return false;
    const auto after = ava::routing::readState(tree);
    const auto added = after.nextInstanceId != before.nextInstanceId ? before.nextInstanceId : 0;
    const bool removed = ava::routing::findStateNode(after.nodes, first) == nullptr;
    const auto addedName = added != 0 ? ava::routing::getDisplayName(after, added) : juce::String {};
    std::lock_guard lock(impl->mutex);
    const auto entry = impl->entries.find(root.getLinkSessionId());
    if (entry != impl->entries.end() && canTransfer(tree, AvaAudioProcessor::linkSendStateKey))
        for (auto& [id, relation] : entry->second.linked)
            if (const auto target = impl->entries.find(id); target != impl->entries.end() && !target->second.internal
                && canTransfer(target->second.processor->parameters.state, AvaAudioProcessor::linkReceiveStateKey))
                relation.routingEdits.push_back({edit, before, first, second, added, removed, addedName});
    return true;
}

bool InstanceLinkRegistry::isSendEnabled(uint64_t unit)
{
    if (!impl) return false;
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(unit);
    return it != impl->entries.end() && !it->second.internal
        && bool(it->second.processor->getValueTreeState().state
            .getProperty(AvaAudioProcessor::linkSendStateKey, false));
}

void InstanceLinkRegistry::setSendEnabled(uint64_t unit, bool enabled)
{
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    reconcileSavedLinks();
    const auto it = impl->entries.find(unit);
    if (it == impl->entries.end() || it->second.internal) return;
    auto& entry = it->second;
    auto& state = entry.processor->getValueTreeState().state;
    if (bool(state.getProperty(AvaAudioProcessor::linkSendStateKey, false)) == enabled) return;
    state.setProperty(AvaAudioProcessor::linkSendStateKey, enabled, nullptr);
    if (enabled)
        for (auto& [id, relation] : entry.linked)
            if (const auto receiver = impl->entries.find(id);
                receiver != impl->entries.end() && !receiver->second.internal)
            {
                relation.unitBaseline = InstanceLinkAccess::captureUnit(*entry.processor);
                relation.routingEdits.clear();
            }
}

InstanceLinkRegistry::Activity InstanceLinkRegistry::activity(uint64_t unit)
{
    if (!impl) return {};
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(unit);
    return it != impl->entries.end() ? it->second.activity : Activity {};
}

bool InstanceLinkRegistry::isReceiveEnabled(uint64_t unit)
{
    if (!impl) return false;
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(unit);
    return it != impl->entries.end() && !it->second.internal
        && bool(it->second.processor->getValueTreeState().state
            .getProperty(AvaAudioProcessor::linkReceiveStateKey, false));
}

void InstanceLinkRegistry::setReceiveEnabled(uint64_t unit, bool enabled)
{
    if (!impl) return;
    std::lock_guard lock(impl->mutex);
    reconcileSavedLinks();
    const auto it = impl->entries.find(unit);
    if (it == impl->entries.end() || it->second.internal) return;
    auto& state = it->second.processor->getValueTreeState().state;
    if (bool(state.getProperty(AvaAudioProcessor::linkReceiveStateKey, false)) == enabled) return;
    state.setProperty(AvaAudioProcessor::linkReceiveStateKey, enabled, nullptr);
    if (enabled)
        for (auto& [source, sender] : impl->entries)
            if (!sender.internal)
                if (const auto relation = sender.linked.find(unit); relation != sender.linked.end())
                {
                    relation->second.unitBaseline = InstanceLinkAccess::captureUnit(*sender.processor);
                    relation->second.routingEdits.clear();
                }
}

int InstanceLinkRegistry::unitNumber(uint64_t unit)
{
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(unit);
    return it != impl->entries.end() && !it->second.internal
        ? readUnitNumber(it->second.processor->parameters.state) : 1;
}

juce::String InstanceLinkRegistry::formatUnitNumber(int number)
{
    return juce::String(number).paddedLeft('0', 2);
}

void InstanceLinkRegistry::setUnitNumber(uint64_t unit, int number)
{
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(unit);
    if (it == impl->entries.end() || it->second.internal) return;
    auto& state = it->second.processor->parameters.state;
    const auto next = juce::jlimit(1, maxUnitNumber, number);
    if (readUnitNumber(state) != next)
        state.setProperty(AvaAudioProcessor::linkUnitNumberStateKey, next, nullptr);
}

bool InstanceLinkRegistry::isPaused(uint64_t unit)
{
    std::lock_guard lock(impl->mutex);
    const auto it = impl->entries.find(unit);
    return it != impl->entries.end() && !it->second.internal
        && bool(it->second.processor->parameters.state.getProperty(AvaAudioProcessor::linkPausedStateKey, false));
}

void InstanceLinkRegistry::setPaused(uint64_t unit, bool paused)
{
    std::lock_guard lock(impl->mutex);
    reconcileSavedLinks();
    const auto it = impl->entries.find(unit);
    if (it == impl->entries.end() || it->second.internal) return;
    auto& state = it->second.processor->parameters.state;
    if (bool(state.getProperty(AvaAudioProcessor::linkPausedStateKey, false)) == paused) return;
    state.setProperty(AvaAudioProcessor::linkPausedStateKey, paused, nullptr);
    it->second.activity = {};
    // Drop changes across both sides of the pause boundary, including edits
    // made between the last registry tick and an immediate resume.
    for (auto& [source, entry] : impl->entries)
    {
        if (entry.internal) continue;
        juce::ValueTree baseline;
        for (auto& [destination, relation] : entry.linked)
        {
            if (source != unit && destination != unit) continue;
            const auto peer = impl->entries.find(destination);
            if (peer == impl->entries.end() || peer->second.internal) continue;
            if (!baseline.isValid()) baseline = InstanceLinkAccess::captureUnit(*entry.processor);
            relation.unitBaseline = baseline;
            relation.routingEdits.clear();
        }
    }
}

void InstanceLinkRegistry::linkUnlocked(uint64_t source, uint64_t target)
{
    auto from = impl->entries.find(source), to = impl->entries.find(target);
    if (from == impl->entries.end() || to == impl->entries.end()
        || from->second.linked.contains(target)) return;
    if (!from->second.internal && !to->second.internal)
    {
        auto& relation = from->second.linked[target];
        relation.unitBaseline = InstanceLinkAccess::captureUnit(*from->second.processor);
        relation.nodes = matchRouting(ava::routing::readState(from->second.processor->parameters.state),
                                      ava::routing::readState(to->second.processor->parameters.state));
        return;
    }
    // Baseline once when the destination becomes available, so reloading a
    // project never replays stale values from the sender's loaded preset.
    const auto baselineTarget = InstanceLinkAccess::capture(*to->second.processor);
    Impl::Entry::Destination destination;
    destination.baseline = std::make_shared<Snapshot>(InstanceLinkAccess::capture(*from->second.processor));
    for (const auto& [key, slot] : destination.baseline->slots)
        if (const auto it = baselineTarget.slots.find(key); it != baselineTarget.slots.end())
            destination.mapping.emplace(key, it->second.uid);
    for (const auto& [key, count] : destination.baseline->filterCounts)
        if (auto* eql = InstanceLinkAccess::destinationEql(*to->second.processor, key, destination.mapping))
            for (int index = 0; index < std::min(count, eql->getActiveFilterCount()); ++index)
                destination.eqlFilterIndices[key][index] = index;
    from->second.linked[target] = std::move(destination);
}

void InstanceLinkRegistry::reconcileSavedLinks()
{
    if (!impl) return;
    std::map<juce::String, uint64_t> byUuid;
    for (auto& [id, entry] : impl->entries)
    {
        if (entry.internal) continue; // Only host UNITs carry UUID identities.
        auto& state = entry.processor->getValueTreeState().state;
        auto uuid = state.getProperty(AvaAudioProcessor::linkUuidStateKey).toString();
        if (uuid.isEmpty() || byUuid.contains(uuid))
        {
            // Host track duplication can clone the entire plugin state.
            // Never let two live AVAs share the same routing identity.
            uuid = juce::Uuid().toString();
            state.setProperty(AvaAudioProcessor::linkUuidStateKey, uuid, nullptr);
            state.removeProperty(AvaAudioProcessor::linkTargetsStateKey, nullptr);
        }
        byUuid[uuid] = id;
    }

    for (auto& [id, entry] : impl->entries)
    {
        if (entry.internal) continue;
        auto& state = entry.processor->getValueTreeState().state;
        const auto uuids = sizedUnitLinkTargets(state, readUnitLinkCount(state));
        std::set<uint64_t> desired;
        for (const auto& uuid : uuids)
            if (const auto peer = byUuid.find(uuid.trim()); peer != byUuid.end() && peer->second != id)
                desired.insert(peer->second);
        for (auto it = entry.linked.begin(); it != entry.linked.end();)
        {
            const auto receiver = impl->entries.find(it->first);
            if (receiver != impl->entries.end() && receiver->second.internal)
                ++it;
            else if (!desired.contains(it->first)) it = entry.linked.erase(it);
            else ++it;
        }
        for (auto target : desired)
            linkUnlocked(id, target);
    }
}

namespace
{
juce::Identifier routingLinkKey(int sourceNodeId)
{
    return juce::Identifier("ava.link.routing." + juce::String(sourceNodeId));
}

juce::StringArray readRoutingTargets(const juce::ValueTree& state, int sourceNodeId)
{
    return juce::StringArray::fromTokens(
        state.getProperty(routingLinkKey(sourceNodeId)).toString(), ";", "");
}
}

void InstanceLinkRegistry::refreshRoutingEntries(AvaAudioProcessor& root)
{
    if (!impl || root.getLinkSessionId() == 0) return;
    // Acquire processor handles outside the registry lock. Runtime nodes may
    // be replaced when the routing graph changes or a project is restored.
    const auto topology = ava::routing::readState(root.getValueTreeState().state);
    std::map<int, std::shared_ptr<AvaAudioProcessor>> live;
    for (const auto& node : topology.nodes)
        if (!node.container && node.id != topology.rootInstanceId)
            if (auto handle = root.getRoutingInstanceHandle(node.id))
                live.emplace(node.id, std::move(handle));

    const auto rootId = root.getLinkSessionId();
    std::lock_guard lock(impl->mutex);
    if (!impl->entries.contains(rootId)) return;
    std::map<int, uint64_t> ids { {topology.rootInstanceId, rootId} };
    std::set<uint64_t> removed;
    for (auto it = impl->entries.begin(); it != impl->entries.end();)
    {
        auto& entry = it->second;
        if (entry.internal && entry.rootSessionId == rootId)
        {
            const auto found = live.find(entry.routingNodeId);
            if (found == live.end() || found->second.get() != entry.processor)
            {
                removed.insert(it->first);
                it = impl->entries.erase(it);
                continue;
            }
            ids.emplace(entry.routingNodeId, it->first);
        }
        ++it;
    }
    if (!removed.empty())
        for (auto& [id, entry] : impl->entries)
        {
            juce::ignoreUnused(id);
            for (auto removedId : removed) entry.linked.erase(removedId);
        }
    for (const auto& [nodeId, handle] : live)
    {
        if (ids.contains(nodeId)) continue;
        const auto id = impl->nextInternalId++;
        Impl::Entry entry;
        entry.processor = handle.get();
        entry.routingHandle = handle;
        entry.rootSessionId = rootId;
        entry.routingNodeId = nodeId;
        entry.internal = true;
        entry.observedRevision = handle->getStateRevision();
        entry.observedViewRevision = handle->getRoutingOwner().viewStateRevision.load(std::memory_order_relaxed);
        impl->entries.emplace(id, std::move(entry));
        ids[nodeId] = id;
    }

    // State is per SOURCE node and contains stable DESTINATION node IDs,
    // never transient registry IDs. Reopening the editor does not copy any
    // existing parameter values into recipients.
    const auto& state = root.getValueTreeState().state;
    for (const auto& [sourceNode, sourceId] : ids)
    {
        const auto source = impl->entries.find(sourceId);
        if (source == impl->entries.end()) continue;
        std::set<uint64_t> desired;
        for (const auto& token : readRoutingTargets(state, sourceNode))
        {
            const auto nodeId = token.getIntValue();
            if (token != juce::String(nodeId) || nodeId == sourceNode) continue;
            const auto destination = ids.find(nodeId);
            if (destination != ids.end()) desired.insert(destination->second);
        }
        // Preserve the separately configured external UNIT recipients.
        for (auto it = source->second.linked.begin(); it != source->second.linked.end();)
        {
            const auto target = impl->entries.find(it->first);
            const bool inThisRoutingGraph = target != impl->entries.end()
                && ((target->second.internal && target->second.rootSessionId == rootId)
                    || it->first == rootId);
            if (inThisRoutingGraph && !desired.contains(it->first))
                it = source->second.linked.erase(it);
            else ++it;
        }
        for (auto targetId : desired) linkUnlocked(sourceId, targetId);
    }
}

std::vector<InstanceLinkRegistry::Peer> InstanceLinkRegistry::routingPeers(
    AvaAudioProcessor& root, int sourceNodeId)
{
    refreshRoutingEntries(root);
    std::vector<Peer> result;
    if (!impl) return result;
    const auto topology = ava::routing::readState(root.getValueTreeState().state);
    const auto linked = readRoutingTargets(root.getValueTreeState().state, sourceNodeId);
    for (const auto& node : topology.nodes)
        if (!node.container && node.id != sourceNodeId
            && (node.id == topology.rootInstanceId || root.getRoutingInstanceHandle(node.id)))
        {
            const auto name = ava::routing::getDisplayName(topology, node.id);
            result.push_back({uint64_t(node.id), name.isNotEmpty()
                ? name : "INSTANCE " + juce::String(node.id),
                linked.contains(juce::String(node.id)), {}});
        }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right)
    { return left.id < right.id; });
    return result;
}

void InstanceLinkRegistry::setRoutingLinked(AvaAudioProcessor& root,
                                           int sourceNodeId, int targetNodeId, bool shouldLink)
{
    if (sourceNodeId == targetNodeId || root.getLinkSessionId() == 0) return;
    const auto topology = ava::routing::readState(root.getValueTreeState().state);
    const auto validNode = [&topology](int id)
    {
        return std::any_of(topology.nodes.begin(), topology.nodes.end(), [id](const auto& node)
        { return node.id == id && !node.container; });
    };
    if (!validNode(sourceNodeId) || !validNode(targetNodeId)) return;
    auto& state = root.getValueTreeState().state;
    const auto key = routingLinkKey(sourceNodeId);
    auto targets = readRoutingTargets(state, sourceNodeId);
    if (shouldLink) targets.addIfNotAlreadyThere(juce::String(targetNodeId));
    else targets.removeString(juce::String(targetNodeId));
    state.setProperty(key, targets.joinIntoString(";"), nullptr);
    root.notifyHostOfStateChange();
    refreshRoutingEntries(root);
}

void InstanceLinkRegistry::pollRouting(AvaAudioProcessor& root)
{
    refreshRoutingEntries(root);
    if (!impl) return;
    std::vector<uint64_t> sources;
    {
        std::lock_guard lock(impl->mutex);
        for (const auto& [id, entry] : impl->entries)
            if (entry.internal && entry.rootSessionId == root.getLinkSessionId())
                sources.push_back(id);
    }
    // The root uses its ordinary UNIT poll, which also dispatches to its
    // internal routing links. Every nested node has an independent baseline.
    poll(root.getLinkSessionId());
    for (auto id : sources) poll(id);
    ava::modules::sibil::syncRouting(root);
}

void InstanceLinkRegistry::poll(uint64_t source)
{
    if (!impl) return;
    // The processor-lifetime registry timer runs on the message thread,
    // independently of plugin windows. No dispatch runs on the audio thread.
    std::lock_guard lock(impl->mutex);
    reconcileSavedLinks();
    const auto sender = impl->entries.find(source);
    if (sender == impl->entries.end() || sender->second.processor == nullptr) return;
    if (sender->second.linked.empty()) return;
    auto& entry = sender->second;
    const auto revision = entry.processor->getStateRevision();
    const auto viewRevision = entry.processor->getRoutingOwner().viewStateRevision.load(std::memory_order_relaxed);
    const bool audioChanged = revision != entry.observedRevision;
    const bool viewEdited = viewRevision != entry.observedViewRevision;
    const bool pending = std::any_of(entry.linked.begin(), entry.linked.end(),
        [] (const auto& link) { return !link.second.routingEdits.empty(); });
    if (!audioChanged && !viewEdited && !pending && ++entry.quietTicks < 12)
        return;
    entry.observedRevision = revision;
    entry.observedViewRevision = viewRevision;
    entry.quietTicks = 0;

    std::shared_ptr<const Snapshot> freshBaseline;
    juce::ValueTree unitSnapshot;
    for (auto& [receiverId, relation] : entry.linked)
    {
        const auto receiver = impl->entries.find(receiverId);
        if (receiver == impl->entries.end() || !receiver->second.processor) continue;
        auto& recipient = receiver->second;
        if (!entry.internal && !recipient.internal)
        {
            if (!audioChanged && !viewEdited && relation.routingEdits.empty()) continue;
            if (!unitSnapshot.isValid()) unitSnapshot = InstanceLinkAccess::captureUnit(*entry.processor);
            if (!unitSnapshot.isValid()) continue;
            const auto previous = std::exchange(relation.unitBaseline, unitSnapshot);
            auto routingEdits = std::exchange(relation.routingEdits, {});
            if (!canTransfer(entry.processor->parameters.state, AvaAudioProcessor::linkSendStateKey)
                || !canTransfer(recipient.processor->parameters.state, AvaAudioProcessor::linkReceiveStateKey))
                continue;

            auto& target = *recipient.processor;
            bool routingChanged = false;
            for (const auto& action : routingEdits)
            {
                auto& tree = target.parameters.state;
                const auto local = ava::routing::readState(tree);
                if (action.added != 0)
                    if (const auto existing = findNamedInstance(local, action.addedName); existing != 0)
                    {
                        relation.nodes[action.added] = existing;
                        continue;
                    }
                const auto resolve = [&](int id)
                {
                    return action.added != 0 || action.removed
                        ? namedRoutingTarget(action.before, local, id)
                        : nodeOnTarget(relation.nodes, action.before, local, id);
                };
                const auto first = resolve(action.first);
                const auto second = resolve(action.second);
                if (first == 0 || (second == 0 && action.second != 0)) continue;
                if (!action.edit(tree, first, second)) continue;
                routingChanged = true;
                if (action.removed) relation.nodes.erase(action.first);
                if (action.added != 0)
                {
                    relation.nodes[action.added] = local.nextInstanceId;
                    ava::routing::renameInstance(tree, local.nextInstanceId, action.addedName, true);
                }
                target.synchronizeRouting();
                if (auto* editor = target.getContentEditor()) editor->requestStructureRestoreResync();
            }

            if (previous.isValid())
            {
                const auto oldRouting = ava::routing::readState(previous);
                const auto newRouting = ava::routing::readState(unitSnapshot);
                if (oldRouting.rootBypassed != newRouting.rootBypassed)
                {
                    const auto root = nodeOnTarget(relation.nodes, newRouting,
                        ava::routing::readState(target.parameters.state), newRouting.rootInstanceId);
                    if (root != 0)
                        target.setRoutingInstanceBypassValue(root, newRouting.rootBypassed);
                }
                if (routingEdits.empty())
                    for (const auto& node : newRouting.nodes)
                    {
                        const auto* old = ava::routing::findStateNode(oldRouting.nodes, node.id);
                        if (old == nullptr || old->compensated == node.compensated) continue;
                        const auto local = nodeOnTarget(relation.nodes, newRouting,
                            ava::routing::readState(target.parameters.state), node.id);
                        if (local != 0)
                            routingChanged |= ava::routing::setGroupCompensated(target.parameters.state, local, node.compensated);
                    }
                if (routingChanged && routingEdits.empty()) target.synchronizeRouting();
            }

            auto merged = InstanceLinkAccess::captureUnit(target);
            // Capture omits local SYNC settings for comparison. Restore them
            // from the recipient before passing the merged result to APVTS.
            for (const auto* key : unitSyncProperties)
                if (target.parameters.state.hasProperty(key))
                    merged.setProperty(key, target.parameters.state.getProperty(key), nullptr);
            const bool stateChanged = mergeUnit(previous, unitSnapshot, merged, relation.nodes);
            if (stateChanged && InstanceLinkAccess::applyUnit(target, merged))
            {
                if (audioChanged) target.notifyHostOfStateChange();
                else target.notifyHostOfViewChange();
            }
            if (routingChanged || stateChanged)
            {
                const auto now = juce::Time::getMillisecondCounter();
                entry.activity.sentAt = now;
                recipient.activity.receivedAt = now;
                if (routingChanged)
                    for (auto& [id, node] : impl->entries)
                        if (node.internal && node.rootSessionId == receiverId)
                        {
                            recipient.linked.erase(id);
                            node.linked.clear();
                        }
                rebaseRecipient(receiverId);
            }
            continue;
        }

        if (freshBaseline == nullptr)
            freshBaseline = std::make_shared<Snapshot>(InstanceLinkAccess::capture(*entry.processor));
        const auto& fresh = *freshBaseline;
        const auto previousBaseline = std::exchange(relation.baseline, freshBaseline);
        if (!previousBaseline) continue;
        const auto& previous = *previousBaseline;
        auto& target = *receiver->second.processor;
        auto& mapping = relation.mapping;
        auto& filterMappings = relation.eqlFilterIndices;
        bool changed = false;
        bool viewChanged = false;

        // Mirror explicit module removals and copy additions. Copies are paired
        // using type + band + creation serial.
        for (const auto& [key, oldSlot] : previous.slots)
            if (!fresh.slots.contains(key))
            {
                const auto mapped = mapping.find(key);
                if (mapped != mapping.end())
                {
                    changed |= target.deleteModuleSlot(size_t(key.band), mapped->second);
                    mapping.erase(mapped);
                    filterMappings.erase(key);
                }
            }
        for (const auto& [key, newSlot] : fresh.slots)
            if (key.serial > 1 && !previous.slots.contains(key))
            {
                const int newUid = target.addModuleCopy(size_t(key.band), key.type);
                if (newUid >= 0)
                {
                    changed = true;
                    mapping[key] = newUid;
                    // Newly created copies get the sender's active filter
                    // count, while pre-existing copies are never reset.
                    if (key.type == module_order::eql)
                        if (const auto wanted = fresh.filterCounts.find(key);
                            wanted != fresh.filterCounts.end())
                            if (auto* eql = InstanceLinkAccess::destinationEql(target, key, mapping))
                            {
                                while (eql->getActiveFilterCount() < wanted->second && eql->addFilter()) {}
                                while (eql->getActiveFilterCount() > wanted->second)
                                    { changed = true; eql->removeFilter(eql->getActiveFilterCount() - 1); }
                                for (int index = 0; index < eql->getActiveFilterCount(); ++index)
                                    filterMappings[key][index] = index;
                            }
                }
            }

        // INSTANCE links mirror both per-module states. MODULES `shown` controls
        // processing activation and editor display; BP stays independent.
        for (const auto& [key, newSlot] : fresh.slots)
        {
            const auto old = previous.slots.find(key);
            if (old == previous.slots.end()) continue;
            const auto mapped = mapping.find(key);
            if (mapped == mapping.end()) continue;
            if (old->second.enabled != newSlot.enabled)
                changed |= target.setModuleSlotEnabled(size_t(key.band), mapped->second, newSlot.enabled);
            if (old->second.shown != newSlot.shown)
                viewChanged |= target.setModuleSlotShown(size_t(key.band), mapped->second, newSlot.shown);
        }
        for (const auto& [band, newOrder] : fresh.order)
        {
            const auto old = previous.order.find(band);
            if (old != previous.order.end() && old->second == newOrder) continue;
            const auto slots = target.getModuleStackForRange(size_t(band));
            std::vector<int> newMapped;
            std::set<int> linkedUids;
            for (const auto& key : newOrder)
                if (const auto it = mapping.find(key); it != mapping.end())
                {
                    newMapped.push_back(it->second);
                    linkedUids.insert(it->second);
                }
            std::vector<int> order;
            order.reserve(slots.size());
            size_t next = 0;
            for (const auto& slot : slots)
                order.push_back(linkedUids.contains(slot.uid) && next < newMapped.size()
                    ? newMapped[next++] : slot.uid);
            if (order.size() == slots.size() && !order.empty())
                changed |= target.moveModuleSlots(size_t(band), order);
        }

        // GAIN's channel ordering is metadata rather than an
        // APVTS parameter; synchronise it separately for original and copies.
        for (const auto& [key, orders] : fresh.chainOrders)
        {
            const auto previousOrder = previous.chainOrders.find(key);
            if (previousOrder != previous.chainOrders.end()
                && previousOrder->second.gains == orders.gains)
                continue;
            BlockChainProcessor* destination = nullptr;
            if (key.type < 0) destination = target.getBlockChainProcessor();
            else if (const auto found = mapping.find(key); found != mapping.end())
                destination = target.getCopiedBlockChainProcessor(found->second);
            if (destination == nullptr) continue;
            if (destination->getGainOrder(size_t(key.band)) != orders.gains)
            {
                destination->setGainOrder(size_t(key.band), orders.gains);
                changed = true;
            }
        }

        // EQL count changes are operations, not absolute preset copies. A
        // receiver can have more filters than the sender at link time, so a
        // newly added filter needs its own destination index.
        for (const auto& [key, count] : fresh.filterCounts)
        {
            const auto old = previous.filterCounts.find(key);
            if (old == previous.filterCounts.end() || old->second == count) continue;
            auto* eql = InstanceLinkAccess::destinationEql(target, key, mapping);
            if (eql == nullptr) continue;
            auto& indices = filterMappings[key];
            const int delta = count - old->second;
            for (int index = 0; index < delta; ++index)
            {
                const int destinationIndex = eql->getActiveFilterCount();
                if (!eql->addFilter()) break;
                indices[old->second + index] = destinationIndex;
                changed = true;
            }
            for (int removed = 0; removed < -delta; ++removed)
            {
                // Detect which source filter was removed by comparing the
                // surviving seven parameter values. If filters are identical,
                // removing the last equivalent filter has the same DSP result.
                const int oldCount = old->second - removed;
                int removedSourceIndex = oldCount - 1;
                if (delta == -1)
                {
                    auto bestScore = std::numeric_limits<double>::infinity();
                    for (int candidate = 0; candidate < oldCount; ++candidate)
                    {
                        double score = 0.0;
                        for (int destinationIndex = 0; destinationIndex < count; ++destinationIndex)
                        {
                            const int sourceIndex = destinationIndex + (destinationIndex >= candidate ? 1 : 0);
                            for (auto suffix : {"_type", "_phase", "_order", "_freq", "_bw", "_gain", "_bp"})
                            {
                                const auto prefix = key.serial == 1
                                    ? "E|" + juce::String(key.band) + "|"
                                    : "X|" + juce::String(key.band) + "|"
                                        + juce::String(key.type) + "|" + juce::String(key.serial) + "|";
                                const auto oldKey = prefix + "filter-" + juce::String(sourceIndex + 1) + suffix;
                                const auto newKey = prefix + "filter-" + juce::String(destinationIndex + 1) + suffix;
                                const auto before = previous.values.find(oldKey);
                                const auto after = fresh.values.find(newKey);
                                if (before != previous.values.end() && after != fresh.values.end())
                                    score += std::abs(double(before->second.normalized - after->second.normalized));
                            }
                        }
                        if (score <= bestScore) { bestScore = score; removedSourceIndex = candidate; }
                    }
                }
                const auto affected = indices.find(removedSourceIndex);
                if (affected == indices.end()) continue;
                const int removedDestinationIndex = affected->second;
                if (!eql->removeFilter(removedDestinationIndex).has_value()) continue;
                changed = true;
                auto oldIndices = std::move(indices);
                indices.clear();
                for (const auto& [sourceIndex, destinationIndex] : oldIndices)
                {
                    if (sourceIndex == removedSourceIndex) continue;
                    const int newSourceIndex = sourceIndex - (sourceIndex > removedSourceIndex ? 1 : 0);
                    const int newDestinationIndex = destinationIndex - (destinationIndex > removedDestinationIndex ? 1 : 0);
                    indices[newSourceIndex] = newDestinationIndex;
                }
            }
        }

        // Apply *only* changed normalized values; never clone complete
        // snapshots or replace the recipient's APVTS.
        for (const auto& [key, value] : fresh.values)
        {
            // Routing monitor/mute switches belong to individual nodes.
            // Mirroring MT silences every linked branch instead of just the
            // node selected in the pinned strip. UNIT-to-UNIT sync is separate.
            if (value.owner == 0
                && (value.id == AvaAudioProcessor::paramSoloInstanceId
                    || value.id == AvaAudioProcessor::paramInstanceMuteId
                    || value.id == AvaAudioProcessor::paramSoloChannelId)) continue;
            // The root's BP is the global plugin bypass; a nested node's BP
            // bypasses only that node. Never equate the two during routing sync.
            if (value.owner == 0 && value.id == AvaAudioProcessor::paramGlobalBypassId
                && (sender->second.internal != receiver->second.internal))
                continue;
            const auto old = previous.values.find(key);
            if (old != previous.values.end()
                && std::abs(old->second.normalized - value.normalized) < 1.0e-7f)
                continue;
            // Preserve the independent filter layouts that existed when
            // LINK was enabled. A newly appended filter in the source maps
            // to the newly appended filter in each recipient, not to an
            // unrelated filter with the same numeric index.
            auto parameterId = value.id;
            if ((value.owner == 3 || value.owner == 6) && parameterId.startsWith("filter-"))
            {
                const int underscore = parameterId.indexOfChar('_');
                if (underscore < 8) continue;
                const int sourceFilterIndex = parameterId.substring(7, underscore).getIntValue() - 1;
                const auto foundModule = filterMappings.find(value.module);
                if (foundModule == filterMappings.end()) continue;
                const auto foundIndex = foundModule->second.find(sourceFilterIndex);
                if (foundIndex == foundModule->second.end()) continue;
                parameterId = "filter-" + juce::String(foundIndex->second + 1)
                              + parameterId.substring(underscore);
            }
            if (auto* destinationParameters = InstanceLinkAccess::destinationState(target, value, mapping))
                if (auto* parameter = destinationParameters->getParameter(parameterId))
                    if (std::abs(parameter->getValue() - value.normalized) >= 1.0e-7f)
                    {
                        changed = true;
                        parameter->beginChangeGesture();
                        parameter->setValueNotifyingHost(value.normalized);
                        parameter->endChangeGesture();
                    }
        }
        // A target that also drives other targets must not echo changes back.
        if (changed || viewChanged)
        {
            if (changed) target.notifyHostOfStateChange();
            rebaseRecipient(receiverId);
        }
    }
}

void InstanceLinkRegistry::rebaseRecipient(uint64_t recipient)
{
    auto& entry = impl->entries.at(recipient);
    auto& target = *entry.processor;
    std::shared_ptr<const Snapshot> localSnapshot;
    juce::ValueTree unitSnapshot;
    for (auto& [outgoingId, outgoing] : entry.linked)
    {
        const auto destination = impl->entries.find(outgoingId);
        if (destination == impl->entries.end()) continue;
        if (!entry.internal && !destination->second.internal)
        {
            if (!unitSnapshot.isValid()) unitSnapshot = InstanceLinkAccess::captureUnit(target);
            outgoing.unitBaseline = unitSnapshot;
        }
        else
        {
            if (localSnapshot == nullptr)
                localSnapshot = std::make_shared<Snapshot>(InstanceLinkAccess::capture(target));
            outgoing.baseline = localSnapshot;
        }
    }
    entry.observedRevision = target.getStateRevision();
    entry.observedViewRevision = target.getRoutingOwner().viewStateRevision.load(std::memory_order_relaxed);
    entry.quietTicks = 0;
}
