#include "Processor.h"

#include "../shared/DspUtilities.h"

DynModuleProcessor::DynModuleProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
      valueTreeState(moduleParameterHost, &undoManager, "dyn_state", createParameterLayout())
{
    cacheParameterPointers();
    setParameterListenersEnabled(true);
}

DynModuleProcessor::~DynModuleProcessor()
{
    setParameterListenersEnabled(false);
}

void DynModuleProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    processorBank.prepare(sampleRate, samplesPerBlock, ownerProcessor.getTotalNumOutputChannels());
    syncParameters(true);
    processorBank.reset();
}

void DynModuleProcessor::releaseResources()
{
    processorBank.releaseResources();
}

void DynModuleProcessor::resetProcessingState() noexcept
{
    processorBank.reset();
}

void DynModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    ava::modules::dsp::clearOutputOnlyChannels(ownerProcessor, buffer);
    syncParameters();
    processorBank.processRange(0, buffer);
}

juce::AudioProcessorValueTreeState& DynModuleProcessor::getValueTreeState() noexcept
{
    return valueTreeState;
}

const juce::AudioProcessorValueTreeState& DynModuleProcessor::getValueTreeState() const noexcept
{
    return valueTreeState;
}

juce::UndoManager& DynModuleProcessor::getUndoManager() noexcept
{
    return undoManager;
}

const juce::UndoManager& DynModuleProcessor::getUndoManager() const noexcept
{
    return undoManager;
}

dyn::dsp::ProcessorBank::RangeLatencies DynModuleProcessor::getRangeLatencies() const noexcept
{
    return processorBank.getRangeLatencies();
}

size_t DynModuleProcessor::ensureRangeCount(const size_t rangeCount)
{
    const auto createdRangeCount = processorBank.ensureRangeCount(rangeCount);
    processorBank.setRangeParameters(currentRangeParameters);
    return createdRangeCount;
}

size_t DynModuleProcessor::getCreatedRangeCount() const noexcept
{
    return processorBank.getCreatedRangeCount();
}

void DynModuleProcessor::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer)
{
    processorBank.processRange(rangeIndex, buffer);
}

void DynModuleProcessor::markParametersDirty() noexcept
{
    parametersDirty.store(true, std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout DynModuleProcessor::createParameterLayout()
{
    return dyn::parameters::createParameterLayout();
}
