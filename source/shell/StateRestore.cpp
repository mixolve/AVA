#include "Processor.h"
#include "Editor.h"
#include "ABCompareSerialization.h"
#include "StateValidation.h"

#include "../crossover/UiStateSchema.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../modules/tls/Processor.h"
#include "../routing/Runtime.h"

#include <optional>

namespace
{
size_t getRestoredSelectedCrossoverRange(const juce::ValueTree& state)
{
    const auto key = crossover_ui::makeStatePropertyId("crossover", "visible_band_index");
    return static_cast<size_t>(static_cast<int>(state.getProperty(key, 0)));
}

struct RestoredModuleStateProperties
{
    explicit RestoredModuleStateProperties(const juce::ValueTree& state)
        : eqlBase64(state.getProperty(AvaAudioProcessor::eqlModuleStateKey).toString()),
          tlsBase64(state.getProperty(AvaAudioProcessor::tlsModuleStateKey).toString()),
          dynBase64(state.getProperty(AvaAudioProcessor::dynModuleStateKey).toString())
    {
    }

    juce::String eqlBase64;
    juce::String tlsBase64;
    juce::String dynBase64;
};

template <typename Processor>
bool restoreBinaryModuleState(Processor* processor, const juce::String& stateBase64)
{
    if (processor == nullptr)
        return false;

    juce::MemoryBlock stateData;

    if (! stateData.fromBase64Encoding(stateBase64))
        return false;

    return processor->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
}

bool restoreActiveModuleState(AvaAudioProcessor& processor,
                              const AvaAudioProcessor::ActiveModule module,
                              const RestoredModuleStateProperties& moduleStates)
{
    switch (module)
    {
        case AvaAudioProcessor::ActiveModule::eql:
            return restoreBinaryModuleState(processor.getEqlProcessorBank(), moduleStates.eqlBase64);

        case AvaAudioProcessor::ActiveModule::tls:
            return restoreBinaryModuleState(processor.getTlsModuleProcessor(), moduleStates.tlsBase64);

        case AvaAudioProcessor::ActiveModule::dyn:
            return restoreBinaryModuleState(processor.getDynModuleProcessor(), moduleStates.dynBase64);

        case AvaAudioProcessor::ActiveModule::none:
            return true;
    }

    return false;
}
}

void AvaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    restoreStateInformation(data, sizeInBytes, true);
}

bool AvaAudioProcessor::applyHistoryStateInformation(const void* data, const int sizeInBytes)
{
    return restoreStateInformation(data, sizeInBytes, false);
}

bool AvaAudioProcessor::restoreStateInformation(const void* data,
                                                const int sizeInBytes,
                                                const bool includeABCompareState,
                                                const bool allowUnsuspendedRestore)
{
    const juce::MessageManagerLock uiLock;
    if (! uiLock.lockWasGained())
        return false;

    const auto stateXml = getXmlFromBinary(data, sizeInBytes);
    if (stateXml == nullptr || ! stateXml->hasTagName(parameters.state.getType()))
        return false;

    auto restoredState = juce::ValueTree::fromXml(*stateXml);
    if (! shell_state_validation::hasCurrentState(restoredState, *this, includeABCompareState))
        return false;

    const auto restoredActiveModule = moduleFromStateId(restoredState.getProperty(activeModuleStateKey).toString());
    const auto restoredModuleStates = RestoredModuleStateProperties(restoredState);
    const auto restoredABCompareState = includeABCompareState
        ? std::optional<shell_state_serialization::RestoredABCompareState> { shell_state_serialization::RestoredABCompareState(restoredState) }
        : std::nullopt;
    const auto restoredSelectedRange = getRestoredSelectedCrossoverRange(restoredState);
    shell_state_serialization::removeABCompareStateProperties(restoredState);

    const juce::ScopedLock lock(processingLock);
    const auto reuseModule = restoredActiveModule == getActiveModule();
    const auto suspendProcessing = ! allowUnsuspendedRestore || ! reuseModule;
    std::optional<ScopedProcessingSuspend> suspendGuard;
    if (suspendProcessing)
        suspendGuard.emplace(*this);

    const auto previousSuppression = suppressHostStateNotifications.exchange(true, std::memory_order_acq_rel);
    const auto wasPrepared = suspendProcessing
        ? processingPrepared.exchange(false, std::memory_order_acq_rel)
        : processingPrepared.load(std::memory_order_acquire);
    const juce::ScopeGuard restoreProcessingFlags { [this, previousSuppression, wasPrepared, suspendProcessing]
    {
        if (suspendProcessing && wasPrepared && currentSampleRate > 0.0)
            processingPrepared.store(true, std::memory_order_release);
        suppressHostStateNotifications.store(previousSuppression, std::memory_order_release);
    } };

    clearActiveModuleStateListeners();
    if (reuseModule)
    {
        if (auto* editor = getOscActionEditor())
            editor->prepareForProcessorStateRestore();
        if (! restoreActiveModuleState(*this, restoredActiveModule, restoredModuleStates))
        {
            registerActiveModuleStateListeners();
            return false;
        }
    }
    else
    {
        resetModuleProcessors();
        setActiveModule(ActiveModule::none);
    }

    parameters.replaceState(restoredState);
    selectedCrossoverRange.store(restoredSelectedRange, std::memory_order_relaxed);

    if (! reuseModule)
    {
        if (restoredActiveModule != ActiveModule::none
            && (! createModuleInstance(restoredActiveModule)
                || ! restoreActiveModuleState(*this, restoredActiveModule, restoredModuleStates)))
        {
            resetModuleProcessors();
            setActiveModule(ActiveModule::none);
            return false;
        }
        setActiveModule(restoredActiveModule);
    }

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->setSelectedRange(restoredSelectedRange);
    if (restoredActiveModule != ActiveModule::none)
        registerActiveModuleStateListeners();

    if (routingRuntime != nullptr)
    {
        routingRuntime->synchronize(parameters.state);
        routingRuntime->restoreProcessorStates(parameters.state);
        routingRuntime->refreshDelayCapacity();
    }
    refreshHostSlotTargets();
    if (restoredABCompareState.has_value())
        shell_state_serialization::restoreABCompareState(*this, *restoredABCompareState);

    updateShellLatency();
    setLastEditorSize(static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0)),
                      static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0)));
    refreshOscConfiguration();
    return true;
}

bool AvaAudioProcessor::applyStateInformationForABCompare(const void* data, const int sizeInBytes)
{
    const auto previousLatencyLocked = abCompareLatencyLocked.load(std::memory_order_acquire);
    const auto previousLatencyFloor = abCompareLatencyFloorSamples.load(std::memory_order_acquire);
    const auto reportedLatencySamples = getLatencySamples();

    abCompareLatencyFloorSamples.store(juce::jmax(previousLatencyFloor, reportedLatencySamples),
                                       std::memory_order_release);
    abCompareLatencyLocked.store(true, std::memory_order_release);

    const auto restored = restoreStateInformation(data, sizeInBytes, false, true);

    if (! restored)
    {
        abCompareLatencyFloorSamples.store(previousLatencyFloor, std::memory_order_release);
        abCompareLatencyLocked.store(previousLatencyLocked, std::memory_order_release);
        updateShellLatency();
    }

    return restored;
}
