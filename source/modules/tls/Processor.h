#pragma once

#include <JuceHeader.h>

#include "DspCore.h"
#include "Parameters.h"
#include "../shared/ParameterHost.h"

#include <array>
#include <atomic>

class TlsModuleProcessor final : private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit TlsModuleProcessor(juce::AudioProcessor& ownerProcessor);
    ~TlsModuleProcessor() override;

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
    tls::dsp::ProcessorBank::RangeLatencies getRangeLatencies() const noexcept;
    size_t ensureRangeCount(size_t rangeCount);
    size_t getCreatedRangeCount() const noexcept;
    void processRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer);
    bool syncParameters(bool force = false);
    void markParametersDirty() noexcept;

private:
    static constexpr size_t numRanges = tls::dsp::ProcessorBank::numRanges;
    static constexpr size_t numParameterSlots = tls::parameters::numParameterSlots;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void cacheParameterPointers();
    void setParameterListenersEnabled(bool enabled);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    tls::dsp::DspCore::Parameters readCrossoverRangeParameters(size_t rangeIndex) const;

    juce::AudioProcessor& ownerProcessor;
    ava::ModuleParameterHost moduleParameterHost;
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState valueTreeState;
    tls::dsp::ProcessorBank processorBank;
    std::array<std::array<std::atomic<float>*, numParameterSlots>, numRanges> rawRangeParameters {};
    std::array<tls::dsp::DspCore::Parameters, numRanges> currentRangeParameters {};
    std::atomic<bool> parametersDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TlsModuleProcessor)
};
