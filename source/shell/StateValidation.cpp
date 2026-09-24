#include "Processor.h"
#include "StateValidation.h"
#include "FilterOrderState.h"
#include "Style.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/ProcessorBank.h"
#include "../modules/fft/ProcessorBank.h"
#include "../modules/trs/Processor.h"
#include "../modules/shared/StateUtilities.h"
#include "../crossover/UiState.h"
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

    for (const auto module : { AvaAudioProcessor::ActiveModule::eql,
                               AvaAudioProcessor::ActiveModule::fft,
                               AvaAudioProcessor::ActiveModule::tls,
                               AvaAudioProcessor::ActiveModule::dyn,
                               AvaAudioProcessor::ActiveModule::trs })
        if (moduleId == AvaAudioProcessor::stateIdForModule(module))
            return true;

    return false;
}
bool isCurrentShellProperty(const juce::Identifier& property)
{
    if (crossover_ui::isCurrentStateProperty(property, "crossover"))
        return true;

    for (const auto* key : { AvaAudioProcessor::activeModuleStateKey,
                             AvaAudioProcessor::eqlModuleStateKey,
                             AvaAudioProcessor::fftModuleStateKey,
                             AvaAudioProcessor::tlsModuleStateKey,
                             AvaAudioProcessor::dynModuleStateKey,
                             AvaAudioProcessor::trsModuleStateKey,
                             AvaAudioProcessor::abCompareSnapshotAStateKey,
                             AvaAudioProcessor::abCompareSnapshotBStateKey,
                             AvaAudioProcessor::abCompareActiveSlotStateKey,
                             AvaAudioProcessor::editorWidthStateKey,
                             AvaAudioProcessor::editorHeightStateKey,
                             AvaAudioProcessor::editorHostParametersExpandedStateKey,
                             AvaAudioProcessor::editorRoutingExpandedStateKey,
                             ava::routing::instancesStateKey,
                             ava::routing::nextInstanceIdStateKey,
                             ava::routing::rootInstanceIdStateKey,
                             ava::routing::namesStateKey,
                             ava::routing::processorStatesKey,
                             AvaAudioProcessor::oscEnabledStateKey,
                             AvaAudioProcessor::oscInputPortStateKey,
                             AvaAudioProcessor::oscOutputHostStateKey,
                             AvaAudioProcessor::oscOutputPortStateKey,
                             AvaAudioProcessor::oscInstanceNameStateKey })
    {
        if (property == juce::Identifier(key))
            return true;
    }

    for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
        if (property == juce::Identifier(AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(rangeIndex)))
            return true;

    for (int slotIndex = 0; slotIndex < AvaAudioProcessor::hostAutomationSlotCount; ++slotIndex)
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

bool isCurrentABCompareSnapshot(const juce::String& encodedState,
                                AvaAudioProcessor& processor);

bool hasCurrentHostSlotTargets(const juce::ValueTree& state,
                               juce::AudioProcessorValueTreeState& shellParameters,
                               juce::AudioProcessorValueTreeState* moduleParameters)
{
    juce::StringArray assignedTargets;

    for (int slotIndex = 0; slotIndex < AvaAudioProcessor::hostAutomationSlotCount; ++slotIndex)
    {
        const auto targetKey = AvaAudioProcessor::getHostSlotTargetStateKey(slotIndex);

        if (! state.hasProperty(targetKey))
            continue;

        const auto targetId = state.getProperty(targetKey).toString();

        if (targetId.isEmpty() || targetId.startsWith(AvaAudioProcessor::paramHostSlotPrefix))
            return false;

        if (assignedTargets.contains(targetId))
            return false;

        assignedTargets.add(targetId);

        if (shellParameters.getParameter(targetId) != nullptr)
            continue;

        if (moduleParameters != nullptr && moduleParameters->getParameter(targetId) != nullptr)
            continue;

        return false;
    }

    return true;
}

bool hasCurrentSerializedModuleState(const juce::ValueTree& state,
                                     AvaAudioProcessor& processor)
{
    const auto moduleId = state.getProperty(AvaAudioProcessor::activeModuleStateKey).toString();
    auto& shellParameters = processor.getValueTreeState();

    if (moduleId.isEmpty())
        return hasCurrentHostSlotTargets(state, shellParameters, nullptr);

    const auto restoreBinary = [] (auto& module, const juce::String& encodedState)
    {
        juce::MemoryBlock stateData;
        return stateData.fromBase64Encoding(encodedState)
            && ! stateData.isEmpty()
            && module.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
    };

    if (moduleId == AvaAudioProcessor::eqlModuleId)
    {
        juce::MemoryBlock stateData;
        const auto encodedState = state.getProperty(AvaAudioProcessor::eqlModuleStateKey).toString();

        if (! stateData.fromBase64Encoding(encodedState) || stateData.isEmpty())
            return false;

        EqlProcessorBank candidate;

        if (! candidate.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize())))
            return false;

        const auto requiredRangeCount = getRequiredCrossoverRangeCount(state);

        if (! requiredRangeCount.has_value() || candidate.getCreatedRangeCount() < *requiredRangeCount)
            return false;

        for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
        {
            const auto key = AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(rangeIndex);

            if (! state.hasProperty(key))
                continue;

            const auto activeFilterCount = candidate.getRangeActiveFilterCount(rangeIndex);

            if (activeFilterCount <= 0
                || ! shell_filter_order_state::decode(state.getProperty(key).toString(),
                                                       activeFilterCount,
                                                       EqlModuleProcessor::maxFilterCount).has_value())
                return false;
        }

        auto* moduleProcessor = candidate.getSelectedProcessor();
        return moduleProcessor != nullptr
            && hasCurrentHostSlotTargets(state, shellParameters, &moduleProcessor->getValueTreeState());
    }

    if (moduleId == AvaAudioProcessor::fftModuleId)
    {
        FftProcessorBank candidate(processor);

        if (! candidate.setStateFromXmlString(state.getProperty(AvaAudioProcessor::fftModuleStateKey).toString()))
            return false;

        const auto requiredRangeCount = getRequiredCrossoverRangeCount(state);

        if (! requiredRangeCount.has_value() || candidate.getCreatedRangeCount() < *requiredRangeCount)
            return false;

        auto* moduleProcessor = candidate.getSelectedProcessor();
        return moduleProcessor != nullptr
            && hasCurrentHostSlotTargets(state, shellParameters, &moduleProcessor->getValueTreeState());
    }

    if (moduleId == AvaAudioProcessor::tlsModuleId)
    {
        TlsModuleProcessor candidate(processor);
        return restoreBinary(candidate, state.getProperty(AvaAudioProcessor::tlsModuleStateKey).toString())
            && hasCurrentHostSlotTargets(state, shellParameters, &candidate.getValueTreeState());
    }

    if (moduleId == AvaAudioProcessor::dynModuleId)
    {
        DynModuleProcessor candidate(processor);
        return restoreBinary(candidate, state.getProperty(AvaAudioProcessor::dynModuleStateKey).toString())
            && hasCurrentHostSlotTargets(state, shellParameters, &candidate.getValueTreeState());
    }

    if (moduleId == AvaAudioProcessor::trsModuleId)
    {
        TrsModuleProcessor candidate(processor);
        return candidate.setStateFromXmlString(state.getProperty(AvaAudioProcessor::trsModuleStateKey).toString())
            && hasCurrentHostSlotTargets(state, shellParameters, &candidate.getValueTreeState());
    }

    return false;
}

bool hasCurrentShellMetadata(const juce::ValueTree& state,
                             AvaAudioProcessor& processor,
                             const bool includeABCompareState)
{
    for (int propertyIndex = 0; propertyIndex < state.getNumProperties(); ++propertyIndex)
    {
        const auto property = state.getPropertyName(propertyIndex);

        if (! isCurrentShellProperty(property))
            return false;

        if (crossover_ui::isCurrentStateProperty(property, "crossover")
            && ! crossover_ui::isCurrentStatePropertyValue(state, property, "crossover"))
            return false;
    }

    const auto hasWidth = state.hasProperty(AvaAudioProcessor::editorWidthStateKey);
    const auto hasHeight = state.hasProperty(AvaAudioProcessor::editorHeightStateKey);

    if (hasWidth != hasHeight
        || (hasWidth
            && ! crossover_ui::hasExactIntegerValue(state.getProperty(AvaAudioProcessor::editorWidthStateKey),
                                                      1,
                                                      maximumStoredEditorWidth))
        || (hasHeight
            && ! crossover_ui::hasExactIntegerValue(state.getProperty(AvaAudioProcessor::editorHeightStateKey),
                                                      minimumEditorHeight,
                                                      maximumEditorHeight))
        || (state.hasProperty(AvaAudioProcessor::editorHostParametersExpandedStateKey)
            && ! crossover_ui::hasExactBooleanValue(
                state.getProperty(AvaAudioProcessor::editorHostParametersExpandedStateKey)))
        || (state.hasProperty(AvaAudioProcessor::editorRoutingExpandedStateKey)
            && ! crossover_ui::hasExactBooleanValue(
                state.getProperty(AvaAudioProcessor::editorRoutingExpandedStateKey)))
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

    if (includeABCompareState != hasABSlot)
        return false;

    if (! includeABCompareState && (hasSnapshotA || hasSnapshotB))
        return false;

    if (hasABSlot)
    {
        const auto activeSlot = state.getProperty(AvaAudioProcessor::abCompareActiveSlotStateKey).toString();

        if (activeSlot != "0" && activeSlot != "1")
            return false;
    }

    if ((hasSnapshotA
         && ! isCurrentABCompareSnapshot(state.getProperty(AvaAudioProcessor::abCompareSnapshotAStateKey).toString(),
                                         processor))
        || (hasSnapshotB
            && ! isCurrentABCompareSnapshot(state.getProperty(AvaAudioProcessor::abCompareSnapshotBStateKey).toString(),
                                            processor)))
        return false;

    const auto moduleId = state.getProperty(AvaAudioProcessor::activeModuleStateKey).toString();
    const std::array<std::pair<const char*, const char*>, 5> moduleProperties {{
        { AvaAudioProcessor::eqlModuleId, AvaAudioProcessor::eqlModuleStateKey },
        { AvaAudioProcessor::fftModuleId, AvaAudioProcessor::fftModuleStateKey },
        { AvaAudioProcessor::tlsModuleId, AvaAudioProcessor::tlsModuleStateKey },
        { AvaAudioProcessor::dynModuleId, AvaAudioProcessor::dynModuleStateKey },
        { AvaAudioProcessor::trsModuleId, AvaAudioProcessor::trsModuleStateKey }
    }};

    auto foundActiveModuleState = moduleId.isEmpty();

    for (const auto& [currentModuleId, stateKey] : moduleProperties)
    {
        const auto hasState = state.hasProperty(stateKey);

        if (moduleId == currentModuleId)
        {
            if (! hasState || state.getProperty(stateKey).toString().isEmpty())
                return false;

            foundActiveModuleState = true;
        }
        else if (hasState)
        {
            return false;
        }
    }

    if (! foundActiveModuleState)
        return false;

    if (moduleId != AvaAudioProcessor::eqlModuleId)
    {
        for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
            if (state.hasProperty(AvaAudioProcessor::getEditorFilterDisplayOrderStateKey(rangeIndex)))
                return false;
    }

    return hasCurrentSerializedModuleState(state, processor);
}

bool isCurrentABCompareSnapshot(const juce::String& encodedState,
                                AvaAudioProcessor& processor)
{
    auto& parameters = processor.getValueTreeState();

    if (encodedState.isEmpty())
        return false;

    juce::MemoryBlock snapshot;

    if (! snapshot.fromBase64Encoding(encodedState) || snapshot.isEmpty())
        return false;

    auto xml = juce::AudioProcessor::getXmlFromBinary(snapshot.getData(), static_cast<int>(snapshot.getSize()));

    if (xml == nullptr || ! xml->hasTagName(parameters.state.getType()))
        return false;

    const auto state = juce::ValueTree::fromXml(*xml);

    return hasCurrentActiveModuleId(state)
        && ava::modules::state::hasExactParameterState(state, parameters)
        && hasCurrentShellInvariants(state)
        && hasCurrentShellMetadata(state, processor, false);
}

}

namespace shell_state_validation
{
bool hasCurrentState(const juce::ValueTree& state,
                     AvaAudioProcessor& processor,
                     const bool includeABCompareState)
{
    return hasCurrentActiveModuleId(state)
        && ava::modules::state::hasExactParameterState(state, processor.getValueTreeState())
        && hasCurrentShellInvariants(state)
        && hasCurrentShellMetadata(state, processor, includeABCompareState);
}
}
