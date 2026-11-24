#include "BlockChainProcessor.h"
#include "StateValidation.h"

#include "../../crossover/ParameterAccess.h"
#include "../../crossover/ParameterIds.h"
#include "../../crossover/UiStateSchema.h"
#include "DspUtilities.h"

#include <utility>

BlockChainProcessor::BlockChainProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
      valueTreeState(moduleParameterHost, nullptr, "chain_state", ava::modules::chain::parameters::createParameterLayout())
{
    for (size_t range = 0; range < numRanges; ++range)
    {
        valueTreeState.state.setProperty(ava::modules::chain::gain::stateKey(range),
                                         ava::modules::chain::gain::serialise(ava::modules::chain::gain::defaultOrder), nullptr);
    }
    publishGainOrders();
    valueTreeState.state.addListener(this);
    cacheParameterPointers();
    setParameterListenersEnabled(true);
}

BlockChainProcessor::~BlockChainProcessor()
{
    valueTreeState.state.removeListener(this);
    setParameterListenersEnabled(false);
}

void BlockChainProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    syncParameters(true);
    processorBank.prepare(sampleRate, samplesPerBlock, ownerProcessor.getTotalNumOutputChannels());
    syncParameters(true);
    processorBank.reset();
}

void BlockChainProcessor::releaseResources()
{
    processorBank.releaseResources();
}

void BlockChainProcessor::resetProcessingState() noexcept
{
    processorBank.reset();
}

void BlockChainProcessor::getStateInformation(juce::MemoryBlock& destData) const
{
    auto state = const_cast<juce::AudioProcessorValueTreeState&>(valueTreeState).copyState();
    if (auto stateXml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary(*stateXml, destData);
}

bool BlockChainProcessor::isCurrentState(const juce::ValueTree& state,
                                        juce::AudioProcessorValueTreeState& parameters)
{
    return ava::modules::state::hasValidParameterState(state, parameters)
        && crossover_ui::hasCurrentStateProperties(state, "chain");
}

bool BlockChainProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    auto xmlState = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);

    if (xmlState == nullptr)
        return false;

    auto restoredState = juce::ValueTree::fromXml(*xmlState);

    if (!isCurrentState(restoredState, valueTreeState))
        return false;

    // Restoring a saved pair must not apply the link to intermediate values.
    setParameterListenersEnabled(false);
    valueTreeState.replaceState(restoredState);
    publishGainOrders();
    cacheParameterPointers();
    setParameterListenersEnabled(true);
    markParametersDirty();
    syncParameters(true);
    return true;
}

juce::AudioProcessorValueTreeState& BlockChainProcessor::getValueTreeState() noexcept
{
    return valueTreeState;
}

const juce::AudioProcessorValueTreeState& BlockChainProcessor::getValueTreeState() const noexcept
{
    return valueTreeState;
}

ava::modules::chain::dsp::ProcessorBank::RangeLatencies BlockChainProcessor::getRangeLatencies() const noexcept
{
    return processorBank.getRangeLatencies();
}

int BlockChainProcessor::getShiftLatencySamples() const noexcept
{
    const auto* core = processorBank.getProcessor(0);
    return core != nullptr ? core->getShiftLatencySamples() : 0;
}

int BlockChainProcessor::getMaximumVarispeedLatencySamples() const noexcept
{
    const auto* core = processorBank.getProcessor(0);
    return core != nullptr ? core->getMaximumVarispeedLatencySamples() : 0;
}

size_t BlockChainProcessor::ensureRangeCount(const size_t rangeCount)
{
    const auto createdRangeCount = processorBank.ensureRangeCount(rangeCount);
    markParametersDirty();
    return createdRangeCount;
}

size_t BlockChainProcessor::getCreatedRangeCount() const noexcept
{
    return processorBank.getCreatedRangeCount();
}

int BlockChainProcessor::getBlockRangeLatency(size_t rangeIndex, ava::modules::chain::blocks::Id block) const noexcept
{
    const auto* core = processorBank.getProcessor(rangeIndex);
    return core != nullptr ? core->getBlockLatencySamples(block) : 0;
}

void BlockChainProcessor::processBlockRange(size_t rangeIndex, ava::modules::chain::blocks::Id block, juce::AudioBuffer<float>& buffer)
{
    auto* core = processorBank.getProcessor(rangeIndex);
    if (core == nullptr || buffer.getNumChannels() < 1) return;
    if (block == ava::modules::chain::blocks::Id::varispeed ||
        block == ava::modules::chain::blocks::Id::sibil)
    {
        std::optional<int64_t> transportSample;
        std::optional<bool> transportPlaying;
        if (auto* playHead = ownerProcessor.getPlayHead())
            if (auto position = playHead->getPosition())
            {
                transportPlaying = position->getIsPlaying();
                if (*transportPlaying || ownerProcessor.isNonRealtime())
                    if (auto sample = position->getTimeInSamples()) transportSample = *sample;
            }
        if (block == ava::modules::chain::blocks::Id::varispeed)
            core->beginVarispeedBlock(transportSample, transportPlaying, ownerProcessor.isNonRealtime());
        else
            core->beginSibilBlock(transportSample, transportPlaying, buffer.getNumSamples());
    }
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        const auto result = core->processBlockSample(block, left[sample], right != nullptr ? right[sample] : left[sample]);
        left[sample] = float(result.left);
        if (right != nullptr) right[sample] = float(result.right);
    }
}

void BlockChainProcessor::markParametersDirty() noexcept
{
    parametersDirty.store(true, std::memory_order_relaxed);
}

void BlockChainProcessor::setParameterListenersEnabled(const bool enabled)
{
    ava::crossover::parameters::setRangeParameterListenersEnabled(valueTreeState,
                                                                  *this,
                                                                  ava::modules::chain::parameters::parameterSpecs,
                                                                  numRanges,
                                                                  enabled);
}

void BlockChainProcessor::updateOppositeDelay(const juce::String& parameterId, const float newValue)
{
    if (!parameterId.endsWith(".delay")) return;
    using namespace ava::modules::chain::parameters;
    for (size_t range = 0; range < numRanges; ++range)
    {
        auto& link = delayLinks[range];
        for (size_t side = 0; side < 2; ++side)
        {
            if (link.parameters[side] == nullptr || link.parameters[side]->paramID != parameterId) continue;
            const auto previous = link.previous[side].exchange(newValue);
            if (rawRangeParameters[range][toIndex(ParameterSlot::delayLinkOpp)]->load() < 0.5f
                || link.updating.exchange(true)) return;
            auto* peer = link.parameters[1 - side];
            const auto peerValue = peer->convertFrom0to1(peer->getValue());
            const auto& limits = peer->getNormalisableRange();
            const auto target = limits.snapToLegalValue(juce::jlimit(limits.start, limits.end,
                peerValue - (newValue - previous)));
            const auto normalised = peer->convertTo0to1(target);
            if (!juce::approximatelyEqual(peer->getValue(), normalised))
                peer->setValueNotifyingHost(normalised);
            link.updating.store(false);
            return;
        }
    }
}

void BlockChainProcessor::parameterChanged(const juce::String& parameterId, const float newValue)
{
    updateOppositeDelay(parameterId, newValue);
    if (newValue >= 0.5f)
    {
        const auto enforceExclusive = [&] (const auto& slots)
        {
            for (size_t range = 0; range < numRanges; ++range)
                for (const auto slot : slots)
                {
                    const auto id = ava::crossover::parameters::makeRangeParameterId(range,
                        ava::modules::chain::parameters::parameterSpecs[ava::modules::chain::parameters::toIndex(slot)].suffix);
                    if (id != parameterId) continue;
                    for (const auto other : slots)
                        if (other != slot)
                            if (auto* peer = valueTreeState.getParameter(ava::crossover::parameters::makeRangeParameterId(range,
                                    ava::modules::chain::parameters::parameterSpecs[ava::modules::chain::parameters::toIndex(other)].suffix));
                                peer != nullptr && peer->getValue() >= 0.5f)
                                peer->setValueNotifyingHost(0.0f);
                    return;
                }
        };
        enforceExclusive(ava::modules::chain::parameters::listenExclusiveSlots);
        enforceExclusive(ava::modules::chain::parameters::rectExclusiveSlots);
    }
    markParametersDirty();
}

void BlockChainProcessor::publishGainOrders()
{
    for (size_t range=0;range<numRanges;++range) {
        auto gainOrder=ava::modules::chain::gain::defaultOrder;
        if(valueTreeState.state.hasProperty(ava::modules::chain::gain::stateKey(range)))
            ava::modules::chain::gain::parse(valueTreeState.state.getProperty(ava::modules::chain::gain::stateKey(range)).toString(),gainOrder);
        gainOrders[range].store(ava::modules::chain::gain::pack(gainOrder),std::memory_order_release);
    }
    markParametersDirty();
}
void BlockChainProcessor::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& key)
{
    if(tree != valueTreeState.state) return;
    for(size_t range=0;range<numRanges;++range)
        if(key==ava::modules::chain::gain::stateKey(range)) {
            publishGainOrders();
            // AvaAudioProcessor observes the module ValueTree and forwards the
            // state change through its shared, coalesced host notification path.
            return;
        }
}
void BlockChainProcessor::valueTreeRedirected(juce::ValueTree&) { publishGainOrders(); }
ava::modules::chain::gain::Order BlockChainProcessor::getGainOrder(const size_t range) const noexcept
{
    return range<numRanges ? ava::modules::chain::gain::unpack(gainOrders[range].load(std::memory_order_acquire))
                           : ava::modules::chain::gain::defaultOrder;
}
void BlockChainProcessor::setGainOrder(const size_t range, const ava::modules::chain::gain::Order& order)
{
    if(range>=numRanges || !ava::modules::chain::gain::valid(order) || getGainOrder(range)==order) return;
    valueTreeState.state.setProperty(ava::modules::chain::gain::stateKey(range),ava::modules::chain::gain::serialise(order),nullptr);
}
