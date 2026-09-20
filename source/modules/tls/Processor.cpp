#include "Processor.h"
#include "../shared/StateUtilities.h"

#include "../../crossover/ParameterAccess.h"
#include "../../crossover/UiState.h"
#include "../shared/DspUtilities.h"

TlsModuleProcessor::TlsModuleProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
      valueTreeState(moduleParameterHost, &undoManager, "tls_state", createParameterLayout())
{
    cacheParameterPointers();
    setParameterListenersEnabled(true);
}

TlsModuleProcessor::~TlsModuleProcessor()
{
    setParameterListenersEnabled(false);
}

void TlsModuleProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    processorBank.prepare(sampleRate, samplesPerBlock, ownerProcessor.getTotalNumOutputChannels());
    syncParameters(true);
    processorBank.reset();
}

void TlsModuleProcessor::releaseResources()
{
    processorBank.releaseResources();
}

void TlsModuleProcessor::resetProcessingState() noexcept
{
    processorBank.reset();
}

void TlsModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    ava::modules::dsp::clearOutputOnlyChannels(ownerProcessor, buffer);
    syncParameters();
    processorBank.processRange(0, buffer);
}

void TlsModuleProcessor::getStateInformation(juce::MemoryBlock& destData) const
{
    if (auto stateXml = const_cast<juce::AudioProcessorValueTreeState&>(valueTreeState).copyState().createXml())
        juce::AudioProcessor::copyXmlToBinary(*stateXml, destData);
}

bool TlsModuleProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    auto xmlState = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);

    if (xmlState == nullptr)
        return false;

    auto restoredState = juce::ValueTree::fromXml(*xmlState);

    if (! ava::modules::state::hasExactParameterState(restoredState, valueTreeState)
        || ! crossover_ui::hasCurrentStateProperties(restoredState, "tls"))
        return false;

    valueTreeState.replaceState(restoredState);
    cacheParameterPointers();
    markParametersDirty();
    syncParameters(true);
    return true;
}

juce::AudioProcessorValueTreeState& TlsModuleProcessor::getValueTreeState() noexcept
{
    return valueTreeState;
}

const juce::AudioProcessorValueTreeState& TlsModuleProcessor::getValueTreeState() const noexcept
{
    return valueTreeState;
}

juce::UndoManager& TlsModuleProcessor::getUndoManager() noexcept
{
    return undoManager;
}

const juce::UndoManager& TlsModuleProcessor::getUndoManager() const noexcept
{
    return undoManager;
}

tls::dsp::ProcessorBank::RangeLatencies TlsModuleProcessor::getRangeLatencies() const noexcept
{
    return processorBank.getRangeLatencies();
}

size_t TlsModuleProcessor::ensureRangeCount(const size_t rangeCount)
{
    const auto createdRangeCount = processorBank.ensureRangeCount(rangeCount);
    processorBank.setRangeParameters(currentRangeParameters);
    return createdRangeCount;
}

size_t TlsModuleProcessor::getCreatedRangeCount() const noexcept
{
    return processorBank.getCreatedRangeCount();
}

void TlsModuleProcessor::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
{
    processorBank.processRange(rangeIndex, buffer);
}

void TlsModuleProcessor::markParametersDirty() noexcept
{
    parametersDirty.store(true, std::memory_order_relaxed);
}

void TlsModuleProcessor::setParameterListenersEnabled(const bool enabled)
{
    ava::crossover::parameters::setRangeParameterListenersEnabled(valueTreeState,
                                                                  *this,
                                                                  tls::parameters::parameterSpecs,
                                                                  numRanges,
                                                                  enabled);
}

void TlsModuleProcessor::parameterChanged(const juce::String&, float)
{
    markParametersDirty();
}

juce::AudioProcessorValueTreeState::ParameterLayout TlsModuleProcessor::createParameterLayout()
{
    return tls::parameters::createParameterLayout();
}
