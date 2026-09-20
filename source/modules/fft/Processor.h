#pragma once

#include <JuceHeader.h>
#include "../shared/ParameterHost.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

class FftModuleProcessor final : private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr std::size_t analyserScopeSize = 512;

    enum class CorrelationType
    {
        phase = 0,
        frequency,
        signedCorrelation
    };

    struct ProcessingSettings
    {
        int fftSize = 4096;
        int overlapFactor = 32;
        bool correlationMode = false;
        CorrelationType correlationType = CorrelationType::phase;
        bool upward = false;
        float detectorLowCutHz = 20.0f;
        float detectorHighCutHz = 20000.0f;
        float floorDb = -std::numeric_limits<float>::infinity();
        float leftThresholdDb = 0.0f;
        float rightThresholdDb = 0.0f;
        float correlationThreshold = 0.0f;
        float correlationSmoothing = 30.0f;
        float correlationAdaptiveAmount = 0.0f;
        float correlationSlopePerOctave = 0.0f;
        float correlationImpact = 0.0f;
        float leftAdaptiveAmount = 0.0f;
        float rightAdaptiveAmount = 0.0f;
        float adaptiveOffset = 0.0f;
        float adaptiveAttackMs = 30.0f;
        float adaptiveHoldMs = 0.0f;
        float adaptiveReleaseMs = 300.0f;
        float slopeDbPerOct = 4.5f;
        float attackMs = 0.0f;
        float releaseMs = 0.0f;
        float kneeDb = 0.0f;
        float ratio = 100.0f;
        float reductionDisplayTimeMs = 50.0f;
        float makeupDb = 0.0f;
        bool dynamicBypassed = false;
    };

    inline static constexpr auto paramTimeId = "fft_time";
    inline static constexpr auto paramSpectralReductionHighId = "fft_spectral_reduction_high";
    inline static constexpr auto paramSpectralReductionLowId = "fft_spectral_reduction_low";
    inline static constexpr auto paramCorrelationReductionHighId = "fft_correlation_reduction_high";
    inline static constexpr auto paramCorrelationReductionLowId = "fft_correlation_reduction_low";
    inline static constexpr auto paramDualMonoLeftThresholdId = "fft_dual_mono_left_threshold";
    inline static constexpr auto paramDualMonoRightThresholdId = "fft_dual_mono_right_threshold";
    inline static constexpr auto paramCorrelationThresholdId = "fft_correlation_threshold";
    inline static constexpr auto paramCorrelationSmoothingId = "fft_correlation_smoothing";
    inline static constexpr auto paramCorrelationAdaptiveId = "fft_correlation_adaptive";
    inline static constexpr auto paramCorrelationSlopeId = "fft_correlation_slope";
    inline static constexpr auto paramCorrelationImpactId = "fft_correlation_impact";
    inline static constexpr auto paramDualMonoLeftAdaptiveId = "fft_dual_mono_left_adaptive";
    inline static constexpr auto paramDualMonoRightAdaptiveId = "fft_dual_mono_right_adaptive";
    inline static constexpr auto paramSpectralAdaptiveOffsetId = "fft_spectral_adaptive_offset";
    inline static constexpr auto paramSpectralAdaptiveAttackId = "fft_spectral_adaptive_attack";
    inline static constexpr auto paramSpectralAdaptiveHoldId = "fft_spectral_adaptive_hold";
    inline static constexpr auto paramSpectralAdaptiveReleaseId = "fft_spectral_adaptive_release";
    inline static constexpr auto paramCorrelationAdaptiveOffsetId = "fft_correlation_adaptive_offset";
    inline static constexpr auto paramCorrelationAdaptiveAttackId = "fft_correlation_adaptive_attack";
    inline static constexpr auto paramCorrelationAdaptiveHoldId = "fft_correlation_adaptive_hold";
    inline static constexpr auto paramCorrelationAdaptiveReleaseId = "fft_correlation_adaptive_release";
    inline static constexpr auto paramDualMonoLinkId = "fft_dual_mono_link_lr";
    inline static constexpr auto paramDynamicBypassId = "fft_dynamic_bypass";
    inline static constexpr auto paramDynamicModeId = "fft_dynamic_mode";
    inline static constexpr auto paramCorrelationTypeId = "fft_correlation_type";
    inline static constexpr auto paramDynamicDirectionId = "fft_dynamic_direction";
    inline static constexpr auto paramDetectorLowCutId = "fft_detector_low_cut";
    inline static constexpr auto paramDetectorHighCutId = "fft_detector_high_cut";
    inline static constexpr auto paramFloorId = "fft_floor";
    inline static constexpr auto paramAttackId = "fft_attack";
    inline static constexpr auto paramReleaseId = "fft_release";
    inline static constexpr auto paramKneeId = "fft_knee";
    inline static constexpr auto paramRatioId = "fft_ratio";
    inline static constexpr auto paramDeltaId = "fft_delta";
    inline static constexpr auto paramDspFftSizeId = "fft_dsp_fft_size";
    inline static constexpr auto paramDspOverlapId = "fft_dsp_overlap";
    inline static constexpr auto paramDspSlopeId = "fft_dsp_slope";

    explicit FftModuleProcessor(juce::AudioProcessor& ownerProcessor);
    ~FftModuleProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void processBlock(juce::AudioBuffer<float>&);

    juce::String getStateXmlString() const;
    bool setStateFromXmlString(const juce::String& stateXmlString);

    void copyGainReductionData(std::array<float, analyserScopeSize>& leftDestination,
                               std::array<float, analyserScopeSize>& rightDestination) const;
    bool isCorrelationMode() const noexcept;
    bool isUpwardMode() const noexcept;
    void getReductionDisplayBounds(float& minimum, float& maximum) const noexcept;
    int getLatencySamples() const noexcept;
    bool refreshLatencyState() noexcept;
    float getAnalyserParameterValue(const juce::String& parameterId) const noexcept;
    void setAnalyserParameterValue(const juce::String& parameterId, float value);

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;

private:
    friend class FftProcessorBank;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static float thresholdToCorrelation(float threshold, bool upward, CorrelationType type) noexcept;
    static float calculateReduction(float detectorValue, float threshold, float ratio, float knee) noexcept;
    static float calculateGainChange(float detectorValue, float threshold, float ratio, float knee, bool upward) noexcept;
    static float calculateTimeCoefficient(float timeMs, float frameDurationSeconds) noexcept;
    void parameterChanged(const juce::String& parameterID, float newValue) override;

    ProcessingSettings getProcessingSettings() const noexcept;
    bool isDeltaEnabled() const noexcept;
    void resetDeltaDelay() noexcept;
    void ensureDeltaDryBufferSize(int channels, int samples);
    void populateAlignedDryBuffer(const juce::AudioBuffer<float>& inputBuffer,
                                  juce::AudioBuffer<float>& delayedDryBuffer,
                                  int channelsToUse,
                                  int latencySamples) noexcept;
    int getSelectedDspFftSize() const noexcept;
    int getSelectedDspOverlapFactor() const noexcept;
    float getSelectedAveragingTimeMs() const noexcept;
    juce::ValueTree createAnalyserStateSnapshot() const;
    bool isCurrentState(const juce::ValueTree& state) const noexcept;
    void setDualMonoLinkListenersEnabled(bool enabled);

    void resetAnalyserState();
    void applyAnalyserState(juce::ValueTree state);

    juce::AudioProcessor& ownerProcessor;
    ava::ModuleParameterHost moduleParameterHost;

    class DynamicProcessor
    {
    public:
        static constexpr auto maxFftOrder = 14;
        static constexpr auto maxFftSize = 1 << maxFftOrder;
        static constexpr auto maxChannels = 2;
        static constexpr auto maxQueueSize = maxFftSize * 2;

        DynamicProcessor();

        void prepare(double newSampleRate, int numChannels);
        void reset() noexcept;
        void processBuffer(juce::AudioBuffer<float>& buffer, int numInputChannels, const ProcessingSettings& settings);
        void copyReductionScope(std::array<float, analyserScopeSize>& leftDestination,
                                std::array<float, analyserScopeSize>& rightDestination) const;
        bool hasProducedOutput() const noexcept { return outputPrimed; }

    private:
        struct ChannelState
        {
            std::array<float, maxFftSize> analysisFifo {};
            std::array<float, maxFftSize> outputAccum {};
            std::array<float, maxFftSize> normalizationAccum {};
            std::array<float, maxQueueSize> readyOutput {};
            std::array<juce::dsp::Complex<float>, maxFftSize> frequencyData {};
            int readyOutputRead = 0;
            int readyOutputWrite = 0;
            int readyOutputCount = 0;
            int analysisFilled = 0;
        };

        void enqueueOutputSample(ChannelState& state, float sample) noexcept;
        float dequeueOutputSample(ChannelState& state) noexcept;
        void processFrame(int channelsToUse,
                          const ProcessingSettings& settings,
                          int fftIndex,
                          int fftSize,
                          int hopSize) noexcept;
        std::array<float, maxChannels> calculatePublishedThresholds(int channelsToUse,
                                                                         const ProcessingSettings& settings) const noexcept;
        void prepareFrameSpectrum(int channelsToUse, int fftIndex, int fftSize) noexcept;
        std::array<float, maxChannels> measureDetectorLevelsForBin(
            int channelsToUse,
            int bin,
            int fftSize,
            float detectorRangeMagnitude,
            std::array<double, maxChannels>& accumulatedDetectorPower) const noexcept;
        void processDualMonoSpectrum(int channelsToUse,
                                     const ProcessingSettings& settings,
                                     int fftSize,
                                     float attackCoefficient,
                                     float releaseCoefficient,
                                     float makeupGain,
                                     const std::array<float, maxChannels>& publishedThreshold,
                                     std::array<double, maxChannels>& accumulatedDetectorPower) noexcept;
        float calculateDesiredCorrelationChange(const ProcessingSettings& settings,
                                                int bin,
                                                float detectorRangeMagnitude,
                                                float minimumLevelDb,
                                                float octavesAboveMin,
                                                float publishedThreshold,
                                                float& threshold) const noexcept;
        void processCorrelationSpectrum(int channelsToUse,
                                        const ProcessingSettings& settings,
                                        int fftSize,
                                        float attackCoefficient,
                                        float releaseCoefficient,
                                        const std::array<float, maxChannels>& publishedThreshold,
                                        std::array<double, maxChannels>& accumulatedDetectorPower,
                                        double& accumulatedCorrelation,
                                        double& accumulatedCorrelationWeight) noexcept;
        void updateAdaptiveReferences(int channelsToUse,
                                      const ProcessingSettings& settings,
                                      int fftSize,
                                      int hopSize,
                                      const std::array<double, maxChannels>& accumulatedDetectorPower,
                                      double accumulatedCorrelation,
                                      double accumulatedCorrelationWeight) noexcept;
        void applyFrequencyConsistencyCompensation(int channelsToUse,
                                                   const ProcessingSettings& settings,
                                                   int fftIndex,
                                                   int fftSize) noexcept;
        void synthesiseFrame(int channelsToUse, int fftIndex, int fftSize) noexcept;
        void publishReductionScope(const ProcessingSettings& settings, int fftSize, int hopSize) noexcept;
        void pushOutputChunk(ChannelState& state, int fftSize, int hopSize) noexcept;
        void reconfigure(int channelsToUse, int fftSize, int hopSize) noexcept;
        int getFftIndexForSize(int fftSize) const noexcept;
        void updateDetectorRange(const ProcessingSettings& settings, int fftSize) noexcept;
        void updateCorrelationDetector(const ProcessingSettings& settings,
                                       int fftSize) noexcept;
        std::array<std::unique_ptr<juce::dsp::FFT>, 5> ffts;
        std::array<std::array<float, maxFftSize>, 5> windowTables {};
        std::array<ChannelState, maxChannels> channelStates {};
        std::array<std::array<float, maxFftSize>, maxChannels> hopBuffers {};
        std::array<std::array<float, (maxFftSize / 2) + 1>, maxChannels> dualMonoSmoothedReductionDb {};
        std::array<std::array<float, (maxFftSize / 2) + 1>, maxChannels> phaseSmoothedReductionRadians {};
        std::array<float, (maxFftSize / 2) + 1> frequencySmoothedBalanceRotation {};
        std::array<float, (maxFftSize / 2) + 1> signedSmoothedMidSideRotation {};
        std::array<std::array<juce::dsp::Complex<float>, maxFftSize>, maxChannels> frequencyConsistencyScratch {};
        std::array<std::array<float, (maxFftSize / 2) + 1>, maxChannels> correlationChanges {};
        std::array<float, (maxFftSize / 2) + 1> correlationDetector {};
        std::array<float, (maxFftSize / 2) + 1> detectedCorrelation {};
        std::array<float, analyserScopeSize> correlationColumnSums {};
        std::array<float, analyserScopeSize> smoothedCorrelationColumns {};
        std::array<int, analyserScopeSize> correlationColumnCounts {};
        std::array<float, (maxFftSize / 2) + 1> detectorRangeMagnitudes {};
        float currentDetectorLowCutHz = -1.0f;
        float currentDetectorHighCutHz = -1.0f;
        std::array<std::array<std::array<float, analyserScopeSize>, maxChannels>, 2> reductionScopeBuffers {};
        std::array<std::array<float, analyserScopeSize>, maxChannels> smoothedReductionScopes {};
        std::atomic<int> activeReductionScopeBuffer { 0 };
        double sampleRate = 44100.0;
        std::array<float, maxChannels> dualMonoAdaptiveReferenceDb {};
        std::array<float, maxChannels> correlationAdaptiveReference { -1.0f, -1.0f };
        std::array<float, maxChannels> dualMonoAdaptiveHoldRemainingMs {};
        std::array<float, maxChannels> correlationAdaptiveHoldRemainingMs {};
        int configuredChannels = 0;
        int currentFftSize = 0;
        int currentHopSize = 0;
        int hopFill = 0;
        bool outputPrimed = false;
        bool lastWasCorrelationMode = false;
        CorrelationType lastCorrelationType = CorrelationType::phase;
    };

    enum class WindowTransitionStage
    {
        none,
        priming,
        fadeOut,
        fadeIn
    };

    void beginWindowTransition(int fftSize, int overlapFactor) noexcept;
    void applyWindowTransition(juce::AudioBuffer<float>& activeOutput,
                               const juce::AudioBuffer<float>* pendingOutput,
                               int channelsToUse) noexcept;
    void completeWindowTransition() noexcept;

    juce::AudioProcessorValueTreeState parameters;
    juce::ValueTree analyserState { "fft_analyser_state" };
    std::atomic<float> analyserTimeValue { 50.0f };
    std::atomic<float> spectralReductionHighValue { 0.0f };
    std::atomic<float> spectralReductionLowValue { -36.0f };
    std::atomic<float> correlationReductionHighValue { 2.2f };
    std::atomic<float> correlationReductionLowValue { 0.0f };
    std::atomic<float>* dualMonoLeftThresholdParam = nullptr;
    std::atomic<float>* dualMonoRightThresholdParam = nullptr;
    std::atomic<float>* correlationThresholdParam = nullptr;
    std::atomic<float>* correlationSmoothingParam = nullptr;
    std::atomic<float>* correlationAdaptiveParam = nullptr;
    std::atomic<float>* correlationSlopeParam = nullptr;
    std::atomic<float>* correlationImpactParam = nullptr;
    std::atomic<float>* dualMonoLeftAdaptiveParam = nullptr;
    std::atomic<float>* dualMonoRightAdaptiveParam = nullptr;
    std::atomic<float>* spectralAdaptiveOffsetParam = nullptr;
    std::atomic<float>* spectralAdaptiveAttackParam = nullptr;
    std::atomic<float>* spectralAdaptiveHoldParam = nullptr;
    std::atomic<float>* spectralAdaptiveReleaseParam = nullptr;
    std::atomic<float>* correlationAdaptiveOffsetParam = nullptr;
    std::atomic<float>* correlationAdaptiveAttackParam = nullptr;
    std::atomic<float>* correlationAdaptiveHoldParam = nullptr;
    std::atomic<float>* correlationAdaptiveReleaseParam = nullptr;
    std::atomic<float>* dualMonoLinkParam = nullptr;
    std::atomic<float>* dynamicBypassParam = nullptr;
    std::atomic<float>* dynamicModeParam = nullptr;
    std::atomic<float>* correlationTypeParam = nullptr;
    std::atomic<float>* dynamicDirectionParam = nullptr;
    std::atomic<float>* detectorLowCutParam = nullptr;
    std::atomic<float>* detectorHighCutParam = nullptr;
    std::atomic<float>* floorParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* kneeParam = nullptr;
    std::atomic<float>* ratioParam = nullptr;
    std::atomic<float>* deltaParam = nullptr;
    std::atomic<float>* dspFftSizeParam = nullptr;
    std::atomic<float>* dspOverlapParam = nullptr;
    std::atomic<float>* dspSlopeParam = nullptr;
    std::atomic<bool> linkedDualMonoPropagationInProgress { false };
    std::array<DynamicProcessor, 2> dynamicProcessors;
    juce::AudioBuffer<float> windowTransitionBuffer;
    static constexpr int deltaDelayBufferSize = DynamicProcessor::maxFftSize + 1;
    std::array<std::array<float, deltaDelayBufferSize>, DynamicProcessor::maxChannels> deltaDelayBuffers {};
    juce::AudioBuffer<float> deltaDryBuffer;
    int preparedBlockSize = 0;
    int deltaDelayWriteIndex = 0;
    int activeLatencySamples = 0;
    int activeDynamicProcessorIndex = 0;
    int activeFftSize = 4096;
    int activeOverlapFactor = 32;
    int pendingFftSize = 0;
    int pendingOverlapFactor = 0;
    int windowTransitionSamples = 1;
    int windowTransitionSamplesRemaining = 0;
    WindowTransitionStage windowTransitionStage = WindowTransitionStage::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FftModuleProcessor)
};
