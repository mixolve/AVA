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
    outputMuteParam = parameters.getRawParameterValue(paramOutputMuteId);
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
    for (size_t band = 0; band < moduleStacks.size(); ++band)
        for (const auto& slot : moduleStacks[band])
            registerModuleSlotBypassTarget(band, slot.uid, slot.type);
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
                                                static_cast<int>(mergedState.getSize()), true))
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

    parameterLayout.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { paramOutputMuteId, 1 }, "INSTANCE / OUT MUTE", false,
        juce::AudioParameterBoolAttributes().withAutomatable(false).withMeta(true)));

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
