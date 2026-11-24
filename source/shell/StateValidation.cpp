#include "Processor.h"
#include "EditorStateSchema.h"
#include "StateValidation.h"
#include "ModuleStackState.h"
#include "FilterOrderState.h"
#include "Style.h"
#include "../modules/eql/Processor.h"
#include "../crossover/UiStateSchema.h"
#include "../routing/State.h"

#include <array>

namespace shell_state_validation
{
void retainValidProperties(juce::ValueTree& incoming,
                           const juce::ValueTree& current,
                           const bool includeABCompareState)
{
    // A bad field must not prevent unrelated parameters, modules or UI state
    // from loading. Keep its last live value instead of copying invalid data.
    const auto keepCurrent = [&](const juce::Identifier& key)
    {
        juce::Logger::writeToLog("AVA: ignoring invalid state property: " + key.toString());
        if (current.hasProperty(key))
            incoming.setProperty(key, current.getProperty(key), nullptr);
        else
            incoming.removeProperty(key, nullptr);
    };
    const auto check = [&](const juce::Identifier& key, const bool valid)
    {
        if (incoming.hasProperty(key) && !valid) keepCurrent(key);
    };

    for (const auto& module : module_bypass::descriptions)
        check(module.bypassStateKey,
              crossover_ui::hasExactBooleanValue(incoming.getProperty(module.bypassStateKey)));

    for (size_t band = 0; band < ava::crossover::BufferRouter::numRanges; ++band)
    {
        const auto orderKey = module_order::bandStateKey(band);
        module_order::Order order;
        check(orderKey, module_order::parse(incoming.getProperty(orderKey).toString(), order));
        for (const auto id : {module_order::eql, module_order::dyn})
        {
            const auto hiddenKey = module_order::bandHiddenKey(band, id);
            check(hiddenKey, crossover_ui::hasExactBooleanValue(incoming.getProperty(hiddenKey)));
        }
        const auto filterOrderKey = AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(band);
        check(filterOrderKey,
              shell_filter_order_state::isCurrentEncoding(incoming.getProperty(filterOrderKey).toString(),
                                                          EqlModuleProcessor::maxFilterCount));
    }

    // Iterate backwards: keepCurrent may remove a property.
    for (int index = incoming.getNumProperties() - 1; index >= 0; --index)
    {
        const auto property = incoming.getPropertyName(index);
        const auto value = incoming.getProperty(property);
        if (property == juce::Identifier(AvaAudioProcessor::hostSlotDefaultsKey)
            && !crossover_ui::hasExactBooleanValue(value))
        {
            keepCurrent(property);
            continue;
        }
        if (const auto valid = shell_editor_ui::validate(property, value); valid.has_value())
        {
            if (!*valid) keepCurrent(property);
            continue;
        }
        if (crossover_ui::isCurrentStateProperty(property, "crossover")
            && !crossover_ui::isCurrentStatePropertyValue(incoming, property, "crossover"))
            keepCurrent(property);
    }

    const auto width = juce::Identifier(AvaAudioProcessor::editorWidthStateKey);
    const auto height = juce::Identifier(AvaAudioProcessor::editorHeightStateKey);
    if (incoming.hasProperty(width) != incoming.hasProperty(height)
        || (incoming.hasProperty(width)
            && (!crossover_ui::hasExactIntegerValue(incoming.getProperty(width), 1, maximumStoredEditorDimension)
                || !crossover_ui::hasExactIntegerValue(incoming.getProperty(height), 1, maximumStoredEditorDimension))))
    {
        keepCurrent(width);
        keepCurrent(height);
    }

    check(AvaAudioProcessor::oscEnabledStateKey,
          crossover_ui::hasExactBooleanValue(incoming.getProperty(AvaAudioProcessor::oscEnabledStateKey)));
    check(AvaAudioProcessor::oscInputPortStateKey,
          crossover_ui::hasExactIntegerValue(incoming.getProperty(AvaAudioProcessor::oscInputPortStateKey), 1, 65535));
    check(AvaAudioProcessor::oscOutputPortStateKey,
          crossover_ui::hasExactIntegerValue(incoming.getProperty(AvaAudioProcessor::oscOutputPortStateKey), 1, 65535));
    const auto outputHost = incoming.getProperty(AvaAudioProcessor::oscOutputHostStateKey).toString();
    check(AvaAudioProcessor::oscOutputHostStateKey,
          outputHost.isNotEmpty() && outputHost == outputHost.trim() && outputHost.length() <= 253);

    const auto moduleId = juce::Identifier(AvaAudioProcessor::activeModuleStateKey);
    if (incoming.hasProperty(moduleId))
    {
        const auto active = incoming.getProperty(moduleId).toString();
        bool known = false;
        for (const auto module : AvaAudioProcessor::availableModules)
            known |= active == AvaAudioProcessor::stateIdForModule(module);
        if (!known) keepCurrent(moduleId);
    }
    else if (current.hasProperty(moduleId))
        incoming.setProperty(moduleId, current.getProperty(moduleId), nullptr);

    const auto stackKey = juce::Identifier(AvaAudioProcessor::moduleStackStateKey);
    if (!incoming.hasProperty(stackKey) && current.hasProperty(stackKey))
        incoming.setProperty(stackKey, current.getProperty(stackKey), nullptr);
    check(stackKey, ava::module_stack::state::decode(incoming.getProperty(stackKey).toString(),
                                                    ava::crossover::BufferRouter::numRanges).has_value());

    for (const auto key : {AvaAudioProcessor::blockChainModuleStateKey,
                           AvaAudioProcessor::dynModuleStateKey,
                           AvaAudioProcessor::eqlModuleStateKey})
    {
        if (!incoming.hasProperty(key))
        {
            if (current.hasProperty(key))
                incoming.setProperty(key, current.getProperty(key), nullptr);
            continue;
        }
        juce::MemoryBlock decoded;
        if (!decoded.fromBase64Encoding(incoming.getProperty(key).toString()) || decoded.isEmpty()
            || juce::AudioProcessor::getXmlFromBinary(decoded.getData(), int(decoded.getSize())) == nullptr)
            keepCurrent(key);
    }

    // Routing graph properties form one internally linked structure. If that
    // graph is malformed, leave it alone while still restoring other settings.
    if (!ava::routing::isCurrentState(incoming))
        for (const auto key : {ava::routing::instancesStateKey,
                               ava::routing::nextInstanceIdStateKey,
                               ava::routing::rootInstanceIdStateKey,
                               ava::routing::rootBypassStateKey,
                               ava::routing::namesStateKey})
            keepCurrent(key);

    const auto processorStates = juce::Identifier(ava::routing::processorStatesKey);
    if (incoming.hasProperty(processorStates))
    {
        const auto xml = juce::XmlDocument::parse(incoming.getProperty(processorStates).toString());
        if (xml == nullptr || !xml->hasTagName("routing_processors")) keepCurrent(processorStates);
    }
    else if (current.hasProperty(processorStates))
        incoming.setProperty(processorStates, current.getProperty(processorStates), nullptr);

    // Host slot bindings are independent. Bad or duplicate targets are ignored
    // locally; they must never invalidate all other host parameters.
    juce::StringArray targets;
    for (int slot = 0; slot < AvaAudioProcessor::assignableHostSlotCount; ++slot)
    {
        const auto key = AvaAudioProcessor::getHostSlotTargetStateKey(slot);
        if (!incoming.hasProperty(key)) continue;
        const auto target = incoming.getProperty(key).toString();
        int instance = 0;
        juce::String local;
        if (target.isEmpty() || target.startsWith(AvaAudioProcessor::paramHostSlotPrefix)
            || targets.contains(target)
            || (ava::routing::parseParameterTarget(target, instance, local)
                && local.startsWith(AvaAudioProcessor::paramHostSlotPrefix)))
        {
            // An invalid assignment is safer left unassigned than duplicated.
            juce::Logger::writeToLog("AVA: ignoring invalid host slot assignment " + juce::String(slot));
            incoming.removeProperty(key, nullptr);
        }
        else targets.add(target);
    }

    if (!includeABCompareState)
    {
        incoming.removeProperty(AvaAudioProcessor::abCompareActiveSlotStateKey, nullptr);
        incoming.removeProperty(AvaAudioProcessor::abCompareSnapshotAStateKey, nullptr);
        incoming.removeProperty(AvaAudioProcessor::abCompareSnapshotBStateKey, nullptr);
    }
    else
    {
        const auto slotKey = juce::Identifier(AvaAudioProcessor::abCompareActiveSlotStateKey);
        check(slotKey, incoming.getProperty(slotKey).toString() == "0"
                       || incoming.getProperty(slotKey).toString() == "1");
    }
}
}
