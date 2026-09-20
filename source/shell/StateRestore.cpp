#include "Processor.h"
#include "ABCompareSerialization.h"
#include "StateValidation.h"

#include "../crossover/UiState.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/ProcessorBank.h"
#include "../modules/fft/ProcessorBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/trs/Processor.h"

#include <optional>

namespace
{
size_t getRestoredSelectedCrossoverRange(const juce::ValueTree& state)
{
    const auto key = crossover_ui::makeStatePropertyId("crossover", "visible_range_index");
    return static_cast<size_t>(static_cast<int>(state.getProperty(key, 0)));
}

struct RestoredModuleStateProperties
{
    explicit RestoredModuleStateProperties(const juce::ValueTree& state)
        : eqlBase64(state.getProperty(AvaAudioProcessor::eqlModuleStateKey).toString()),
          fftXml(state.getProperty(AvaAudioProcessor::fftModuleStateKey).toString()),
          tlsBase64(state.getProperty(AvaAudioProcessor::tlsModuleStateKey).toString()),
          dynBase64(state.getProperty(AvaAudioProcessor::dynModuleStateKey).toString()),
          trsXml(state.getProperty(AvaAudioProcessor::trsModuleStateKey).toString())
    {
    }

    juce::String eqlBase64;
    juce::String fftXml;
    juce::String tlsBase64;
    juce::String dynBase64;
    juce::String trsXml;
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

template <typename Processor>
bool restoreXmlModuleState(Processor* processor, const juce::String& stateXml)
{
    if (processor == nullptr || stateXml.isEmpty())
        return false;

    return processor->setStateFromXmlString(stateXml);
}

bool restoreEqlModuleState(AvaAudioProcessor& processor, const juce::String& stateBase64)
{
    auto* eqlProcessorBank = processor.getEqlProcessorBank();

    if (eqlProcessorBank == nullptr)
        return false;

    juce::MemoryBlock stateData;

    if (! stateData.fromBase64Encoding(stateBase64))
        return false;

    return eqlProcessorBank->setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
}

bool restoreActiveModuleState(AvaAudioProcessor& processor,
                              const AvaAudioProcessor::ActiveModule module,
                              const RestoredModuleStateProperties& moduleStates)
{
    switch (module)
    {
        case AvaAudioProcessor::ActiveModule::eql:
            return restoreEqlModuleState(processor, moduleStates.eqlBase64);

        case AvaAudioProcessor::ActiveModule::fft:
            return restoreXmlModuleState(processor.getFftProcessorBank(), moduleStates.fftXml);

        case AvaAudioProcessor::ActiveModule::tls:
            return restoreBinaryModuleState(processor.getTlsModuleProcessor(), moduleStates.tlsBase64);

        case AvaAudioProcessor::ActiveModule::dyn:
            return restoreBinaryModuleState(processor.getDynModuleProcessor(), moduleStates.dynBase64);

        case AvaAudioProcessor::ActiveModule::trs:
            return restoreXmlModuleState(processor.getTrsModuleProcessor(), moduleStates.trsXml);

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
                                                const bool includeABCompareState)
{
    if (restoreStateInformationPreservingLoadedModule(data,
                                                      sizeInBytes,
                                                      includeABCompareState,
                                                      true))
        return true;

    auto stateXml = getXmlFromBinary(data, sizeInBytes);

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

    if (restoredActiveModule == ActiveModule::none)
        removeModuleStateProperties(restoredState);
    else
        restoredState.setProperty(activeModuleStateKey, stateIdForModule(restoredActiveModule), nullptr);

    const auto previousNotificationSuppression = suppressHostStateNotifications.exchange(true, std::memory_order_acq_rel);
    const ScopedProcessingSuspend suspendGuard(*this);
    const auto wasProcessingPrepared = processingPrepared.exchange(false, std::memory_order_acq_rel);
    const juce::ScopedLock lock(processingLock);

    parameters.replaceState(restoredState);
    selectedCrossoverRange.store(restoredSelectedRange, std::memory_order_relaxed);
    clearActiveModuleStateListeners();
    resetModuleProcessors();
    setActiveModule(ActiveModule::none);

    auto restoredModuleState = restoredActiveModule == ActiveModule::none;

    if (restoredActiveModule != ActiveModule::none && createModuleInstance(restoredActiveModule))
        restoredModuleState = restoreActiveModuleState(*this, restoredActiveModule, restoredModuleStates);

    if (! restoredModuleState)
        resetModuleProcessors();

    setActiveModule(restoredModuleState ? restoredActiveModule : ActiveModule::none);

    if (restoredModuleState && restoredActiveModule != ActiveModule::none)
        registerActiveModuleStateListeners();

    refreshHostSlotTargets();

    if (restoredABCompareState.has_value())
        shell_state_serialization::restoreABCompareState(*this, *restoredABCompareState);

    updateShellLatency();
    setLastEditorSize(static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0)),
                      static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0)));

    if (wasProcessingPrepared && currentSampleRate > 0.0)
        processingPrepared.store(true, std::memory_order_release);

    suppressHostStateNotifications.store(previousNotificationSuppression, std::memory_order_release);
    return restoredModuleState;
}

bool AvaAudioProcessor::restoreStateInformationPreservingLoadedModule(const void* data,
                                                                      const int sizeInBytes,
                                                                      const bool includeABCompareState,
                                                                      const bool suspendProcessingForRestore)
{
    auto stateXml = getXmlFromBinary(data, sizeInBytes);

    if (stateXml == nullptr || ! stateXml->hasTagName(parameters.state.getType()))
        return false;

    auto restoredState = juce::ValueTree::fromXml(*stateXml);

    if (! shell_state_validation::hasCurrentState(restoredState, *this, includeABCompareState))
        return false;

    const auto restoredActiveModule = moduleFromStateId(restoredState.getProperty(activeModuleStateKey).toString());
    const auto restoredSelectedRange = getRestoredSelectedCrossoverRange(restoredState);

    if (restoredActiveModule != getActiveModule())
        return false;

    const auto restoredModuleStates = RestoredModuleStateProperties(restoredState);
    const auto restoredABCompareState = includeABCompareState
        ? std::optional<shell_state_serialization::RestoredABCompareState> { shell_state_serialization::RestoredABCompareState(restoredState) }
        : std::nullopt;
    shell_state_serialization::removeABCompareStateProperties(restoredState);
    const auto previousNotificationSuppression = suppressHostStateNotifications.exchange(true, std::memory_order_acq_rel);
    std::optional<ScopedProcessingSuspend> suspendGuard;

    if (suspendProcessingForRestore)
        suspendGuard.emplace(*this);

    const auto wasProcessingPrepared = suspendProcessingForRestore
        ? processingPrepared.exchange(false, std::memory_order_acq_rel)
        : processingPrepared.load(std::memory_order_acquire);
    const juce::ScopedLock lock(processingLock);

    if (restoredActiveModule == ActiveModule::none)
        removeModuleStateProperties(restoredState);
    else
        restoredState.setProperty(activeModuleStateKey, stateIdForModule(restoredActiveModule), nullptr);

    clearActiveModuleStateListeners();
    const auto restoredModuleState = restoreActiveModuleState(*this, restoredActiveModule, restoredModuleStates);

    if (! restoredModuleState)
    {
        if (restoredActiveModule != ActiveModule::none)
            registerActiveModuleStateListeners();
        if (suspendProcessingForRestore && wasProcessingPrepared && currentSampleRate > 0.0)
            processingPrepared.store(true, std::memory_order_release);
        suppressHostStateNotifications.store(previousNotificationSuppression, std::memory_order_release);
        return false;
    }

    parameters.replaceState(restoredState);
    selectedCrossoverRange.store(restoredSelectedRange, std::memory_order_relaxed);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->setSelectedRange(restoredSelectedRange);

    if (fftProcessorBank != nullptr)
        fftProcessorBank->setSelectedRange(restoredSelectedRange);

    if (restoredActiveModule != ActiveModule::none)
        registerActiveModuleStateListeners();

    refreshHostSlotTargets();
    if (restoredABCompareState.has_value())
        shell_state_serialization::restoreABCompareState(*this, *restoredABCompareState);

    updateShellLatency();
    setLastEditorSize(static_cast<int>(parameters.state.getProperty(editorWidthStateKey, 0)),
                      static_cast<int>(parameters.state.getProperty(editorHeightStateKey, 0)));

    if (suspendProcessingForRestore && wasProcessingPrepared && currentSampleRate > 0.0)
        processingPrepared.store(true, std::memory_order_release);

    suppressHostStateNotifications.store(previousNotificationSuppression, std::memory_order_release);
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

    const auto restored = restoreStateInformationPreservingLoadedModule(data,
                                                                        sizeInBytes,
                                                                        false,
                                                                        false)
        || restoreStateInformation(data, sizeInBytes, false);

    if (! restored)
    {
        abCompareLatencyFloorSamples.store(previousLatencyFloor, std::memory_order_release);
        abCompareLatencyLocked.store(previousLatencyLocked, std::memory_order_release);
        updateShellLatency();
    }

    return restored;
}
