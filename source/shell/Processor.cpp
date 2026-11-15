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

namespace
{
// Expose a state-backed boolean through the shared slot/OSC parameter path.
// Routing targets resolve their processor by ID instead of retaining a removable child.
class DelegatedBoolParameter final : public juce::AudioParameterBool
{
public:
    DelegatedBoolParameter(juce::String id, juce::String parameterName, std::function<float()> readIn, std::function<void(bool)> writeIn)
        : AudioParameterBool(juce::ParameterID { id, 1 }, parameterName, false),
          read(std::move(readIn)), write(std::move(writeIn)) {}
    float getValue() const override { return juce::jmax(0.0f, read()); }
    void setValue(float value) override { write(value >= 0.5f); }
private:
    std::function<float()> read;
    std::function<void(bool)> write;
};

}

// A slot observes the actual source parameter, including UI gestures. Values
// still pass through the single synchronizer; listener callbacks never wait on
// the processing lock, including while a module is being detached.
struct AvaAudioProcessor::HostSlotBinding final : juce::AudioProcessorParameter::Listener
{
    HostSlotBinding(AvaAudioProcessor& ownerIn, int slotIn) : owner(ownerIn), slot(slotIn) {}
    ~HostSlotBinding() override { bind(nullptr); }
    void bind(juce::RangedAudioParameter* source)
    {
        if (source == parameter) return;
        if (parameter != nullptr) parameter->removeListener(this);
        pendingGestures.store(0, std::memory_order_release);
        pendingEnd = false;
        if (gestureActive)
        {
            gestureActive = false;
            owner.parameters.getParameter(owner.hostSlotParameterIds[size_t(slot)])->endChangeGesture();
        }
        parameter = source;
        domain = {};
        // Binding a different source must not inherit the old target's echo guard.
        owner.hostDrivenTargetValues[size_t(slot)].store(-1.0f, std::memory_order_release);
        if (parameter != nullptr) parameter->addListener(this);
    }
    float toSlot(float sourceValue) const noexcept {
        const auto view = domain.presentation();
        if (parameter == nullptr || view.limits.isEmpty()) return sourceValue;
        const auto display = view.toDisplay(parameter->convertFrom0to1(sourceValue));
        return float(juce::jlimit(0.0, 1.0, (display - view.limits.getStart()) / view.limits.getLength()));
    }
    float fromSlot(float value) const noexcept {
        const auto view = domain.presentation();
        if (parameter == nullptr || view.limits.isEmpty()) return value;
        return parameter->convertTo0to1(float(view.toPlain(view.limits.getStart() + value * view.limits.getLength())));
    }
    FilterParameterDomain domain;
    void parameterValueChanged(int, float) override { owner.syncHostSlotValues(); }
    void parameterGestureChanged(int, bool active) override
    {
        const juce::ScopedTryLock lock(owner.processingLock);
        if (lock.isLocked() && (owner.synchronisingHostSlots
            || owner.suppressHostStateNotifications.load(std::memory_order_relaxed))) return;
        pendingGestures.fetch_or(active ? 1u : 2u, std::memory_order_acq_rel);
        owner.syncHostSlotValues();
    }
    void beginPendingGesture()
    {
        const auto pending = pendingGestures.exchange(0, std::memory_order_acq_rel);
        if ((pending & 1u) != 0 && !gestureActive)
        {
            gestureActive = true;
            owner.parameters.getParameter(owner.hostSlotParameterIds[size_t(slot)])->beginChangeGesture();
        }
        pendingEnd = (pending & 2u) != 0;
    }
    void endPendingGesture()
    {
        if (!pendingEnd || !gestureActive) return;
        pendingEnd = gestureActive = false;
        owner.parameters.getParameter(owner.hostSlotParameterIds[size_t(slot)])->endChangeGesture();
    }
    AvaAudioProcessor& owner;
    const int slot;
    juce::RangedAudioParameter* parameter = nullptr;
    std::atomic<unsigned> pendingGestures {0};
    bool gestureActive = false, pendingEnd = false;
};

AvaAudioProcessor::AvaAudioProcessor(const bool routingInstanceIn)
    : AudioProcessor([]
      {
          auto buses = BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
          for (int channel = 4; channel < routingChannelCount; channel += 2)
              buses = buses.withInput("Input " + juce::String(channel + 1) + "-" + juce::String(channel + 2),
                                      juce::AudioChannelSet::stereo(), false);
          return buses.withOutput("Output", juce::AudioChannelSet::stereo(), true);
      }()),
    parameters(*this, nullptr, "ava_state", createParameterLayout()),
    routingInstance(routingInstanceIn)
{
    for (auto& order : bandModuleOrders)
        order.store(module_order::pack(module_order::defaultOrder), std::memory_order_relaxed);
    for (size_t band = 0; band < bandHiddenMasks.size(); ++band)
    {
        bandHiddenMasks[band].store(module_order::defaultHiddenMask, std::memory_order_relaxed);
        for (int index = 0; index < 2; ++index)
        {
            const int id = index == 0 ? module_order::eql : module_order::dyn;
            const auto key = module_order::bandHiddenKey(band, id);
            bandVisibilityParameters[band][size_t(index)] = std::make_unique<DelegatedBoolParameter>(
                key, juce::String(id == module_order::eql ? "EQL" : "DYN") + "-BAND-" + juce::String(int(band + 1)) + "-HIDDEN",
                [this, band, id] { return isModuleHiddenForRange(band, id) ? 1.0f : 0.0f; },
                [this, band, id](bool hidden) { setModuleHiddenForRange(band, id, hidden); });
        }
    }
    for (const auto& module : module_bypass::descriptions)
        moduleBypassTargets[size_t(module.id)] = std::make_unique<DelegatedBoolParameter>(
            module.bypassStateKey, juce::String(module.name) + "-BYPASS",
            [this, id = module.id] { return (moduleBypasses.load(std::memory_order_acquire) & (1u << id)) != 0 ? 1.0f : 0.0f; },
            [this, id = module.id](bool bypassed) { setModuleBypassed(id, bypassed); });
    if (! routingInstance)
        oscController = std::make_unique<OscController>(*this);
    globalBypassParam = parameters.getRawParameterValue(paramGlobalBypassId);
    bypassCompensationParam = parameters.getRawParameterValue(paramBypassCompensationId);
    instanceMuteParam = parameters.getRawParameterValue(paramInstanceMuteId);
    processingInputParam = parameters.getRawParameterValue(paramProcessingInputId);
    soloInstanceParam = parameters.getRawParameterValue(paramSoloInstanceId);
    soloChannelParam = parameters.getRawParameterValue(paramSoloChannelId);
    crossoverActiveSplitCountParam = parameters.getRawParameterValue(paramCrossoverActiveSplitCountId);

    for (size_t splitIndex = 0; splitIndex < crossoverSplitFrequencyParams.size(); ++splitIndex)
    {
        const auto suffix = "split-" + juce::String(static_cast<int>(splitIndex + 1));
        crossoverSplitFrequencyParams[splitIndex] = parameters.getRawParameterValue(getCrossoverParameterId(suffix.toRawUTF8()));
    }

    for (size_t rangeIndex = 0; rangeIndex < crossoverSoloParams.size(); ++rangeIndex)
        crossoverSoloParams[rangeIndex] = parameters.getRawParameterValue(getCrossoverSoloParameterId(rangeIndex));

    for (int slotIndex = 0; slotIndex < assignableHostSlotCount; ++slotIndex)
    {
        hostSlotParameterIds[static_cast<size_t>(slotIndex)] = getHostSlotParameterId(slotIndex);
        hostSlotParameters[size_t(slotIndex)] = parameters.getParameter(hostSlotParameterIds[size_t(slotIndex)]);
        observedHostSlotValues[size_t(slotIndex)].store(-1.0f, std::memory_order_relaxed);
        pendingHostSlotValues[size_t(slotIndex)].store(-1.0f, std::memory_order_relaxed);
        hostDrivenTargetValues[size_t(slotIndex)].store(-1.0f, std::memory_order_relaxed);
        if (!routingInstance)
            hostSlotBindings[size_t(slotIndex)] = std::make_unique<HostSlotBinding>(*this, slotIndex);
    }


    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            parameters.addParameterListener(ranged->getParameterID(), this);
    parameters.state.addListener(this);

    if (! routingInstance)
        routingRuntime = std::make_shared<ava::routing::Runtime>(*this);

    for (const auto module : availableModules) loadModule(module);
    // Initialise both the original slots and their creation counters.  A
    // zero counter makes ADD generate serial 1, which insertCopy rejects.
    resetModuleCopies();
    publishRenderGraph();
    if (!routingInstance)
    {
        // Saved identity lets manually selected links reconnect after the DAW
        // restores each independent instance, regardless of loading order.
        parameters.state.setProperty(linkUuidStateKey, juce::Uuid().toString(), nullptr);
        linkSessionId = InstanceLinkRegistry::instance().attach(*this);
    }
}

AvaAudioProcessor::~AvaAudioProcessor()
{
    if (linkSessionId != 0) InstanceLinkRegistry::instance().detach(linkSessionId);
    cancelPendingUpdate();
    if (!routingInstance) invalidateHostSlotTargets();
    oscController.reset();
    parameters.state.removeListener(this);
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            parameters.removeParameterListener(ranged->getParameterID(), this);

    clearLoadedModuleStateListeners();
    activeRenderGraph.store(nullptr);
    retiredRenderGraphs.clear();
    publishedRenderGraph.reset();
    routingRuntime.reset();
    eqlProcessorBank.reset(); blockChainModuleProcessor.reset(); dynModuleProcessor.reset();
}

std::shared_ptr<AvaAudioProcessor> AvaAudioProcessor::getRoutingInstanceHandle(const int instanceId) noexcept
{
    const juce::ScopedLock lock(processingLock);
    return routingRuntime == nullptr ? nullptr : routingRuntime->getInstanceHandle(instanceId);
}

bool AvaAudioProcessor::isInstanceSoloBypassed() const
{
    const auto& owner = getRoutingOwner();
    const juce::ScopedLock lock(owner.processingLock);
    const auto* solo = owner.routingRuntime != nullptr ? owner.routingRuntime->getSoloProcessor(false) : nullptr;
    return solo != nullptr && solo != this;
}

void AvaAudioProcessor::synchronizeRouting()
{
    if (routingRuntime == nullptr) return;
    const auto wasRestoring = restoringModel.exchange(true);
    const juce::ScopeGuard routingReady {[this, wasRestoring] { restoringModel.store(wasRestoring); }};

    auto nextTopology = ava::routing::readState(parameters.state);
    if (! routingRuntime->hasMultipleInstances() && ava::routing::getInstanceCount(nextTopology) > 1
        && nextTopology.rootInstanceId == routingRuntime->getRootInstanceId())
    {
        if (auto* bypass = parameters.getParameter(paramGlobalBypassId); bypass != nullptr && bypass->getValue() >= 0.5f)
        {
            parameters.state.setProperty(ava::routing::rootBypassStateKey, true, nullptr);
            nextTopology.rootBypassed = true;
            bypass->setValueNotifyingHost(0.0f);
        }
    }
    if (ava::routing::getInstanceCount(nextTopology) == 1 && nextTopology.rootBypassed)
    {
        if (auto* bypass = parameters.getParameter(paramGlobalBypassId))
            bypass->setValueNotifyingHost(1.0f);
        parameters.state.removeProperty(ava::routing::rootBypassStateKey, nullptr);
        nextTopology.rootBypassed = false;
    }
    if (nextTopology.rootInstanceId != routingRuntime->getRootInstanceId())
    {
        if (const auto promoted = routingRuntime->getInstanceHandle(nextTopology.rootInstanceId))
        {
            juce::MemoryBlock processorState;
            promoted->getStateInformation(processorState);
            const auto xml = getXmlFromBinary(processorState.getData(),
                                               static_cast<int>(processorState.getSize()));
            if (xml != nullptr)
            {
                auto promotedState = juce::ValueTree::fromXml(*xml);
                auto currentState = parameters.copyState();
                routingRuntime->writeProcessorStates(currentState, nextTopology.rootInstanceId);
                for (const auto* key : { ava::routing::instancesStateKey,
                                         ava::routing::nextInstanceIdStateKey,
                                         ava::routing::rootInstanceIdStateKey,
                                         ava::routing::rootBypassStateKey,
                                         ava::routing::namesStateKey,
                                         ava::routing::processorStatesKey,
                                         editorWidthStateKey,
                                         editorHeightStateKey,
                                         oscEnabledStateKey,
                                         oscInputPortStateKey,
                                         oscOutputHostStateKey,
                                         oscOutputPortStateKey,
                                         linkUuidStateKey,
                                         linkTargetsStateKey,
                                         linkCountStateKey,
                                         linkSendStateKey,
                                         linkReceiveStateKey })
                {
                    if (currentState.hasProperty(key))
                        promotedState.setProperty(key, currentState.getProperty(key), nullptr);
                    else
                        promotedState.removeProperty(key, nullptr);
                }
                // Promoting another routing node to root must not discard
                // directed LINK INSTANCE selections stored on the owner.
                for (int propertyIndex = 0; propertyIndex < currentState.getNumProperties(); ++propertyIndex)
                {
                    const auto property = currentState.getPropertyName(propertyIndex);
                    if (property.toString().startsWith("ava.link.routing."))
                        promotedState.setProperty(property, currentState.getProperty(property), nullptr);
                }
                if (const auto promotedXml = promotedState.createXml())
                {
                    for (int slot = 0; slot < assignableHostSlotCount; ++slot)
                    {
                        const auto key = getHostSlotTargetStateKey(slot);
                        const auto target = currentState.getProperty(key).toString();
                        int instance = 0;
                        juce::String local;
                        const auto qualified = ava::routing::parseParameterTarget(target, instance, local);
                        const auto* node = qualified ? ava::routing::findStateNode(nextTopology.nodes, instance) : nullptr;
                        const auto keep = target.isNotEmpty()
                            && (qualified ? node != nullptr && !node->container : parameters.getParameter(target) != nullptr);
                        if (keep) promotedXml->setAttribute(key, target);
                        else promotedXml->removeAttribute(key);
                        const auto id = getHostSlotParameterId(slot);
                        for (auto* child : promotedXml->getChildIterator())
                            if (child->hasTagName("PARAM") && child->getStringAttribute("id") == id)
                                if (const auto value = ava::modules::state::readParameterPlainValue(currentState, id))
                                    child->setAttribute("value", keep ? *value : 0.0f);
                    }
                    preserveEditorWindowState(*promotedXml, currentState);
                    juce::MemoryBlock mergedState;
                    copyXmlToBinary(*promotedXml, mergedState);
                    if (restoreStateInformation(mergedState.getData(),
                                                static_cast<int>(mergedState.getSize()), true, false))
                    {
                        notifyHostOfStateChange();
                        return;
                    }
                }
            }
        }
    }
    const juce::ScopedLock lock(processingLock);
    auto nextRuntime = std::make_shared<ava::routing::Runtime>(*this, routingRuntime.get());
    nextRuntime->synchronize(parameters.state);
    if (currentSampleRate > 0.0) nextRuntime->prepare(currentSampleRate, preparedBlockSize, routingChannelCount);
    routingRuntime = std::move(nextRuntime);
    publishRenderGraph();
    refreshHostSlotTargets();
    updateShellLatency();
    notifyHostOfStateChange();
}

OscSettings AvaAudioProcessor::getOscSettings() const
{
    if (routingOwner != nullptr)
        return routingOwner->getOscSettings();
    OscSettings settings;
    const auto& state = parameters.state;
    settings.enabled = state.hasProperty(oscEnabledStateKey)
        ? static_cast<bool>(state.getProperty(oscEnabledStateKey))
        : false;
    settings.inputPort = static_cast<int>(state.getProperty(oscInputPortStateKey,
                                                            OscSettings::defaultInputPort));
    settings.outputHost = state.getProperty(oscOutputHostStateKey, "127.0.0.1").toString();
    settings.outputPort = static_cast<int>(state.getProperty(oscOutputPortStateKey,
                                                             OscSettings::defaultOutputPort));

    return settings;
}

bool AvaAudioProcessor::setOscSettings(const OscSettings& settings)
{
    if (routingOwner != nullptr)
        return routingOwner->setOscSettings(settings);
    if (oscController == nullptr || ! oscController->applySettings(settings))
        return false;

    parameters.state.setProperty(oscEnabledStateKey, settings.enabled, nullptr);
    parameters.state.setProperty(oscInputPortStateKey, settings.inputPort, nullptr);
    parameters.state.setProperty(oscOutputHostStateKey, settings.outputHost.trim(), nullptr);
    parameters.state.setProperty(oscOutputPortStateKey, settings.outputPort, nullptr);
    notifyHostOfStateChange();
    return true;
}

void AvaAudioProcessor::refreshOscConfiguration()
{
    if (oscController == nullptr)
        return;

    OscSettings settings;
    const auto& state = parameters.state;
    settings.enabled = state.hasProperty(oscEnabledStateKey)
        && static_cast<bool>(state.getProperty(oscEnabledStateKey));
    settings.inputPort = static_cast<int>(state.getProperty(oscInputPortStateKey,
                                                            OscSettings::defaultInputPort));
    settings.outputHost = state.getProperty(oscOutputHostStateKey, "127.0.0.1").toString();
    settings.outputPort = static_cast<int>(state.getProperty(oscOutputPortStateKey,
                                                             OscSettings::defaultOutputPort));
    oscController->applySettingsAsync(settings);
}

bool AvaAudioProcessor::isOscInputPortBusy() const noexcept
{
    if (routingOwner != nullptr)
        return routingOwner->isOscInputPortBusy();
    return oscController != nullptr && oscController->isInputPortBusy();
}

int AvaAudioProcessor::getRoutingInstanceId() const noexcept
{
    return routingInstance ? routingNodeId : routingRuntime != nullptr ? routingRuntime->getRootInstanceId() : 1;
}

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
    // The outer BP is the whole plugin; per-node BP uses the routing target.
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

juce::String AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(const size_t rangeIndex)
{
    const auto clampedRange = juce::jmin(rangeIndex, ava::crossover::BufferRouter::numRanges - 1);
    return "ava.editor.eql.band_" + juce::String(static_cast<int>(clampedRange)) + ".filter_display_order";
}

juce::String AvaAudioProcessor::getCrossoverParameterId(const char* suffix)
{
    return suffix;
}

juce::String AvaAudioProcessor::getCrossoverSoloParameterId(const size_t rangeIndex)
{
    return "band-" + juce::String(static_cast<int>(rangeIndex + 1)) + "_solo";
}

ava::crossover::Settings AvaAudioProcessor::getCrossoverSettings() const noexcept
{
    if (renderingOwner == this && renderingGraph != nullptr && renderingGraph->buffers != nullptr)
        return audioBuffers().settings;
    ava::crossover::Settings settings;

    if (crossoverActiveSplitCountParam != nullptr)
        settings.activeSplitCount = static_cast<size_t>(juce::jlimit(0,
                                                                    static_cast<int>(ava::crossover::BufferRouter::numSplits),
                                                                    juce::roundToInt(crossoverActiveSplitCountParam->load(std::memory_order_relaxed))));

    for (size_t index = 0; index < settings.splitFrequencies.size(); ++index)
        if (const auto* value = crossoverSplitFrequencyParams[index])
            settings.splitFrequencies[index] = value->load(std::memory_order_relaxed);

    for (size_t index = 0; index < settings.soloMask.size(); ++index)
        if (const auto* value = crossoverSoloParams[index])
            settings.soloMask[index] = value->load(std::memory_order_relaxed) >= 0.5f;

    return settings;
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
    // MT and BP belong to routing instances. Keep their host slots synchronized
    // with the topology, without disturbing unrelated user assignments.
    // A removed instance frees both slots before a newly inserted one is assigned.
    if (!suppressHostStateNotifications.load(std::memory_order_relaxed))
    {
        const auto isInstanceSwitch = [] (const juce::String& id)
        {
            int instanceId = 0;
            juce::String local;
            return ava::routing::parseParameterTarget(id, instanceId, local)
                && (local == paramInstanceMuteId || local == paramGlobalBypassId);
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
            for (int slot = 0; slot < assignableHostSlotCount; ++slot)
            {
                const auto current = parameters.state.getProperty(getHostSlotTargetStateKey(slot)).toString();
                if (current == targetId) return;
                if (current.isEmpty() && freeSlot < 0) freeSlot = slot;
            }
            if (freeSlot >= 0)
                parameters.state.setProperty(getHostSlotTargetStateKey(freeSlot), targetId, nullptr);
            // With 64 slots, pre-existing assignments or >32 instances can
            // exhaust the bank; existing mappings always take precedence.
        };
        for (const auto& node : topology.nodes)
            if (!node.container)
            {
                assignIfMissing(ava::routing::parameterTargetId(node.id, paramInstanceMuteId));
                assignIfMissing(ava::routing::bypassTargetId(node.id));
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

juce::AudioProcessorValueTreeState::ParameterLayout AvaAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> parameterLayout;



    for (int slotIndex = 0; slotIndex < assignableHostSlotCount; ++slotIndex)
    {
        parameterLayout.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { getHostSlotParameterId(slotIndex), 2 },
            getHostSlotLetterLabel(slotIndex),
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.0f },
            0.0f,
            juce::AudioParameterFloatAttributes().withAutomatable(true)));
    }

    parameterLayout.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { paramCrossoverActiveSplitCountId, 1 },
        "CROSSOVER / COUNT",
        0,
        static_cast<int>(ava::crossover::BufferRouter::numSplits),
        0,
        juce::AudioParameterIntAttributes().withAutomatable(false).withMeta(true)));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { paramProcessingInputId, 1 }, "CROSSOVER / CH-IN",
        getProcessingInputChoices(), 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));

    for (const auto* id : { paramSoloInstanceId, paramSoloChannelId })
        parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { id, 1 }, "CROSSOVER / " + juce::String(id).toUpperCase(), false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramInstanceMuteId, 1 }, "INSTANCE / MT", false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    constexpr std::array<float, 5> splitFrequencyDefaults { 134.0f, 523.0f, 2093.0f, 5000.0f, 10000.0f };
    static_assert(splitFrequencyDefaults.size() == ava::crossover::BufferRouter::numSplits);

    for (size_t index = 0; index < splitFrequencyDefaults.size(); ++index)
    {
        const auto suffix = "split-" + juce::String(static_cast<int>(index + 1));
        parameterLayout.push_back(ava::modules::parameters::makeFloatParameter(
            getCrossoverParameterId(suffix.toRawUTF8()),
            "CROSSOVER / SPLIT-" + juce::String(static_cast<int>(index + 1)),
            juce::NormalisableRange<float> { 20.0f, 20000.0f, 0.01f }, splitFrequencyDefaults[index],
            [] (float value) -> ava::modules::parameters::ValueText
                { return ava::modules::parameters::formatFixedDecimalValue(value, 2); }, {}, false,
            juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));
    }

    for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
    {
        parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { getCrossoverSoloParameterId(rangeIndex), 1 },
            "BAND " + juce::String(static_cast<int>(rangeIndex + 1)) + " / SOLO",
            false,
            juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));
    }

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { getCrossoverParameterId("auto-solo"), 1 },
        "CROSSOVER / AUTO-SOLO",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramGlobalBypassId, 1 },
        "AVA / GLOBAL / BP",
        false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

    // Gain applied only to the unprocessed signal while global BYPASS is on.
    // Keep the full [-96, +96] dB range available for numerical entry.
    parameterLayout.push_back(ava::modules::parameters::makeFloatParameter(
        paramBypassCompensationId, "AVA / GLOBAL / BYPASS COMPENSATION",
        juce::NormalisableRange<float> { -96.0f, 96.0f, 0.01f }, 0.0f,
        [] (float value) -> ava::modules::parameters::ValueText
        { return ava::modules::parameters::formatFixedDecimalValue(value, 2); }, "dB"));

    parameterLayout.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { paramSyncTypeId, 1 }, "INSTANCE / NOTE SYNC",
        juce::StringArray { "HOST", "MANUAL" }, 0,
        juce::AudioParameterChoiceAttributes().withAutomatable(false).withMeta(true)));
    parameterLayout.push_back(ava::modules::parameters::makeFloatParameter(
        paramBpmId, "INSTANCE / MANUAL BPM", juce::NormalisableRange<float> { 1.0f, 1000.0f, 0.01f }, 120.0f,
        [] (float value) -> ava::modules::parameters::ValueText
            { return ava::modules::parameters::formatFixedDecimalValue(value, 2); }, {}, false,
        juce::AudioParameterFloatAttributes().withAutomatable(false).withMeta(true)));

    return { parameterLayout.begin(), parameterLayout.end() };
}

const juce::String AvaAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AvaAudioProcessor::acceptsMidi() const
{
    return false;
}

bool AvaAudioProcessor::producesMidi() const
{
    return false;
}

bool AvaAudioProcessor::isMidiEffect() const
{
    return false;
}

double AvaAudioProcessor::getTailLengthSeconds() const
{
    return tailLengthSeconds.load(std::memory_order_relaxed);
}

int AvaAudioProcessor::getNumPrograms()
{
    return 1;
}

int AvaAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AvaAudioProcessor::setCurrentProgram(int)
{
}

const juce::String AvaAudioProcessor::getProgramName(int)
{
    return {};
}

void AvaAudioProcessor::changeProgramName(int, const juce::String&)
{
}

FilterParameterDomain AvaAudioProcessor::getFilterParameterDomain(const juce::String& targetId) const
{
    auto id = targetId;
    const AvaAudioProcessor* processor = this;
    std::shared_ptr<AvaAudioProcessor> child;
    int instance = 0; juce::String local;
    if (ava::routing::parseParameterTarget(id, instance, local)) {
        const auto& owner = getRoutingOwner();
        processor = &owner;
        if (instance != owner.getRoutingInstanceId()) {
            child = owner.routingRuntime != nullptr ? owner.routingRuntime->getInstanceHandle(instance) : nullptr;
            processor = child.get();
        }
        id = local;
    }
    if (processor == nullptr) return {};
    size_t band = processor->getSelectedCrossoverRange(); juce::String suffix;
    int copyUid = -1;
    juce::String copyLocal;
    const EqlProcessorBank* bank = processor->getEqlProcessorBank();
    if (ava::module_copy_targets::parse(id, copyUid, copyLocal))
    {
        // Explicit BAND is required for copied EQL parameters: silently
        // selecting the current editor band would retarget host automation.
        if (!parseOscBandParameterId(copyLocal, band, suffix)) return {};
        id = suffix;
        const auto it = processor->moduleCopies.find(copyUid);
        bank = it != processor->moduleCopies.end() ? it->second->eql.get() : nullptr;
    }
    else if (parseOscBandParameterId(id, band, suffix)) id = suffix;
    if (!id.startsWith("filter-")) return {};
    const auto* module = bank != nullptr ? bank->getProcessor(band) : nullptr;
    if (module == nullptr) return {};
    FilterParameterDomain domain;
    domain.model = module;
    domain.kind = id.endsWith("_gain") ? FilterParameterDomain::gain
        : id.endsWith("_freq") ? FilterParameterDomain::frequency : FilterParameterDomain::none;
    if (domain.kind == FilterParameterDomain::none) return {};
    const auto filter = id.substring(7).getIntValue() - 1;
    const auto& state = module->getValueTreeState();
    domain.type = state.getRawParameterValue(EqlModuleProcessor::getFilterTypeParamId(filter));
    domain.order = state.getRawParameterValue(EqlModuleProcessor::getFilterOrderParamId(filter));
    domain.phase = state.getRawParameterValue(EqlModuleProcessor::getFilterPhaseParamId(filter));
    return domain;
}
