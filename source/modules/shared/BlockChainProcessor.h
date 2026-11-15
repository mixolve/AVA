#pragma once

#include <JuceHeader.h>

#include "BlockDspCore.h"
#include "BlockParameters.h"
#include "ParameterHost.h"

#include <array>
#include <atomic>

class BlockChainProcessor final : private juce::AudioProcessorValueTreeState::Listener,
                                 private juce::ValueTree::Listener
{
public:
    explicit BlockChainProcessor(juce::AudioProcessor& ownerProcessor);
    ~BlockChainProcessor() override;
    juce::AudioProcessor& getOwnerProcessor() noexcept { return ownerProcessor; }

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;

    void getStateInformation(juce::MemoryBlock& destData) const;
    bool setStateInformation(const void* data, int sizeInBytes);
    static bool isCurrentState(const juce::ValueTree& state,
                               juce::AudioProcessorValueTreeState& parameters);

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;
    ava::modules::chain::dsp::ProcessorBank::RangeLatencies getRangeLatencies() const noexcept;
    size_t ensureRangeCount(size_t rangeCount);
    int getShiftLatencySamples() const noexcept;
    int getMaximumVarispeedLatencySamples() const noexcept;
    size_t getCreatedRangeCount() const noexcept;
    void processBlockRange(size_t rangeIndex, ava::modules::chain::blocks::Id block, juce::AudioBuffer<float>& buffer);
    int getBlockRangeLatency(size_t rangeIndex, ava::modules::chain::blocks::Id block) const noexcept;
    bool syncParameters(bool force = false);
    void markParametersDirty() noexcept;
    ava::modules::chain::gain::Order getGainOrder(size_t range) const noexcept;
    void setGainOrder(size_t range, const ava::modules::chain::gain::Order& order);

private:
    static constexpr size_t numRanges = ava::modules::chain::dsp::ProcessorBank::numRanges;
    static constexpr size_t numParameterSlots = ava::modules::chain::parameters::numParameterSlots;

    void cacheParameterPointers();
    void updateOppositeDelay(const juce::String& parameterId, float newValue);
    void publishGainOrders();
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected(juce::ValueTree&) override;
    void setParameterListenersEnabled(bool enabled);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    ava::modules::chain::dsp::DspCore::Parameters readCrossoverRangeParameters(size_t rangeIndex) const;

    juce::AudioProcessor& ownerProcessor;
    ava::ModuleParameterHost moduleParameterHost;
    juce::AudioProcessorValueTreeState valueTreeState;
    ava::modules::chain::dsp::ProcessorBank processorBank;
    std::array<std::array<std::atomic<float>*, numParameterSlots>, numRanges> rawRangeParameters {};
    std::array<ava::modules::chain::dsp::DspCore::Parameters, numRanges> currentRangeParameters {};
    std::array<std::atomic<uint64_t>, numRanges> gainOrders {};
    struct DelayLinkState
    {
        std::array<juce::RangedAudioParameter*, 2> parameters {};
        std::array<std::atomic<float>, 2> previous {};
        std::atomic<bool> updating { false };
    };
    std::array<DelayLinkState, numRanges> delayLinks;
    std::atomic<bool> parametersDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlockChainProcessor)
};
