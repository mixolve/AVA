#include "Processor.h"
#include "../../shell/HostChangeDetails.h"
#include "../shared/StateValidation.h"

#include "../../crossover/ParameterAccess.h"
#include "../../crossover/ParameterIds.h"
#include "../../crossover/UiStateSchema.h"
#include "../shared/DspUtilities.h"

TlsModuleProcessor::TlsModuleProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
      valueTreeState(moduleParameterHost, &undoManager, "tls_state", createParameterLayout())
{
    for (size_t range = 0; range < numRanges; ++range)
    {
        valueTreeState.state.setProperty(tls::blocks::stateKey(range),
                                         tls::blocks::serialise(tls::blocks::defaultOrder), nullptr);
        valueTreeState.state.setProperty(tls::gain::stateKey(range),
                                         tls::gain::serialise(tls::gain::defaultOrder), nullptr);
    }
    publishBlockOrders();
    valueTreeState.state.addListener(this);
    cacheParameterPointers();
    setParameterListenersEnabled(true);
}

TlsModuleProcessor::~TlsModuleProcessor()
{
    valueTreeState.state.removeListener(this);
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
    publishBlockOrders();
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

void TlsModuleProcessor::publishBlockOrders()
{
    for (size_t range=0;range<numRanges;++range) {
        auto order=tls::blocks::defaultOrder;
        if (valueTreeState.state.hasProperty(tls::blocks::stateKey(range)))
            tls::blocks::parse(valueTreeState.state.getProperty(tls::blocks::stateKey(range)).toString(),order);
        blockOrders[range].store(tls::blocks::pack(order),std::memory_order_release);
        auto gainOrder=tls::gain::defaultOrder;
        if(valueTreeState.state.hasProperty(tls::gain::stateKey(range)))
            tls::gain::parse(valueTreeState.state.getProperty(tls::gain::stateKey(range)).toString(),gainOrder);
        gainOrders[range].store(tls::gain::pack(gainOrder),std::memory_order_release);
    }
    markParametersDirty();
}
void TlsModuleProcessor::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& key)
{
    if(tree != valueTreeState.state) return;
    for(size_t range=0;range<numRanges;++range)
        if(key==tls::blocks::stateKey(range) || key==tls::gain::stateKey(range)) {
            publishBlockOrders();
            ownerProcessor.updateHostDisplay(makeHostStateChangeDetails());
            return;
        }
}
void TlsModuleProcessor::valueTreeRedirected(juce::ValueTree&) { publishBlockOrders(); }
tls::blocks::Order TlsModuleProcessor::getBlockOrder(const size_t range) const noexcept
{
    return range<numRanges ? tls::blocks::unpack(blockOrders[range].load(std::memory_order_acquire))
                           : tls::blocks::defaultOrder;
}
void TlsModuleProcessor::setBlockOrder(const size_t range, const tls::blocks::Order& order)
{
    if(range>=numRanges || !tls::blocks::valid(order) || getBlockOrder(range)==order) return;
    undoManager.beginNewTransaction("ARRANGE TLS");
    valueTreeState.state.setProperty(tls::blocks::stateKey(range),tls::blocks::serialise(order),&undoManager);
    undoManager.beginNewTransaction();
}

tls::gain::Order TlsModuleProcessor::getGainOrder(const size_t range) const noexcept
{
    return range<numRanges ? tls::gain::unpack(gainOrders[range].load(std::memory_order_acquire))
                           : tls::gain::defaultOrder;
}
void TlsModuleProcessor::setGainOrder(const size_t range, const tls::gain::Order& order)
{
    if(range>=numRanges || !tls::gain::valid(order) || getGainOrder(range)==order) return;
    undoManager.beginNewTransaction("ARRANGE TLS GAIN");
    valueTreeState.state.setProperty(tls::gain::stateKey(range),tls::gain::serialise(order),&undoManager);
    undoManager.beginNewTransaction();
}
