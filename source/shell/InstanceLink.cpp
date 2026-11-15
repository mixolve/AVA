#include "InstanceLink.h"
#include "Processor.h"
#include "../routing/State.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/sibil/Link.h"
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

namespace
{
struct ModuleKey
{
    int band = 0, type = 0, serial = 1;
    auto operator<=>(const ModuleKey&) const = default;
};

struct SlotInfo { int uid = -1; bool enabled = true; };

struct ParameterValue
{
    // 0: shell; 1: original CHAIN; 2: original DYN; 3: original EQL;
    // 4: copied CHAIN; 5: copied DYN; 6: copied EQL.
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

} // namespace

// The access shim is the only part of LINK allowed to inspect processor-owned
// module stores. It never exposes an internal pointer to the UI or registry.
class InstanceLinkAccess
{
public:
    static Snapshot capture(AvaAudioProcessor&);
    static juce::AudioProcessorValueTreeState* destinationState(
        AvaAudioProcessor&, const ParameterValue&, const std::map<ModuleKey, int>&);
    static EqlModuleProcessor* destinationEql(
        AvaAudioProcessor&, const ModuleKey&, const std::map<ModuleKey, int>&);
};

Snapshot InstanceLinkAccess::capture(AvaAudioProcessor& p)
{
    Snapshot snapshot;
    // Processing and module-copy mutations take this same lock. Capture a
    // coherent set of parameter owners without keeping stale pointers.
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
            snapshot.slots.emplace(key, SlotInfo {slot.uid, slot.enabled});
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
        juce::String name;
        uint64_t observedRevision = 0;
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
        };
        std::map<uint64_t, Destination> linked;
    };
    mutable std::mutex mutex;
    uint64_t nextId = 1; // External UNITs use sequential display numbers.
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
    entry.name = "AVA " + juce::String(int(id)).paddedLeft('0', 2);
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
    // A root UNIT owns the lifetime of its synthetic routing-node entries.
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
            result.push_back({id, entry.name, sender->second.linked.contains(id),
                entry.processor->getValueTreeState().state.getProperty(AvaAudioProcessor::linkUuidStateKey).toString()});
    return result;
}

namespace
{
// Preserve empty positions: NONE in one selector must not shift later links.
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
    {
        // Resume future edits only, without disturbing pending internal links.
        std::vector<uint64_t> recipients;
        for (const auto& [id, relation] : entry.linked)
        {
            juce::ignoreUnused(relation);
            const auto receiver = impl->entries.find(id);
            if (receiver != impl->entries.end() && !receiver->second.internal) recipients.push_back(id);
        }
        for (auto receiver : recipients)
        {
            entry.linked.erase(receiver);
            linkUnlocked(unit, receiver);
        }
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
    {
        // Only this recipient gets a fresh baseline. Other recipients must
        // still receive any sender edits pending when RECEIVE was enabled.
        for (auto& [source, sender] : impl->entries)
            if (!sender.internal && sender.linked.contains(unit))
            {
                sender.linked.erase(unit);
                linkUnlocked(source, unit);
            }
    }
}

void InstanceLinkRegistry::linkUnlocked(uint64_t source, uint64_t target)
{
    auto from = impl->entries.find(source), to = impl->entries.find(target);
    if (from == impl->entries.end() || to == impl->entries.end()
        || from->second.linked.contains(target)) return;
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
            // Reconciliation of host UNIT UUIDs must not remove internal links.
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
    const auto revision = sender->second.processor->getStateRevision();
    // All normal control edits notify the root processor. Avoid allocating a
    // large EQL snapshot 60 times/sec while nothing is changing. Keep a slow
    // fallback scan for metadata edits that may not publish a revision.
    if (revision == sender->second.observedRevision && ++sender->second.quietTicks < 12)
        return;
    sender->second.observedRevision = revision;
    sender->second.quietTicks = 0;
    const auto freshBaseline = std::make_shared<Snapshot>(InstanceLinkAccess::capture(*sender->second.processor));
    const auto& fresh = *freshBaseline;
    if (sender->second.linked.empty()) return;

    for (auto& [receiverId, relation] : sender->second.linked)
    {
        const auto receiver = impl->entries.find(receiverId);
        if (receiver == impl->entries.end() || !receiver->second.processor) continue;
        const auto previousBaseline = std::move(relation.baseline);
        relation.baseline = freshBaseline;
        if (!previousBaseline) continue;
        const auto& previous = *previousBaseline;
        // RECEIVE gates incoming host UNIT changes only. Internal routing
        // INSTANCE links retain their independent behaviour.
        const bool unitTransfer = !sender->second.internal && !receiver->second.internal;
        if (unitTransfer
            && (!bool(sender->second.processor->getValueTreeState().state
                    .getProperty(AvaAudioProcessor::linkSendStateKey, false))
                || !bool(receiver->second.processor->getValueTreeState().state
                    .getProperty(AvaAudioProcessor::linkReceiveStateKey, false)))) continue;
        auto& target = *receiver->second.processor;
        auto& mapping = relation.mapping;
        auto& filterMappings = relation.eqlFilterIndices;
        bool changed = false;

        // Mirror explicit copy additions/removals, not existing, unrelated
        // copies. A copy is paired using type + band + creation serial.
        for (const auto& [key, oldSlot] : previous.slots)
            if (key.serial > 1 && !fresh.slots.contains(key))
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
                // Create the same module type, using the target's original
                // as the insertion point, then apply the sender's parameter deltas.
                const int newUid = target.addModuleCopy(size_t(key.band), ava::module_stack::originalUid(key.type));
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

        // Keep per-slot bypass and the source's relative module ordering.
        for (const auto& [key, newSlot] : fresh.slots)
        {
            const auto old = previous.slots.find(key);
            if (old == previous.slots.end() || old->second.enabled == newSlot.enabled) continue;
            const auto mapped = mapping.find(key);
            if (mapped != mapping.end())
                changed |= target.setModuleSlotEnabled(size_t(key.band), mapped->second, newSlot.enabled);
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
        if (changed)
        {
            if (unitTransfer)
            {
                const auto now = juce::Time::getMillisecondCounter();
                sender->second.activity.sentAt = now;
                receiver->second.activity.receivedAt = now;
            }
            target.notifyHostOfStateChange();
            const auto receivedBaseline = std::make_shared<Snapshot>(InstanceLinkAccess::capture(target));
            for (auto& [outgoingId, outgoing] : receiver->second.linked)
            {
                juce::ignoreUnused(outgoingId);
                outgoing.baseline = receivedBaseline;
            }
            receiver->second.observedRevision = target.getStateRevision();
            receiver->second.quietTicks = 0;
        }
    }
}
