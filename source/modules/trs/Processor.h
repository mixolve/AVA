#pragma once

#include <JuceHeader.h>

#include "../shared/SampleRangeBank.h"
#include "DspCore.h"
#include "../shared/ParameterHost.h"

#include <array>
#include <atomic>

class TrsModuleProcessor final
{
public:
    using RangeLatencies = ava::modules::SampleRangeBank<trs::dsp::DspCore>::RangeLatencies;

    inline static constexpr auto paramTransientMuteId = "mute.transient.icon";
    inline static constexpr auto paramTransientGainId = "gain.transient";
    inline static constexpr auto paramSustainMuteId = "mute.sustain.icon";
    inline static constexpr auto paramSustainGainId = "gain.sustain";
    inline static constexpr auto paramHoldId = "hold";
    inline static constexpr auto paramHoldTypeId = "hold-type";
    inline static constexpr auto paramHoldSyncId = "hold.sync";
    inline static constexpr auto paramReleaseId = "release";
    inline static constexpr auto paramReleaseCurveId = "release-curve";
    inline static constexpr auto paramReleaseTypeId = "release-type";
    inline static constexpr auto paramReleaseSyncId = "release.sync";
    inline static constexpr auto paramThresholdId = "threshold";
    inline static constexpr auto paramKneeId = "knee";
    inline static constexpr auto paramRetriggerId = "retrigger";
    inline static constexpr auto paramOneShotId = "one-shot";
    inline static constexpr auto paramLookaheadId = "lookahead";

    static constexpr size_t numRanges = ava::crossover::Splitter::numRanges;

    explicit TrsModuleProcessor(juce::AudioProcessor& ownerProcessor);
    ~TrsModuleProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState();
    void processBlock(juce::AudioBuffer<float>& buffer);

    juce::String getStateXmlString() const;
    bool setStateFromXmlString(const juce::String& stateXmlString);
    int getLatencySamples() const noexcept;
    RangeLatencies getRangeLatencies() const noexcept;
    size_t ensureRangeCount(size_t rangeCount);
    size_t getCreatedRangeCount() const noexcept;
    void processRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer);
    bool refreshLatencyState() noexcept;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;
    static juce::StringArray getHostSyncChoices();
    static int getDefaultHostSyncChoiceIndex() noexcept;
private:
    using ProcessorBank = ava::modules::SampleRangeBank<trs::dsp::DspCore>;

    struct RawRangeParameters
    {
        std::atomic<float>* transientMute = nullptr;
        std::atomic<float>* transientGain = nullptr;
        std::atomic<float>* sustainMute = nullptr;
        std::atomic<float>* sustainGain = nullptr;
        std::atomic<float>* hold = nullptr;
        std::atomic<float>* holdType = nullptr;
        std::atomic<float>* holdSync = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* releaseCurve = nullptr;
        std::atomic<float>* releaseType = nullptr;
        std::atomic<float>* releaseSync = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* knee = nullptr;
        std::atomic<float>* retrigger = nullptr;
        std::atomic<float>* oneShot = nullptr;
        std::atomic<float>* lookahead = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void cacheParameterPointers();
    trs::dsp::DspCore::Parameters readCrossoverRangeParameters(size_t rangeIndex, double hostBpm) const noexcept;
    void syncParameters();
    double getHostBpm() const noexcept;

    juce::AudioProcessor& ownerProcessor;
    ava::ModuleParameterHost moduleParameterHost;
    juce::AudioProcessorValueTreeState parameters;
    ProcessorBank processorBank;
    std::array<RawRangeParameters, numRanges> rawRangeParameters {};
    ProcessorBank::RangeParameters currentRangeParameters {};
    double currentSampleRate = 44100.0;
    int moduleLatencySamples = 0;
    int preparedBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrsModuleProcessor)
};
