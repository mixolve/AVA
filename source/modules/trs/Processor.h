#pragma once

#include <JuceHeader.h>

#include "../SampleRangeBank.h"
#include "DspCore.h"
#include "../ParameterHost.h"

#include <array>
#include <atomic>

class TrsModuleProcessor final
{
public:
    using RangeLatencies = ava::modules::SampleRangeBank<trs::dsp::DspCore>::RangeLatencies;

    inline static constexpr auto paramTransOnId = "trans_on";
    inline static constexpr auto paramTransGainId = "trans_gain";
    inline static constexpr auto paramSusOnId = "sus_on";
    inline static constexpr auto paramSusGainId = "sus_gain";
    inline static constexpr auto paramTimeHoldId = "hold";
    inline static constexpr auto paramTimeHoldModeId = "hold_mode";
    inline static constexpr auto paramTimeHoldSyncId = "hold_sync";
    inline static constexpr auto paramTimeReleaseId = "release";
    inline static constexpr auto paramTimeReleaseCurveId = "rel_curve";
    inline static constexpr auto paramTimeReleaseModeId = "release_mode";
    inline static constexpr auto paramTimeReleaseSyncId = "release_sync";
    inline static constexpr auto paramSensThresholdId = "sens_thresh";
    inline static constexpr auto paramSensKneeId = "sens_knee";
    inline static constexpr auto paramSensRetriggerId = "sens_retr";
    inline static constexpr auto paramSensOneShotId = "sens_one_shot";
    inline static constexpr auto paramLookaheadId = "lookahead";

    static constexpr size_t numRanges = ava::crossover::Splitter::numRanges;

    explicit TrsModuleProcessor(juce::AudioProcessor& ownerProcessor);
    ~TrsModuleProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState();
    void processBlock(juce::AudioBuffer<float>& buffer);

    juce::String getStateXmlString() const;
    void setStateFromXmlString(const juce::String& stateXmlString);
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
        std::atomic<float>* transOn = nullptr;
        std::atomic<float>* transGain = nullptr;
        std::atomic<float>* sustainOn = nullptr;
        std::atomic<float>* sustainGain = nullptr;
        std::atomic<float>* hold = nullptr;
        std::atomic<float>* holdMode = nullptr;
        std::atomic<float>* holdSync = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* releaseCurve = nullptr;
        std::atomic<float>* releaseMode = nullptr;
        std::atomic<float>* releaseSync = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* knee = nullptr;
        std::atomic<float>* retrigger = nullptr;
        std::atomic<float>* oneShot = nullptr;
        std::atomic<float>* lookahead = nullptr;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void cacheParameterPointers();
    trs::dsp::DspCore::Parameters readCrossoverRangeParameters(size_t rangeIndex) const noexcept;
    void syncParameters();
    double getHostBpm() const noexcept;

    juce::AudioProcessor& ownerProcessor;
    ava::ParameterHost parameterHost;
    juce::AudioProcessorValueTreeState parameters;
    ProcessorBank processorBank;
    std::array<RawRangeParameters, numRanges> rawRangeParameters {};
    ProcessorBank::RangeParameters currentRangeParameters {};
    double currentSampleRate = 44100.0;
    int moduleLatencySamples = 0;
    int preparedBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrsModuleProcessor)
};
