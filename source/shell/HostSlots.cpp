#include "Processor.h"
#include "InstanceLink.h"
#include "EditorStateSchema.h"
#include "EditorPreferences.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/Processor.h"
#include "FilterOrderState.h"
#include "OscController.h"
#include "ModuleCopyTargets.h"
#include "ModuleSlotTargets.h"
#include "../routing/Runtime.h"
#include "../crossover/ModuleBindings.h"
#include "../modules/eql/FilterSupport.h"
#include "../modules/shared/StateValidation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "HostSlotBinding.h"

using shell_host_slot::DelegatedBoolParameter;

juce::String AvaAudioProcessor::getHostAssignmentTargetId(const juce::String& parameterId) const
{
    auto id = parameterId.trim();
    int instance = 0;
    juce::String local;
    if (ava::routing::parseParameterTarget(id, instance, local)) return id;
    // A copied module has its own APVTS, even if its local parameter IDs
    // happen to match the original module's parameter IDs.
    int copyUid = -1;
    if (ava::module_copy_targets::parse(id, copyUid, local))
        return ava::routing::parameterTargetId(getRoutingInstanceId(), id);
    if (id.startsWith("filter-"))
        id = "band-" + juce::String(int(getSelectedCrossoverRange()) + 1) + "_" + id;
    if (!routingInstance && id == paramGlobalBypassId) return id;
    return ava::routing::parameterTargetId(getRoutingInstanceId(), id);
}

juce::String AvaAudioProcessor::getHostSlotTargetId(const int slot) const
{
    if (!juce::isPositiveAndBelow(slot, hostAutomationSlotCount)) return {};
    return getRoutingOwner().parameters.state.getProperty(getHostSlotTargetStateKey(slot)).toString();
}

bool AvaAudioProcessor::isHostParameterAssigned(const juce::String& id) const
{
    const auto& owner = getRoutingOwner();
    // UI-only assignment lookup: painting must never take the audio lock.
    const auto canonical = getHostAssignmentTargetId(id);
    int instance = 0;
    juce::String local;
    const auto acceptsLocal = ava::routing::parseParameterTarget(canonical, instance, local)
        && instance == owner.getRoutingInstanceId() && local != paramGlobalBypassId;
    for (int slot = 0; slot < hostAutomationSlotCount; ++slot)
    {
        const auto assigned = owner.getHostSlotTargetId(slot);
        if (assigned == canonical || (acceptsLocal && assigned == local)) return true;
    }
    return false;
}

bool AvaAudioProcessor::toggleHostSlotAssignment(const juce::String& id)
{
    auto& owner = getRoutingOwner();
    const juce::ScopedLock lock(owner.processingLock);
    const auto canonical = getHostAssignmentTargetId(id);
    if (owner.findHostSlotTarget(canonical) == nullptr) return false;
    int instance = 0;
    juce::String local;
    const auto acceptsLocal = ava::routing::parseParameterTarget(canonical, instance, local)
        && instance == owner.getRoutingInstanceId() && local != paramGlobalBypassId;
    auto freeSlot = -1;
    for (int slot = 0; slot < assignableHostSlotCount; ++slot)
    {
        const auto assigned = owner.getHostSlotTargetId(slot);
        if (assigned == canonical || (acceptsLocal && assigned == local))
        {
            owner.clearHostSlotAssignment(slot);
            return true;
        }
        if (freeSlot < 0 && assigned.isEmpty()) freeSlot = slot;
    }
    if (freeSlot < 0) return false;
    owner.parameters.state.setProperty(getHostSlotTargetStateKey(freeSlot), canonical, nullptr);
    owner.refreshHostSlotTargets();
    owner.notifyHostOfStateChange();
    return true;
}

void AvaAudioProcessor::clearHostSlotAssignment(const int slot)
{
    if (!juce::isPositiveAndBelow(slot, assignableHostSlotCount)) return;
    auto& owner = getRoutingOwner();
    const juce::ScopedLock lock(owner.processingLock);
    if (owner.hostSlotBindings[size_t(slot)] != nullptr) owner.hostSlotBindings[size_t(slot)]->bind(nullptr);
    owner.hostSlotTargets[size_t(slot)].store(nullptr, std::memory_order_release);
    owner.pendingHostSlotValues[size_t(slot)].store(-1.0f, std::memory_order_release);
    owner.observedHostSlotValues[size_t(slot)].store(-1.0f, std::memory_order_release);
    owner.parameters.state.removeProperty(getHostSlotTargetStateKey(slot), nullptr);
    if (auto* parameter = owner.parameters.getParameter(getHostSlotParameterId(slot)))
        parameter->setValueNotifyingHost(0.0f);
    owner.notifyHostOfStateChange();
}

void AvaAudioProcessor::swapHostSlotAssignments(const int first, const int second)
{
    if (first == second || !juce::isPositiveAndBelow(first, assignableHostSlotCount)
        || !juce::isPositiveAndBelow(second, assignableHostSlotCount)) return;
    auto& owner = getRoutingOwner();
    const juce::ScopedLock lock(owner.processingLock);
    const auto a = owner.getHostSlotTargetId(first), b = owner.getHostSlotTargetId(second);
    owner.invalidateHostSlotTargets();
    const auto put = [&] (int slot, const juce::String& id)
    {
        const auto key = getHostSlotTargetStateKey(slot);
        if (id.isEmpty()) owner.parameters.state.removeProperty(key, nullptr);
        else owner.parameters.state.setProperty(key, id, nullptr);
    };
    put(first, b); put(second, a);
    owner.refreshHostSlotTargets();
    for (const auto slot : {first, second})
        if (owner.getHostSlotTargetId(slot).isEmpty())
            if (auto* parameter = owner.parameters.getParameter(getHostSlotParameterId(slot)))
                parameter->setValueNotifyingHost(0.0f);
    owner.notifyHostOfStateChange();
}

juce::String AvaAudioProcessor::getHostSlotDisplayName(const int slot) const
{
    const auto target = getHostSlotTargetId(slot);
    int instance = 0;
    juce::String local;
    if (!ava::routing::parseParameterTarget(target, instance, local)) return getOscDisplayName(target);
    const auto topology = ava::routing::readState(getRoutingOwner().parameters.state);
    size_t moduleBand = 0;
    int moduleUid = -1;
    if (ava::module_slot_targets::parse(local, moduleBand, moduleUid))
    {
        auto& root = const_cast<AvaAudioProcessor&>(getRoutingOwner());
        auto child = instance != root.getRoutingInstanceId() ? root.getRoutingInstanceHandle(instance) : nullptr;
        const auto* processor = instance == root.getRoutingInstanceId() ? &root : child.get();
        if (processor != nullptr)
        {
            const auto slots = processor->getModuleStackForRange(moduleBand);
            if (const auto* found = ava::module_stack::find(slots, moduleUid))
                return ava::routing::getDisplayName(topology, instance) + " / "
                    + juce::String(module_order::descriptions[size_t(found->type)].name)
                    + " (" + juce::String(ava::module_stack::displayOrdinal(slots, moduleUid)).paddedLeft('0', 2)
                    + ") / BAND " + juce::String(int(moduleBand) + 1) + " / BP";
        }
    }
    int copyUid = -1;
    juce::String copyLocal;
    juce::String controlName;
    if (ava::module_copy_targets::parse(local, copyUid, copyLocal))
    {
        auto& root = const_cast<AvaAudioProcessor&>(getRoutingOwner());
        auto child = instance != root.getRoutingInstanceId() ? root.getRoutingInstanceHandle(instance) : nullptr;
        const auto* processor = instance == root.getRoutingInstanceId() ? &root : child.get();
        if (processor != nullptr)
        {
            const juce::ScopedLock lock(processor->processingLock);
            const auto found = processor->moduleCopies.find(copyUid);
            if (found != processor->moduleCopies.end() && found->second != nullptr)
            {
                const auto& copy = *found->second;
                const auto ordinal = ava::module_stack::displayOrdinal(processor->moduleStacks[size_t(copy.range)], copyUid);
                size_t band = size_t(copy.range);
                juce::String suffix;
                if (!parseOscBandParameterId(copyLocal, band, suffix)) suffix = copyLocal;
                controlName = juce::String(module_order::descriptions[size_t(copy.type)].name).toUpperCase()
                    + " (" + juce::String(ordinal).paddedLeft('0', 2) + ") / BAND "
                    + juce::String(int(band) + 1) + " / " + getOscDisplayName(suffix);
            }
        }
        if (controlName.isEmpty()) controlName = getOscDisplayName(copyLocal);
    }
    else controlName = getOscDisplayName(local);
    return ava::routing::getDisplayName(topology, instance) + " / " + controlName;
}

std::vector<OscParameterInfo> AvaAudioProcessor::getAssignedOscParameters() const
{
    std::vector<OscParameterInfo> result;
    for (int slot = 0; slot < hostAutomationSlotCount; ++slot)
    {
        auto id = getHostSlotTargetId(slot);
        int instance = 0;
        juce::String local;
        if (ava::routing::parseParameterTarget(id, instance, local))
        {
            if (local == ava::routing::compensationParameterId) {
                if (routingInstance) continue; // Structural targets live in the owner's OSC namespace.
            } else {
                if (instance != getRoutingInstanceId()) continue;
                if (local != paramGlobalBypassId || routingInstance) id = local;
            }
        }
        else if (routingInstance) continue;
        auto* parameter = findHostSlotTarget(id);
        if (parameter == nullptr || std::any_of(result.begin(), result.end(),
            [&id] (const auto& info) { return info.sourceId == id; })) continue;
        OscParameterInfo info { id };
        const auto view = getFilterParameterDomain(id).presentation();
        info.valueScale = float(view.scale);
        if (!view.limits.isEmpty()) info.parameterRange = juce::Range<float>(
            float(view.limits.getStart() / view.scale), float(view.limits.getEnd() / view.scale));
        result.push_back(std::move(info));
    }
    return result;
}

juce::String AvaAudioProcessor::getHostSlotOscAddress(const int slot) const
{
    const auto& owner = getRoutingOwner();
    auto target = getHostSlotTargetId(slot);
    if (owner.findHostSlotTarget(target) == nullptr) return {};
    const auto topology = ava::routing::readState(owner.parameters.state);
    auto instance = topology.rootInstanceId;
    juce::String local;
    if (ava::routing::parseParameterTarget(target, instance, local)) {
        if (local == ava::routing::compensationParameterId) instance = topology.rootInstanceId;
        else if (local != paramGlobalBypassId || instance != topology.rootInstanceId) target = local;
    }
    return makeAssignedOscAddress(ava::routing::getDisplayName(topology, instance), target);
}

juce::String AvaAudioProcessor::getHostSlotParameterId(const int slotIndex)
{
    const auto clampedSlot = juce::jlimit(0, hostAutomationSlotCount - 1, slotIndex);
    return juce::String(paramHostSlotPrefix) + juce::String::formatted("%02d", clampedSlot + 1);
}

juce::String AvaAudioProcessor::getHostSlotLetterLabel(const int slotIndex)
{
    const auto index = juce::jlimit(0, hostAutomationSlotCount - 1, slotIndex);
    const auto first = index / 26;
    const auto second = index % 26;

    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + first))
        + juce::String::charToString(static_cast<juce::juce_wchar>('A' + second));
}

juce::String AvaAudioProcessor::getHostSlotTargetStateKey(const int slotIndex)
{
    const auto clampedSlot = juce::jlimit(0, hostAutomationSlotCount - 1, slotIndex);
    return "ava.host_slot." + juce::String::formatted("%02d", clampedSlot + 1) + ".target";
}

juce::RangedAudioParameter* AvaAudioProcessor::findHostSlotTarget(const juce::String& parameterId) const noexcept
{
    if (parameterId.isEmpty() || parameterId.startsWith(paramHostSlotPrefix)) return nullptr;
    int instance = 0;
    juce::String local;
    if (!ava::routing::parseParameterTarget(parameterId, instance, local))
        return findLocalHostSlotTarget(parameterId);
    const auto& owner = getRoutingOwner();
    const auto topology = ava::routing::readState(owner.parameters.state);
    const auto* node = ava::routing::findStateNode(topology.nodes, instance);
    if (node == nullptr) return nullptr;
    if (local == ava::routing::compensationParameterId) {
        if (!ava::routing::isParallelHead(topology, instance)) return nullptr;
        const auto found = owner.routingCompensationTargets.find(instance);
        return found != owner.routingCompensationTargets.end() ? found->second.get() : nullptr;
    }
    if (node->container) return nullptr;
    if (local == paramGlobalBypassId)
    {
        const auto found = owner.routingBypassTargets.find(instance);
        return found != owner.routingBypassTargets.end() ? found->second.get() : nullptr;
    }
    if (instance == topology.rootInstanceId) return owner.findLocalHostSlotTarget(local);
    const auto child = owner.routingRuntime != nullptr ? owner.routingRuntime->getInstanceHandle(instance) : nullptr;
    return child != nullptr ? child->findLocalHostSlotTarget(local) : nullptr;
}

void AvaAudioProcessor::registerModuleSlotBypassTarget(const size_t band, const int uid, const int type)
{
    const auto key = std::make_pair(band, uid);
    if (moduleSlotBypassTargets.contains(key)) return;
    const auto id = ava::module_slot_targets::make(band, uid);
    const auto name = juce::String(module_order::descriptions[size_t(type)].name).toUpperCase()
        + " / BAND " + juce::String(int(band) + 1) + " / BP";
    moduleSlotBypassTargets.emplace(key, std::make_unique<DelegatedBoolParameter>(id, name,
        [this, band, uid] { return isModuleSlotBypassed(band, uid) ? 1.0f : 0.0f; },
        [this, band, uid](bool bypassed) { setModuleSlotEnabled(band, uid, !bypassed); }));
}

void AvaAudioProcessor::assignNewModuleBypassToHostSlot(const size_t band, const int uid)
{
    auto& owner = getRoutingOwner();
    const auto targetId = ava::routing::parameterTargetId(getRoutingInstanceId(),
        ava::module_slot_targets::make(band, uid));
    // Auto-assignment must never overwrite manually assigned slots.
    int freeSlot = -1;
    for (int index = assignableHostSlotCount - 1; index >= 0; --index)
    {
        const auto current = owner.getHostSlotTargetId(index);
        if (current == targetId) return;
        if (current.isEmpty() && freeSlot < 0) freeSlot = index;
    }
    if (freeSlot < 0) return;
    owner.parameters.state.setProperty(getHostSlotTargetStateKey(freeSlot), targetId, nullptr);
    owner.refreshHostSlotTargets();
}

juce::RangedAudioParameter* AvaAudioProcessor::findLocalHostSlotTarget(const juce::String& parameterId) const noexcept
{
    if (parameterId.isEmpty() || parameterId.startsWith(paramHostSlotPrefix)
        || parameterId.startsWith("instance-")) return nullptr;
    if (auto* parameter = parameters.getParameter(parameterId))
        return parameter;

    int copyUid = -1;
    juce::String local;
    if (ava::module_copy_targets::parse(parameterId, copyUid, local))
    {
        const auto it = moduleCopies.find(copyUid);
        if (it == moduleCopies.end()) return nullptr;
        const auto& copy = *it->second;
        if (copy.blocks != nullptr) return copy.blocks->getValueTreeState().getParameter(local);
        if (copy.dyn != nullptr) return copy.dyn->getValueTreeState().getParameter(local);
        if (copy.eql != nullptr)
        {
            size_t band = 0;
            juce::String suffix;
            if (!parseOscBandParameterId(local, band, suffix)) return nullptr;
            if (const auto* eql = copy.eql->getProcessor(band))
                return eql->getValueTreeState().getParameter(suffix);
        }
        return nullptr;
    }

    size_t slotBand = 0;
    int slotUid = -1;
    if (ava::module_slot_targets::parse(parameterId, slotBand, slotUid))
    {
        if (slotBand >= moduleStacks.size()) return nullptr;
        const auto found = moduleSlotBypassTargets.find(std::make_pair(slotBand, slotUid));
        if (found == moduleSlotBypassTargets.end()) return nullptr;
        const auto& stack = moduleStacks[slotBand];
        return ava::module_stack::find(stack, slotUid) != nullptr ? found->second.get() : nullptr;
    }

    for (const auto& module : module_bypass::descriptions)
        if (parameterId == module.bypassStateKey) return moduleBypassTargets[size_t(module.id)].get();

    const auto findInModule = [&parameterId] (auto* processor) -> juce::RangedAudioParameter*
    {
        return processor != nullptr ? processor->getValueTreeState().getParameter(parameterId) : nullptr;
    };

    if (auto* parameter = findInModule(getBlockChainProcessor())) return parameter;
    if (auto* parameter = findInModule(getDynModuleProcessor())) return parameter;
    size_t band = 0;
    juce::String suffix;
    if (parseOscBandParameterId(parameterId, band, suffix))
        if (const auto* bank = getEqlProcessorBank())
            if (const auto* module = bank->getProcessor(band))
                return module->getValueTreeState().getParameter(suffix);
    if (auto* parameter = findInModule(getEqlModuleProcessor())) return parameter;

    return nullptr;
}

// JUCE/VST3 hosts may deliver automation on a render worker without running
// the GUI message loop. Do not rely on ParameterListener/AsyncUpdater alone:
// sample the actual host-facing parameter at the beginning of every block.
// The cached pointer is lifetime-stable and this function allocates nothing.
void AvaAudioProcessor::pollHostSlotValuesFromAudioCallback() noexcept
{
    if (routingInstance) return;
    for (int slot = 0; slot < assignableHostSlotCount; ++slot)
    {
        const auto index = size_t(slot);
        auto* hostParameter = hostSlotParameters[index];
        // An unresolved binding keeps its last pending host value. Once the
        // graph has been rebuilt, refreshHostSlotTargets() will rebind it.
        if (hostParameter == nullptr || hostSlotTargets[index].load(std::memory_order_acquire) == nullptr)
            continue;
        const auto value = hostParameter->getValue();
        if (!std::isfinite(value)) continue;
        const auto last = observedHostSlotValues[index].load(std::memory_order_relaxed);
        if (last < 0.0f || std::abs(last - value) > 1.0e-7f)
        {
            observedHostSlotValues[index].store(value, std::memory_order_relaxed);
            pendingHostSlotValues[index].store(juce::jlimit(0.0f, 1.0f, value), std::memory_order_release);
            hostSlotAutomationPending.store(true, std::memory_order_release);
        }
    }
}

// Host renders can restore a parameter chunk without emitting new parameter
// callbacks afterwards. Re-seed each assigned slot from the *host-facing*
// parameter, so its value is applied to the restored DSP target rather than
// being overwritten by a stale target when the binding is rebuilt.
// Never replace a newer automation value that is already queued by REAPER.
void AvaAudioProcessor::queueCurrentHostSlotValues()
{
    if (routingOwner != nullptr) { routingOwner->queueCurrentHostSlotValues(); return; }
    if (routingInstance) return;
    const juce::ScopedLock lock(processingLock);
    bool queued = false;
    for (int slot = 0; slot < assignableHostSlotCount; ++slot)
    {
        if (getHostSlotTargetId(slot).isEmpty()) continue;
        auto* parameter = hostSlotParameters[size_t(slot)];
        if (parameter == nullptr) continue;
        float empty = -1.0f;
        pendingHostSlotValues[size_t(slot)].compare_exchange_strong(
            empty, parameter->getValue(), std::memory_order_acq_rel);
        queued = true;
    }
    if (queued) hostSlotAutomationPending.store(true, std::memory_order_release);
}

void AvaAudioProcessor::invalidateHostSlotTargets()
{
    auto& owner = getRoutingOwner();
    const juce::ScopedLock lock(owner.processingLock);
    for (auto& binding : owner.hostSlotBindings) if (binding != nullptr) binding->bind(nullptr);
    for (auto& target : owner.hostSlotTargets) target.store(nullptr, std::memory_order_release);
    for (auto& observed : owner.observedHostSlotValues) observed.store(-1.0f, std::memory_order_release);
    // Pending host automation is tied to the exposed slot, not a particular
    // transient DSP parameter object. Keep it across a routing rebuild.
}

void AvaAudioProcessor::refreshHostSlotTargets()
{
    if (routingOwner != nullptr) { routingOwner->refreshHostSlotTargets(); return; }
    if (routingInstance) return;
    const juce::ScopedLock lock(processingLock);

    if (routingRuntime != nullptr)
        for (const auto& node : ava::routing::readState(parameters.state).nodes)
            if (!node.container && !routingBypassTargets.contains(node.id))
                routingBypassTargets.emplace(node.id, std::make_unique<DelegatedBoolParameter>(ava::routing::bypassTargetId(node.id),
                    "INSTANCE-" + juce::String(node.id) + "-BYPASS",
                    [this, id = node.id] { return getRoutingInstanceBypassValue(id); },
                    [this, id = node.id](bool bypassed) { setRoutingInstanceBypassValue(id, bypassed); }));

    const auto topology = ava::routing::readState(parameters.state);
    // S, MT and BP belong to routing instances. Keep their host slots synchronized
    // with the topology, without disturbing unrelated user assignments.
    // A removed instance frees all assigned instance switches before new assignments.
    if (!suppressHostStateNotifications.load(std::memory_order_relaxed))
    {
        const auto isInstanceSwitch = [] (const juce::String& id)
        {
            int instanceId = 0;
            juce::String local;
            return ava::routing::parseParameterTarget(id, instanceId, local)
                && (local == paramSoloInstanceId || local == paramInstanceMuteId || local == paramGlobalBypassId);
        };
        for (int slot = 0; slot < assignableHostSlotCount; ++slot)
        {
            const auto key = getHostSlotTargetStateKey(slot);
            const auto assigned = parameters.state.getProperty(key).toString();
            // Only remove switches for routing nodes that were actually deleted.
            // A node may be present in saved topology while its processor is
            // temporarily unavailable during an offline freeze/restore.
            int instanceId = 0;
            juce::String local;
            if (isInstanceSwitch(assigned)
                && ava::routing::parseParameterTarget(assigned, instanceId, local)
                && ava::routing::findStateNode(topology.nodes, instanceId) == nullptr)
                parameters.state.removeProperty(key, nullptr);
        }
        const auto assignIfMissing = [this] (const juce::String& targetId)
        {
            if (findHostSlotTarget(targetId) == nullptr) return;
            int freeSlot = -1;
            // Reserve the end of the SL bank for automatically managed
            // instance S / MT / BP, leaving the first slots available for users.
            for (int slot = assignableHostSlotCount - 1; slot >= 0; --slot)
            {
                const auto current = parameters.state.getProperty(getHostSlotTargetStateKey(slot)).toString();
                if (current == targetId) return;
                if (current.isEmpty() && freeSlot < 0) freeSlot = slot;
            }
            if (freeSlot >= 0)
                parameters.state.setProperty(getHostSlotTargetStateKey(freeSlot), targetId, nullptr);
            // With 64 slots, pre-existing assignments or >21 instances can
            // exhaust the bank; existing mappings always take precedence.
        };
        for (const auto& node : topology.nodes)
            if (!node.container)
            {
                assignIfMissing(ava::routing::bypassTargetId(node.id));
                assignIfMissing(ava::routing::parameterTargetId(node.id, paramInstanceMuteId));
                assignIfMissing(ava::routing::parameterTargetId(node.id, paramSoloInstanceId));
            }
    }
    for (const auto& node : topology.nodes)
        if (ava::routing::isParallelHead(topology, node.id) && !routingCompensationTargets.contains(node.id))
            routingCompensationTargets.emplace(node.id, std::make_unique<DelegatedBoolParameter>(
                ava::routing::compensationTargetId(node.id), "CONTAINER / C",
                [this, id = node.id] {
                    const auto current = ava::routing::readState(parameters.state);
                    const auto* head = ava::routing::findStateNode(current.nodes, id);
                    return head != nullptr && head->compensated ? 1.0f : 0.0f;
                },
                [this, id = node.id](bool enabled) {
                    const juce::ScopedLock stateLock(processingLock);
                    ava::routing::setGroupCompensated(parameters.state, id, enabled);
                    triggerAsyncUpdate();
                }));

    for (int slotIndex = 0; slotIndex < assignableHostSlotCount; ++slotIndex)
    {
        const auto targetParameterId = parameters.state.getProperty(getHostSlotTargetStateKey(slotIndex)).toString();
        auto* target = findHostSlotTarget(targetParameterId);
        const auto index = size_t(slotIndex);
        const auto previousTarget = hostSlotTargets[index].exchange(target, std::memory_order_acq_rel);
        if (previousTarget != target)
            observedHostSlotValues[index].store(-1.0f, std::memory_order_release);
        if (targetParameterId.isEmpty())
        {
            pendingHostSlotValues[index].store(-1.0f, std::memory_order_release);
            observedHostSlotValues[index].store(-1.0f, std::memory_order_release);
        }
        auto* source = target;
        if (const auto instance = ava::routing::bypassTargetInstance(targetParameterId); instance > 0 && target != nullptr)
        {
            if (instance == getRoutingInstanceId())
            {
                if (routingRuntime == nullptr || !routingRuntime->hasMultipleInstances())
                    source = parameters.getParameter(paramGlobalBypassId);
            }
            else if (auto child = routingRuntime->getInstanceHandle(instance))
                source = child->parameters.getParameter(paramGlobalBypassId);
        }
        if (auto* binding = hostSlotBindings[size_t(slotIndex)].get()) {
            binding->bind(source);
            binding->domain = getFilterParameterDomain(targetParameterId);
        }
        // Do not destroy an assignment just because its source is temporarily
        // unresolved. During freeze/unfreeze the routing and child processors
        // are restored in several steps; the next refresh will rebind it.
    }
    syncHostSlotValues();
}

void AvaAudioProcessor::syncHostSlotValues(const bool fromAudioCallback)
{
    if (routingOwner != nullptr) { routingOwner->syncHostSlotValues(fromAudioCallback); return; }
    if (routingInstance) return;
    const juce::ScopedTryLock lock(processingLock);
    if (!lock.isLocked())
    {
        if (!fromAudioCallback) triggerAsyncUpdate();
        return;
    }
    if (synchronisingHostSlots || suppressHostStateNotifications.load(std::memory_order_relaxed)) return;
    const juce::ScopedValueSetter<bool> syncing(synchronisingHostSlots, true);
    hostSlotAutomationPending.store(false, std::memory_order_release);
    if (!fromAudioCallback)
        for (auto& binding : hostSlotBindings) if (binding != nullptr) binding->beginPendingGesture();
    for (int slotIndex = 0; slotIndex < assignableHostSlotCount; ++slotIndex)
    {
        if (hostSlotBindings[size_t(slotIndex)] == nullptr
            || hostSlotBindings[size_t(slotIndex)]->domain.isEditing()
            || hostSlotTargets[size_t(slotIndex)].load(std::memory_order_acquire) == nullptr)
        {
            if (pendingHostSlotValues[size_t(slotIndex)].load(std::memory_order_acquire) >= 0.0f)
                hostSlotAutomationPending.store(true, std::memory_order_release);
            continue;
        }
        const auto requested = pendingHostSlotValues[size_t(slotIndex)].exchange(-1.0f, std::memory_order_acq_rel);
        if (requested >= 0.0f)
            if (auto* target = hostSlotTargets[size_t(slotIndex)].load(std::memory_order_acquire);
                target != nullptr) {
                const auto value = hostSlotBindings[size_t(slotIndex)]->fromSlot(requested);
                if (std::abs(target->getValue() - value) > 1.0e-7f)
                {
                    // This is a host parameter update, not an independent
                    // state edit. Child APVTS and editor listeners fire
                    // synchronously during setValueNotifyingHost().
                    const auto wasApplying = applyingHostSlotValue.exchange(true, std::memory_order_acq_rel);
                    const juce::ScopeGuard finishAutomation { [this, wasApplying]
                    {
                        applyingHostSlotValue.store(wasApplying, std::memory_order_release);
                    } };
                    target->setValueNotifyingHost(value);
                }
                // A discrete or nonlinear target may round the host's value.
                // Do not send that rounded value back to REAPER as a *new*
                // parameter edit, including during a later sync pass.
                hostDrivenTargetValues[size_t(slotIndex)].store(target->getValue(), std::memory_order_release);
            }
    }
    // The realtime fallback only consumes host changes. Mirroring source
    // values back to the host and forwarding gestures belongs off the audio
    // callback; it must not create additional envelope/Undo events on freeze.
    if (fromAudioCallback) return;

    // Resolve by the cached parameter object, not its unqualified ID: EQL
    // parameters in different bands deliberately have identical local IDs.
    for (int slotIndex = 0; slotIndex < assignableHostSlotCount; ++slotIndex)
        if (auto* target = hostSlotTargets[size_t(slotIndex)].load(std::memory_order_acquire))
            if (auto* slot = parameters.getParameter(hostSlotParameterIds[size_t(slotIndex)]);
                slot != nullptr) {
                if (hostSlotBindings[size_t(slotIndex)]->domain.isEditing()) continue;
                const auto targetValue = target->getValue();
                const auto hostDrivenValue = hostDrivenTargetValues[size_t(slotIndex)].load(std::memory_order_acquire);
                if (hostDrivenValue >= 0.0f && std::abs(targetValue - hostDrivenValue) <= 1.0e-7f)
                    continue;
                hostDrivenTargetValues[size_t(slotIndex)].store(-1.0f, std::memory_order_release);
                const auto value = hostSlotBindings[size_t(slotIndex)]->toSlot(targetValue);
                if (std::abs(slot->getValue() - value) > 1.0e-7f) slot->setValueNotifyingHost(value);
            }
    for (auto& binding : hostSlotBindings) if (binding != nullptr) binding->endPendingGesture();
}

float AvaAudioProcessor::getRoutingInstanceBypassValue(int instanceId) const
{
    const juce::ScopedLock lock(processingLock);
    if (routingRuntime == nullptr) return -1.0f;
    if (instanceId == routingRuntime->getRootInstanceId())
    {
        if (routingRuntime->hasMultipleInstances())
            return routingRuntime->isRootBypassed() ? 1.0f : 0.0f;
        if (auto* bypass = parameters.getParameter(paramGlobalBypassId)) return bypass->getValue();
    }
    else if (auto instance = routingRuntime->getInstanceHandle(instanceId))
        if (auto* bypass = instance->parameters.getParameter(paramGlobalBypassId)) return bypass->getValue();
    return -1.0f;
}

void AvaAudioProcessor::setRoutingInstanceBypassValue(int instanceId, bool bypassed)
{
    const juce::ScopedLock lock(processingLock);
    if (routingRuntime == nullptr) return;
    auto instance = instanceId == routingRuntime->getRootInstanceId()
        ? std::shared_ptr<AvaAudioProcessor> {} : routingRuntime->getInstanceHandle(instanceId);
    auto* target = instanceId == routingRuntime->getRootInstanceId() ? this : instance.get();
    if (target == nullptr || (!bypassed && target->isInstanceSoloBypassed())) return;
    if (target == this && routingRuntime->hasMultipleInstances())
    {
        routingRuntime->setRootBypassed(bypassed);
        parameters.state.setProperty(ava::routing::rootBypassStateKey, bypassed, nullptr);
        triggerAsyncUpdate();
    }
    else if (auto* parameter = target->parameters.getParameter(paramGlobalBypassId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(bypassed ? 1.0f : 0.0f);
        parameter->endChangeGesture();
    }
    triggerAsyncUpdate();
}

void AvaAudioProcessor::applyHostSlotValue(const int slotIndex, const float normalizedValue) noexcept
{
    if (!juce::isPositiveAndBelow(slotIndex, hostAutomationSlotCount) || !std::isfinite(normalizedValue)) return;
    // Always retain the last host value, even if a route rebuild or another
    // slot synchronisation is in progress. REAPER's offline freeze does not
    // guarantee that queued message-thread callbacks will run during render.
    pendingHostSlotValues[size_t(slotIndex)].store(juce::jlimit(0.0f, 1.0f, normalizedValue), std::memory_order_release);
    hostSlotAutomationPending.store(true, std::memory_order_release);
    // VST3 may call parameter listeners on the audio/offline-render thread.
    // Never mirror module values back into host slots on that thread: a
    // feedback edit can overwrite REAPER's envelope while freezing/rendering.
    // processBlock() consumes the pending value before rendering audio.
    auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (messageManager != nullptr && messageManager->isThisTheMessageThread())
        syncHostSlotValues();
    else if (!processingPrepared.load(std::memory_order_acquire)
             || hostSlotTargets[size_t(slotIndex)].load(std::memory_order_acquire) == nullptr)
        triggerAsyncUpdate();
}

