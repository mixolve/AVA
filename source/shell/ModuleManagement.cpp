#include "Processor.h"
#include "../modules/eql/ProcessorBank.h"
#include "../modules/fft/ProcessorBank.h"
#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/fft/Processor.h"
#include "../modules/trs/Processor.h"
#include "WindowState.h"
#include "../modules/eql/Processor.h"

#include <algorithm>

const char* AvaAudioProcessor::stateIdForModule(const ActiveModule module) noexcept
{
    switch (module)
    {
        case ActiveModule::tls: return tlsModuleId;
        case ActiveModule::eql: return eqlModuleId;
        case ActiveModule::fft: return fftModuleId;
        case ActiveModule::dyn: return dynModuleId;
        case ActiveModule::trs: return trsModuleId;
        case ActiveModule::none: break;
    }

    return "";
}

AvaAudioProcessor::ActiveModule AvaAudioProcessor::moduleFromStateId(const juce::String& moduleId)
{
    if (moduleId == eqlModuleId)
        return ActiveModule::eql;

    if (moduleId == fftModuleId)
        return ActiveModule::fft;

    if (moduleId == tlsModuleId)
        return ActiveModule::tls;

    if (moduleId == dynModuleId)
        return ActiveModule::dyn;

    if (moduleId == trsModuleId)
        return ActiveModule::trs;

    return ActiveModule::none;
}

juce::AudioProcessorValueTreeState& AvaAudioProcessor::getValueTreeState() noexcept
{
    return parameters;
}

const juce::AudioProcessorValueTreeState& AvaAudioProcessor::getValueTreeState() const noexcept
{
    return parameters;
}

AvaAudioProcessor::ActiveModule AvaAudioProcessor::getActiveModule() const noexcept
{
    return activeModule.load(std::memory_order_acquire);
}

void AvaAudioProcessor::setActiveModule(const ActiveModule module)
{
    activeModule.store(module, std::memory_order_release);

    const auto activeId = juce::String(stateIdForModule(module));

    if (activeId.isNotEmpty())
        parameters.state.setProperty(activeModuleStateKey, activeId, nullptr);
    else
        parameters.state.removeProperty(activeModuleStateKey, nullptr);
}

bool AvaAudioProcessor::loadModule(const ActiveModule module)
{
    const juce::ScopedLock lock(processingLock);

    if (module == ActiveModule::none || getActiveModule() != ActiveModule::none)
        return false;

    const ScopedProcessingSuspend suspendGuard(*this);
    clearActiveModuleStateListeners();
    const auto wasProcessingPrepared = processingPrepared.exchange(false, std::memory_order_acq_rel);
    const auto restoreProcessingPrepared = [this, wasProcessingPrepared]
    {
        if (wasProcessingPrepared && currentSampleRate > 0.0)
            processingPrepared.store(true, std::memory_order_release);
    };

    if (! createModuleInstance(module))
    {
        restoreProcessingPrepared();
        return false;
    }

    if (module == ActiveModule::eql)
        if (auto* eql = getEqlProcessorBank())
            eql->loadInitialFilterPreset();

    setActiveModule(module);
    registerActiveModuleStateListeners();
    refreshHostSlotTargets();
    updateShellLatency();
    notifyHostOfStateChange();
    restoreProcessingPrepared();
    return true;
}

bool AvaAudioProcessor::clearLoadedModule()
{
    const juce::ScopedLock lock(processingLock);

    if (getActiveModule() == ActiveModule::none)
        return false;

    const auto closingModule = getActiveModule();

    const ScopedProcessingSuspend suspendGuard(*this);
    clearActiveModuleStateListeners();
    resetModuleProcessors();
    setActiveModule(ActiveModule::none);

    if (closingModule == ActiveModule::eql)
    {
        for (size_t rangeIndex = 0; rangeIndex < ava::crossover::BufferRouter::numRanges; ++rangeIndex)
            parameters.state.removeProperty(getEditorFilterDisplayOrderStateKey(rangeIndex), nullptr);
    }

    refreshHostSlotTargets();
    updateShellLatency();
    notifyHostOfStateChange();
    return true;
}

void AvaAudioProcessor::registerActiveModuleStateListeners()
{
    clearActiveModuleStateListeners();

    auto observeModule = [this] (auto* processor)
    {
        if (processor == nullptr)
            return;

        observedModuleValueTreeState = &processor->getValueTreeState();
    };

    switch (getActiveModule())
    {
        case ActiveModule::eql: observeModule(getEqlModuleProcessor()); break;
        case ActiveModule::fft: observeModule(getFftModuleProcessor()); break;
        case ActiveModule::tls: observeModule(getTlsModuleProcessor()); break;
        case ActiveModule::dyn: observeModule(getDynModuleProcessor()); break;
        case ActiveModule::trs: observeModule(getTrsModuleProcessor()); break;
        case ActiveModule::none: break;
    }

    if (observedModuleValueTreeState == nullptr)
        return;

    observedModuleParameterIds.reserve(static_cast<size_t>(observedModuleValueTreeState->state.getNumChildren()));

    for (const auto parameterState : observedModuleValueTreeState->state)
    {
        const auto parameterId = parameterState.getProperty("id").toString();

        if (parameterId.isEmpty())
            continue;

        observedModuleValueTreeState->addParameterListener(parameterId, this);
        observedModuleParameterIds.push_back(parameterId);
    }

    observedModuleState = observedModuleValueTreeState->state;

    if (observedModuleState.isValid())
        observedModuleState.addListener(this);
}

void AvaAudioProcessor::clearActiveModuleStateListeners()
{
    if (observedModuleValueTreeState != nullptr)
    {
        for (const auto& parameterId : observedModuleParameterIds)
            observedModuleValueTreeState->removeParameterListener(parameterId, this);
    }

    observedModuleValueTreeState = nullptr;
    observedModuleParameterIds.clear();

    if (observedModuleState.isValid())
        observedModuleState.removeListener(this);

    observedModuleState = {};
}

void AvaAudioProcessor::parameterChanged(const juce::String& parameterID, const float newValue)
{
    for (int slotIndex = 0; slotIndex < hostAutomationSlotCount; ++slotIndex)
    {
        if (parameterID == hostSlotParameterIds[static_cast<size_t>(slotIndex)])
        {
            applyHostSlotValue(slotIndex, newValue);
            notifyHostOfStateChange();
            return;
        }
    }

    if (parameterID == paramCrossoverActiveSplitCountId)
    {
        const auto splitCount = juce::jlimit(0,
                                             static_cast<int>(ava::crossover::BufferRouter::numSplits),
                                             juce::roundToInt(newValue));
        requestedCrossoverRangeCount.store(static_cast<size_t>(splitCount + 1), std::memory_order_release);
        triggerAsyncUpdate();
    }

    notifyHostOfStateChange();
}

void AvaAudioProcessor::handleAsyncUpdate()
{
    ensureActiveCrossoverRangeCount(requestedCrossoverRangeCount.load(std::memory_order_acquire));
    applyPendingShellUpdates();
}

void AvaAudioProcessor::applyPendingShellUpdates()
{
    const auto requestedLatency = requestedLatencySamples.load(std::memory_order_acquire);

    if (getLatencySamples() != requestedLatency)
        setLatencySamples(requestedLatency);

    if (pendingHostStateNotification.exchange(false, std::memory_order_acq_rel)
        && ! suppressHostStateNotifications.load(std::memory_order_relaxed))
    {
        auto details = juce::AudioProcessorListener::ChangeDetails()
                           .withNonParameterStateChanged(true);

       #if JucePlugin_Build_AU
        details = details.withProgramChanged(true);
       #endif

        updateHostDisplay(details);
    }
}

void AvaAudioProcessor::ensureActiveCrossoverRangeCount(const size_t rangeCount)
{
    const auto constrainedRangeCount = std::clamp(rangeCount, size_t { 1 }, EqlProcessorBank::numRanges);
    const juce::ScopedLock lock(processingLock);
    const auto eqlNeedsRange = eqlProcessorBank != nullptr
        && eqlProcessorBank->getCreatedRangeCount() < constrainedRangeCount;
    const auto fftNeedsRange = fftProcessorBank != nullptr
        && fftProcessorBank->getCreatedRangeCount() < constrainedRangeCount;
    const auto tlsNeedsRange = tlsModuleProcessor != nullptr
        && tlsModuleProcessor->getCreatedRangeCount() < constrainedRangeCount;
    const auto dynNeedsRange = dynModuleProcessor != nullptr
        && dynModuleProcessor->getCreatedRangeCount() < constrainedRangeCount;
    const auto trsNeedsRange = trsModuleProcessor != nullptr
        && trsModuleProcessor->getCreatedRangeCount() < constrainedRangeCount;

    if (! eqlNeedsRange && ! fftNeedsRange && ! tlsNeedsRange && ! dynNeedsRange && ! trsNeedsRange)
        return;

    const ScopedProcessingSuspend suspendGuard(*this);
    const auto wasProcessingPrepared = processingPrepared.exchange(false, std::memory_order_acq_rel);

    if (eqlNeedsRange)
        eqlProcessorBank->ensureRangeCount(constrainedRangeCount);

    if (fftNeedsRange)
        fftProcessorBank->ensureRangeCount(constrainedRangeCount);

    if (tlsNeedsRange)
        tlsModuleProcessor->ensureRangeCount(constrainedRangeCount);

    if (dynNeedsRange)
        dynModuleProcessor->ensureRangeCount(constrainedRangeCount);

    if (trsNeedsRange)
        trsModuleProcessor->ensureRangeCount(constrainedRangeCount);

    updateShellLatency();

    if (wasProcessingPrepared && currentSampleRate > 0.0)
        processingPrepared.store(true, std::memory_order_release);
}

void AvaAudioProcessor::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&)
{
    notifyHostOfStateChange();
}

TlsModuleProcessor* AvaAudioProcessor::getTlsModuleProcessor() noexcept
{
    return tlsModuleProcessor.get();
}

const TlsModuleProcessor* AvaAudioProcessor::getTlsModuleProcessor() const noexcept
{
    return tlsModuleProcessor.get();
}

FftModuleProcessor* AvaAudioProcessor::getFftModuleProcessor() noexcept
{
    return fftProcessorBank != nullptr ? fftProcessorBank->getSelectedProcessor() : nullptr;
}

const FftModuleProcessor* AvaAudioProcessor::getFftModuleProcessor() const noexcept
{
    return fftProcessorBank != nullptr ? fftProcessorBank->getSelectedProcessor() : nullptr;
}

FftProcessorBank* AvaAudioProcessor::getFftProcessorBank() noexcept
{
    return fftProcessorBank.get();
}

const FftProcessorBank* AvaAudioProcessor::getFftProcessorBank() const noexcept
{
    return fftProcessorBank.get();
}

DynModuleProcessor* AvaAudioProcessor::getDynModuleProcessor() noexcept
{
    return dynModuleProcessor.get();
}

const DynModuleProcessor* AvaAudioProcessor::getDynModuleProcessor() const noexcept
{
    return dynModuleProcessor.get();
}

TrsModuleProcessor* AvaAudioProcessor::getTrsModuleProcessor() noexcept
{
    return trsModuleProcessor.get();
}

const TrsModuleProcessor* AvaAudioProcessor::getTrsModuleProcessor() const noexcept
{
    return trsModuleProcessor.get();
}

void AvaAudioProcessor::resetModuleProcessors() noexcept
{
    for (auto& target : hostSlotTargets)
        target.store(nullptr, std::memory_order_release);

    eqlProcessorBank.reset();
    fftProcessorBank.reset();
    tlsModuleProcessor.reset();
    dynModuleProcessor.reset();
    trsModuleProcessor.reset();
}

bool AvaAudioProcessor::createModuleInstance(const ActiveModule module)
{
    const auto requiredRangeCount = getCrossoverSettings().activeSplitCount + 1;
    const auto selectedRange = juce::jmin(selectedCrossoverRange.load(std::memory_order_relaxed),
                                          requiredRangeCount - 1);
    selectedCrossoverRange.store(selectedRange, std::memory_order_relaxed);

    auto prepareModule = [this] (auto& processor)
    {
        if (currentSampleRate > 0.0 && preparedBlockSize > 0)
            processor->prepareToPlay(currentSampleRate, preparedBlockSize);

        return true;
    };

    switch (module)
    {
        case ActiveModule::eql:
            eqlProcessorBank = std::make_unique<EqlProcessorBank>();
            eqlProcessorBank->ensureRangeCount(requiredRangeCount);
            eqlProcessorBank->setSelectedRange(selectedRange);
            return prepareModule(eqlProcessorBank);

        case ActiveModule::fft:
            fftProcessorBank = std::make_unique<FftProcessorBank>(*this);
            fftProcessorBank->ensureRangeCount(requiredRangeCount);
            fftProcessorBank->setSelectedRange(selectedRange);
            return prepareModule(fftProcessorBank);

        case ActiveModule::tls:
            tlsModuleProcessor = std::make_unique<TlsModuleProcessor>(*this);
            tlsModuleProcessor->ensureRangeCount(requiredRangeCount);
            return prepareModule(tlsModuleProcessor);

        case ActiveModule::dyn:
            dynModuleProcessor = std::make_unique<DynModuleProcessor>(*this);
            dynModuleProcessor->ensureRangeCount(requiredRangeCount);
            return prepareModule(dynModuleProcessor);

        case ActiveModule::trs:
            trsModuleProcessor = std::make_unique<TrsModuleProcessor>(*this);
            trsModuleProcessor->ensureRangeCount(requiredRangeCount);
            return prepareModule(trsModuleProcessor);

        case ActiveModule::none:
            break;
    }

    return false;
}

EqlModuleProcessor* AvaAudioProcessor::getEqlModuleProcessor() noexcept
{
    return eqlProcessorBank != nullptr ? eqlProcessorBank->getSelectedProcessor() : nullptr;
}

const EqlModuleProcessor* AvaAudioProcessor::getEqlModuleProcessor() const noexcept
{
    return eqlProcessorBank != nullptr ? eqlProcessorBank->getSelectedProcessor() : nullptr;
}

EqlProcessorBank* AvaAudioProcessor::getEqlProcessorBank() noexcept
{
    return eqlProcessorBank.get();
}

const EqlProcessorBank* AvaAudioProcessor::getEqlProcessorBank() const noexcept
{
    return eqlProcessorBank.get();
}

void AvaAudioProcessor::setSelectedCrossoverRange(const size_t rangeIndex)
{
    const auto activeRangeCount = static_cast<size_t>(getCrossoverSettings().activeSplitCount + 1);
    const auto selectedRange = juce::jmin(rangeIndex, activeRangeCount - 1);
    selectedCrossoverRange.store(selectedRange, std::memory_order_relaxed);
    ensureActiveCrossoverRangeCount(activeRangeCount);

    if (eqlProcessorBank != nullptr)
        eqlProcessorBank->setSelectedRange(selectedRange);

    if (fftProcessorBank != nullptr)
        fftProcessorBank->setSelectedRange(selectedRange);

    registerActiveModuleStateListeners();
    refreshHostSlotTargets();
}

size_t AvaAudioProcessor::getSelectedCrossoverRange() const noexcept
{
    return selectedCrossoverRange.load(std::memory_order_relaxed);
}
