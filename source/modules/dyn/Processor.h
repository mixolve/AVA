#pragma once

#include <JuceHeader.h>

#include "SampleProcessor.h"
#include "Parameters.h"
#include "SpectralProcessor.h"
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
    void processRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer,
                      const juce::AudioBuffer<float>* routingInputs = nullptr, int routingOffset = 0);
    bool syncParameters(bool force = false, bool readHostTempo = false);
    bool isSpectralRange(size_t rangeIndex) const noexcept;
    bool isCorrelationRange(size_t rangeIndex) const noexcept;
    bool isTransustRange(size_t rangeIndex) const noexcept;
    void* getDomainEditorIdentity(size_t rangeIndex) const noexcept;
    void markParametersDirty() noexcept;
    void setProcessingInputChannel(int choice) noexcept;
    bool isSidechainChannelAvailable(int choice) const;

private:
    static constexpr size_t numRanges = dyn::dsp::ProcessorBank::numRanges;
    static constexpr size_t numParameterSlots = dyn::parameters::numParameterSlots;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void cacheParameterPointers();
    double getHostBpm() const noexcept;
    void setParameterListenersEnabled(bool enabled);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    dyn::dsp::DspCore::Parameters readCrossoverRangeParameters(size_t rangeIndex, double hostBpm) const;

    juce::AudioProcessor& ownerProcessor;
    ava::ModuleParameterHost moduleParameterHost;
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState valueTreeState;
    dyn::dsp::ProcessorBank processorBank;
    std::array<std::unique_ptr<DynSpectralProcessor>, numRanges> spectralProcessors;
    int processingInputChannel = 0;
    std::array<int, numRanges> processingSidechainChannel {};
    std::array<bool, numRanges> processingSpectral {};
    std::array<bool, numRanges> processingTransust {};
    std::array<bool, numRanges> processingCorrelation {};
    std::array<std::array<float, 2>, numRanges> lastDomainOutput {};
    std::array<int, numRanges> domainFadeRemaining {};
    int domainFadeSamples = 240;
    double spectralSampleRate = 0.0;
    int spectralBlockSize = 0;
    void applyDomainFade(size_t rangeIndex, juce::AudioBuffer<float>& buffer) noexcept;
    float loadParameter(size_t rangeIndex, dyn::parameters::ParameterSlot slot) const noexcept;
    void processSpectralRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer,
                              const juce::AudioBuffer<float>* sidechain, int sidechainOffset);
    std::array<std::array<std::atomic<float>*, numParameterSlots>, numRanges> rawRangeParameters {};
    std::array<std::array<juce::RangedAudioParameter*, dyn::parameters::linkedParameterSlots.size()>, numRanges> linkedParameters {};
    std::array<dyn::dsp::DspCore::Parameters, numRanges> currentRangeParameters {};
    std::atomic<bool> parametersDirty { true };
    double lastHostBpm = 120.0;
    std::atomic<bool> linkedParameterUpdateInProgress { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynModuleProcessor)
};
