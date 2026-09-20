#pragma once

#include <JuceHeader.h>

#include "DspCore.h"
#include "Parameters.h"
#include "../shared/ParameterHost.h"

#include <array>
#include <atomic>

class DynModuleProcessor final : private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit DynModuleProcessor(juce::AudioProcessor& ownerProcessor);
    ~DynModuleProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void processBlock(juce::AudioBuffer<float>& buffer);

    void getStateInformation(juce::MemoryBlock& destData) const;
    bool setStateInformation(const void* data, int sizeInBytes);

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;
    juce::UndoManager& getUndoManager() noexcept;
    const juce::UndoManager& getUndoManager() const noexcept;
    dyn::dsp::ProcessorBank::RangeLatencies getRangeLatencies() const noexcept;
    size_t ensureRangeCount(size_t rangeCount);
    size_t getCreatedRangeCount() const noexcept;
    void processRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer);
    bool syncParameters(bool force = false);
    void markParametersDirty() noexcept;

private:
    static constexpr size_t numRanges = dyn::dsp::ProcessorBank::numRanges;
    static constexpr size_t numParameterSlots = dyn::parameters::numParameterSlots;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void cacheParameterPointers();
    void setParameterListenersEnabled(bool enabled);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void setRangeParameterValue(size_t rangeIndex,
                                dyn::parameters::ParameterSlot targetSlot,
                                float targetValue);
    float readRangeParameterValue(size_t rangeIndex,
                                  dyn::parameters::ParameterSlot slot) const noexcept;
    void syncAllFieldParameters(size_t rangeIndex,
                                dyn::parameters::ParameterSlot leftUp,
                                dyn::parameters::ParameterSlot leftDown,
                                dyn::parameters::ParameterSlot rightUp,
                                dyn::parameters::ParameterSlot rightDown);
    void syncUpDownParameterPairs(size_t rangeIndex,
                                  dyn::parameters::ParameterSlot leftUp,
                                  dyn::parameters::ParameterSlot leftDown,
                                  dyn::parameters::ParameterSlot rightUp,
                                  dyn::parameters::ParameterSlot rightDown);
    dyn::dsp::DspCore::Parameters readCrossoverRangeParameters(size_t rangeIndex) const;

    juce::AudioProcessor& ownerProcessor;
    ava::ModuleParameterHost moduleParameterHost;
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState valueTreeState;
    dyn::dsp::ProcessorBank processorBank;
    std::array<std::array<std::atomic<float>*, numParameterSlots>, numRanges> rawRangeParameters {};
    std::array<dyn::dsp::DspCore::Parameters, numRanges> currentRangeParameters {};
    std::atomic<bool> parametersDirty { true };
    std::atomic<bool> linkedParameterPropagationInProgress { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynModuleProcessor)
};
