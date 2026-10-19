#include "Processor.h"
#include "HostChangeDetails.h"
#include "ABCompareSerialization.h"

#include "../modules/tls/Processor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../routing/Runtime.h"

namespace
{
template <typename Processor>
void storeBinaryModuleState(juce::ValueTree& state,
                            const juce::Identifier& property,
                            Processor* processor)
{
    if (processor == nullptr)
    {
        state.removeProperty(property, nullptr);
        return;
    }

    juce::MemoryBlock stateData;
    processor->getStateInformation(stateData);

    if (stateData.getSize() > 0)
        state.setProperty(property, stateData.toBase64Encoding(), nullptr);
    else
        state.removeProperty(property, nullptr);
}

}

void AvaAudioProcessor::removeModuleStateProperties(juce::ValueTree& state)
{
    state.removeProperty(activeModuleStateKey, nullptr);
    state.removeProperty(eqlModuleStateKey, nullptr);
    state.removeProperty(tlsModuleStateKey, nullptr);
    state.removeProperty(dynModuleStateKey, nullptr);
}

void AvaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    writeStateInformation(destData, true);
}

void AvaAudioProcessor::getStateInformationForABCompareSnapshot(juce::MemoryBlock& destData)
{
    writeStateInformation(destData, false);
}

void AvaAudioProcessor::writeStateInformation(juce::MemoryBlock& destData, const bool includeABCompareState)
{
    const juce::ScopedLock lock(processingLock);
    const auto editorWidth = lastEditorWidth.load(std::memory_order_relaxed);
    const auto editorHeight = lastEditorHeight.load(std::memory_order_relaxed);
    auto state = parameters.copyState();

    if (editorWidth > 0 && editorHeight > 0)
    {
        state.setProperty(editorWidthStateKey, editorWidth, nullptr);
        state.setProperty(editorHeightStateKey, editorHeight, nullptr);
    }

    const auto active = getActiveModule();

    if (active == ActiveModule::none)
    {
        removeModuleStateProperties(state);
    }
    else
    {
        state.setProperty(activeModuleStateKey, stateIdForModule(active), nullptr);
        storeBinaryModuleState(state, eqlModuleStateKey, getEqlProcessorBank());
        storeBinaryModuleState(state, tlsModuleStateKey, getTlsModuleProcessor());
        storeBinaryModuleState(state, dynModuleStateKey, getDynModuleProcessor());
    }

    if (includeABCompareState)
        shell_state_serialization::storeABCompareState(state, *this);
    else
        shell_state_serialization::removeABCompareStateProperties(state);

    if (routingRuntime != nullptr)
        routingRuntime->writeProcessorStates(state);

    if (auto stateXml = state.createXml())
        copyXmlToBinary(*stateXml, destData);
}

juce::Point<int> AvaAudioProcessor::getLastEditorSize() const noexcept
{
    return { lastEditorWidth.load(std::memory_order_relaxed),
             lastEditorHeight.load(std::memory_order_relaxed) };
}

void AvaAudioProcessor::setLastEditorSize(const int width, const int height) noexcept
{
    lastEditorWidth.store(juce::jmax(0, width), std::memory_order_relaxed);
    lastEditorHeight.store(juce::jmax(0, height), std::memory_order_relaxed);
}

void AvaAudioProcessor::notifyHostOfStateChange()
{
    if (suppressHostStateNotifications.load(std::memory_order_relaxed))
        return;

    if (parentStateChanged != nullptr)
        parentStateChanged();

    if (auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        messageManager != nullptr && messageManager->isThisTheMessageThread())
    {
        pendingHostStateNotification.store(false, std::memory_order_release);
        updateHostDisplay(makeHostStateChangeDetails());
        return;
    }

    if (! pendingHostStateNotification.exchange(true, std::memory_order_acq_rel))
        triggerAsyncUpdate();
}
