#include "Processor.h"
#include "EditorStateSchema.h"
#include "StateValidation.h"
#include "ABCompareSerialization.h"
#include "FilterOrderState.h"
#include "Style.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/StateValidation.h"
#include "../modules/shared/StateValidation.h"
#include "../crossover/UiStateSchema.h"
#include "../modules/eql/Processor.h"
#include "../routing/State.h"

#include <cmath>
#include <optional>

namespace
{
bool hasCurrentActiveModuleId(const juce::ValueTree& state)
{
    if (! state.hasProperty(AvaAudioProcessor::activeModuleStateKey))
        return true;

    const auto moduleId = state.getProperty(AvaAudioProcessor::activeModuleStateKey).toString();

    for (const auto module : AvaAudioProcessor::availableModules)
        if (moduleId == AvaAudioProcessor::stateIdForModule(module))
            return true;

    return false;
}
bool isCurrentShellProperty(const juce::Identifier& property)
{
    if(property==juce::Identifier(AvaAudioProcessor::hostSlotDefaultsKey))return true;
    for (size_t band = 0; band < ava::crossover::BufferRouter::numRanges; ++band)
    {
        if (property == juce::Identifier(module_order::bandStateKey(band))) return true;
        for (const auto id : {module_order::eql, module_order::dyn})
            if (property == juce::Identifier(module_order::bandHiddenKey(band, id))) return true;
    }
    for (const auto& module : module_bypass::descriptions)
        if (property == juce::Identifier(module.bypassStateKey)) return true;
    if (crossover_ui::isCurrentStateProperty(property, "crossover"))
        return true;

    for (const auto* key : { AvaAudioProcessor::activeModuleStateKey,
                             AvaAudioProcessor::eqlModuleStateKey,
                             AvaAudioProcessor::blockChainModuleStateKey,
                             AvaAudioProcessor::dynModuleStateKey,
                             AvaAudioProcessor::abCompareSnapshotAStateKey,
                             AvaAudioProcessor::abCompareSnapshotBStateKey,
                             AvaAudioProcessor::abCompareActiveSlotStateKey,
                             AvaAudioProcessor::editorWidthStateKey,
                             AvaAudioProcessor::editorHeightStateKey,
                             ava::routing::instancesStateKey,
                             ava::routing::nextInstanceIdStateKey,
                             ava::routing::rootInstanceIdStateKey,
                             ava::routing::rootBypassStateKey,
                             ava::routing::namesStateKey,
                             ava::routing::processorStatesKey,
                             AvaAudioProcessor::oscEnabledStateKey,
                             AvaAudioProcessor::oscInputPortStateKey,
                             AvaAudioProcessor::oscOutputHostStateKey,
                             AvaAudioProcessor::oscOutputPortStateKey })
    {
        if (property == juce::Identifier(key))
            return true;
    }

    for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
        if (property == juce::Identifier(AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(rangeIndex)))
            return true;

    for (int slotIndex = 0; slotIndex < AvaAudioProcessor::assignableHostSlotCount; ++slotIndex)
    {
        if (property == juce::Identifier(AvaAudioProcessor::getHostSlotTargetStateKey(slotIndex)))
            return true;
    }

    return false;
}

std::optional<size_t> getRequiredCrossoverRangeCount(const juce::ValueTree& state)
{
    const auto splitCount = ava::modules::state::readParameterPlainValue(
        state,
        AvaAudioProcessor::paramCrossoverActiveSplitCountId);

    if (! splitCount.has_value())
        return std::nullopt;

    const auto roundedSplitCount = juce::roundToInt(*splitCount);

    if (std::abs(*splitCount - static_cast<float>(roundedSplitCount)) > 1.0e-6f
        || roundedSplitCount < 0
        || roundedSplitCount >= static_cast<int>(ava::crossover::BufferRouter::numRanges))
        return std::nullopt;

    return static_cast<size_t>(roundedSplitCount + 1);
}

bool hasCurrentShellInvariants(const juce::ValueTree& state)
{
    const auto requiredRangeCount = getRequiredCrossoverRangeCount(state);

    if (! requiredRangeCount.has_value())
        return false;

    const auto visibleRangeKey = crossover_ui::makeStatePropertyId("crossover", "visible_band_index");

    if (state.hasProperty(visibleRangeKey)
        && static_cast<size_t>(static_cast<int>(state.getProperty(visibleRangeKey))) >= *requiredRangeCount)
        return false;

    return true;
}

bool hasCurrentHostSlotTargets(const juce::ValueTree& state)
{
    juce::StringArray targets;
    for (int slot = 0; slot < AvaAudioProcessor::assignableHostSlotCount; ++slot) {
        const auto key = AvaAudioProcessor::getHostSlotTargetStateKey(slot);
        if (!state.hasProperty(key)) continue;
        const auto target = state.getProperty(key).toString();
        int instance = 0; juce::String local;
        if (target.isEmpty() || target.startsWith(AvaAudioProcessor::paramHostSlotPrefix) || targets.contains(target)
            || (ava::routing::parseParameterTarget(target, instance, local) && local.startsWith(AvaAudioProcessor::paramHostSlotPrefix))) return false;
        targets.add(target);
    }
    // Availability belongs to the resolver: it clears removed targets without
    // rejecting every other saved parameter in the plugin.
    return true;
}

bool hasCurrentSerializedModuleState(const juce::ValueTree& state)
{
    // Validate the session itself independently of optional CHAIN/DYN/EQL
    // blobs. A saved EQL preset from another version must not cause the host
    // to reject every other control, especially LISTEN, on reopening.
    // Each blob is checked by its owning module during restoration instead.
    return hasCurrentHostSlotTargets(state);
}

bool hasCurrentShellMetadata(const juce::ValueTree& state,
                             AvaAudioProcessor& processor,
                             const bool includeABCompareState)
{
    juce::ignoreUnused(processor);
    for (const auto& module : module_bypass::descriptions)
        if (state.hasProperty(module.bypassStateKey)
            && !crossover_ui::hasExactBooleanValue(state.getProperty(module.bypassStateKey))) return false;
    for (size_t band = 0; band < ava::crossover::BufferRouter::numRanges; ++band)
    {
        const auto key = module_order::bandStateKey(band);
        if (state.hasProperty(key))
        {
            module_order::Order order;
            if (!module_order::parse(state.getProperty(key).toString(), order)) return false;
        }
        for (const auto id : {module_order::eql, module_order::dyn})
        {
            const auto hiddenKey = module_order::bandHiddenKey(band, id);
            if (state.hasProperty(hiddenKey)
                && !crossover_ui::hasExactBooleanValue(state.getProperty(hiddenKey))) return false;
        }
    }
    for (int propertyIndex = 0; propertyIndex < state.getNumProperties(); ++propertyIndex)
    {
        const auto property = state.getPropertyName(propertyIndex);

        if(property==juce::Identifier(AvaAudioProcessor::hostSlotDefaultsKey) && !crossover_ui::hasExactBooleanValue(state.getProperty(property)))return false;
        if (const auto valid = shell_editor_ui::validate(property, state.getProperty(property)); valid.has_value())
        {
            if (!*valid) return false;
            continue;
        }
        // Only current known state fields are validated here; other namespaces
        // are owned and validated by their respective components.
        if (! isCurrentShellProperty(property))
            continue;

        if (crossover_ui::isCurrentStateProperty(property, "crossover")
            && ! crossover_ui::isCurrentStatePropertyValue(state, property, "crossover"))
            return false;
    }

    // Routing processor state is validated and restored per child by Runtime.
    // A stale nested instance must not invalidate the owner's CHAIN state.

    const auto hasWidth = state.hasProperty(AvaAudioProcessor::editorWidthStateKey);
    const auto hasHeight = state.hasProperty(AvaAudioProcessor::editorHeightStateKey);

    if (hasWidth != hasHeight
        || (hasWidth
            && ! crossover_ui::hasExactIntegerValue(state.getProperty(AvaAudioProcessor::editorWidthStateKey),
                                                      1,
                                                      maximumStoredEditorDimension))
        || (hasHeight
            && ! crossover_ui::hasExactIntegerValue(state.getProperty(AvaAudioProcessor::editorHeightStateKey),
                                                      1,
                                                      maximumStoredEditorDimension))
        || ! ava::routing::isCurrentState(state)
        || ! ava::routing::hasCurrentProcessorStates(state)
        || (state.hasProperty(AvaAudioProcessor::oscEnabledStateKey)
            && ! crossover_ui::hasExactBooleanValue(state.getProperty(AvaAudioProcessor::oscEnabledStateKey)))
        || (state.hasProperty(AvaAudioProcessor::oscInputPortStateKey)
            && ! crossover_ui::hasExactIntegerValue(state.getProperty(AvaAudioProcessor::oscInputPortStateKey), 1, 65535))
        || (state.hasProperty(AvaAudioProcessor::oscOutputPortStateKey)
            && ! crossover_ui::hasExactIntegerValue(state.getProperty(AvaAudioProcessor::oscOutputPortStateKey), 1, 65535))
        || (state.hasProperty(AvaAudioProcessor::oscOutputHostStateKey)
            && (state.getProperty(AvaAudioProcessor::oscOutputHostStateKey).toString().trim().isEmpty()
                || state.getProperty(AvaAudioProcessor::oscOutputHostStateKey).toString().length() > 253)))
        return false;

    for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
    {
        const auto key = AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(rangeIndex);

        if (state.hasProperty(key)
            && ! shell_filter_order_state::isCurrentEncoding(state.getProperty(key).toString(),
                                                               EqlModuleProcessor::maxFilterCount))
            return false;
    }

    const auto hasABSlot = state.hasProperty(AvaAudioProcessor::abCompareActiveSlotStateKey);
    const auto hasSnapshotA = state.hasProperty(AvaAudioProcessor::abCompareSnapshotAStateKey);
    const auto hasSnapshotB = state.hasProperty(AvaAudioProcessor::abCompareSnapshotBStateKey);

    // Hosts may reopen sessions saved before A/B compare was introduced.
    // An absent A/B slot does not invalidate the main plugin state.
    if (!includeABCompareState && (hasABSlot || hasSnapshotA || hasSnapshotB))
        return false;

    if (hasABSlot)
    {
        const auto activeSlot = state.getProperty(AvaAudioProcessor::abCompareActiveSlotStateKey).toString();

        if (activeSlot != "0" && activeSlot != "1")
            return false;
    }

    // Validate A/B snapshots separately when restoring them. A stale snapshot
    // is secondary data and must not erase the current LISTEN/CHAIN settings.

    const auto moduleId = state.getProperty(AvaAudioProcessor::activeModuleStateKey).toString();
    const std::array<std::pair<const char*, const char*>, 3> moduleProperties {{
        { AvaAudioProcessor::eqlModuleId, AvaAudioProcessor::eqlModuleStateKey },
        { AvaAudioProcessor::blockChainModuleId, AvaAudioProcessor::blockChainModuleStateKey },
        { AvaAudioProcessor::dynModuleId, AvaAudioProcessor::dynModuleStateKey }
    }};

    auto foundActiveModuleState = moduleId.isEmpty();
    for (const auto& [currentModuleId, stateKey] : moduleProperties)
    {
        if (moduleId == currentModuleId) foundActiveModuleState = state.hasProperty(stateKey);
        if (state.hasProperty(stateKey)
            && (moduleId.isEmpty() || state.getProperty(stateKey).toString().isEmpty())) return false;
    }
    if (!foundActiveModuleState) return false;
    if (!state.hasProperty(AvaAudioProcessor::eqlModuleStateKey))
        for (size_t range = 0; range < ava::crossover::BufferRouter::numRanges; ++range)
            if (state.hasProperty(AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(range))) return false;

    return hasCurrentSerializedModuleState(state);
}


}

namespace shell_state_validation
{
bool hasCurrentState(const juce::ValueTree& state,
                     AvaAudioProcessor& processor,
                     const bool includeABCompareState)
{
    const auto completed = ava::modules::state::completeMissingParameterDefaults(
        state, processor.getValueTreeState());
    if (!ava::modules::state::hasValidParameterState(completed, processor.getValueTreeState())) return false;
    return hasCurrentActiveModuleId(completed)
        && hasCurrentShellInvariants(completed)
        && hasCurrentShellMetadata(completed, processor, includeABCompareState);
}
}
