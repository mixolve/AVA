#pragma once

#include "CorrelationEnvelope.h"
#include "PeakCrest.h"
#include "ThresholdHysteresis.h"

#include <JuceHeader.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <limits>
#include <memory>
#include <utility>

class DynSpectralProcessor final
{
  public:
    static constexpr std::size_t correlationColumnCount = 512;

    struct ProcessingSettings
    {
        int fftSize = 4096;
        int overlapFactor = 32;
        bool correlationMode = false;
        float thresholdDb = 0.0f, thresholdHysteresisDb = 0.0f;
        float correlationThreshold = 0.0f, correlationThresholdHysteresis = 0.0f;
        float correlationKnee = 0.0f;
        float correlationSmoothing = 30.0f;
        float correlationSlopePerOctave = 0.0f;
        float correlationImpact = 0.0f;
        float adaptiveAmount = 0.0f;
        float adaptiveOffset = 0.0f;
        float adaptiveAttackMs = 30.0f;
        float adaptiveHoldMs = 0.0f;
        float adaptiveReleaseMs = 300.0f;
        float slopeDbPerOct = 4.5f;
        float attackMs = 0.0f;
        float autoRelease = 0.0f, releaseMemoryMs = 200.0f, releaseDepth = 0.0f;
        float releasePeakMs = 0.0f, releaseRmsMs = 150.0f;
        float kneeDb = 0.0f;
        float ratio = 100.0f;
        float progression = 0.0f, progressionSpan = 12.0f;
        float makeupDb = 0.0f;
        float inputDb = 0.0f, outputDb = 0.0f, mix = 1.0f;
        float rangeDb = 96.0f, rangeCurve = 0.0f;
        float delayMs = 0.0f, holdMs = 0.0f, attackCurve = 0.0f, releaseCurve = 0.0f;
        float adaptiveTimeMs = 0.0f;
        float releaseType = 0.0f, tension = 0.0f, tensionFloorDb = -96.0f;
        float correlationTensionFloor = -1.0f, tensionHysteresis = 0.0f;
        bool useSidechainInput = false, sidechainListen = false, separateDetector = false;
        float rmsMix = 0.0f;
        float lookaheadMs = 0.0f, peakCrestDb = 0.0f, correlationPeakCrest = 0.0f, rmsTimeMs = 50.0f;
    };

    explicit DynSpectralProcessor(juce::AudioProcessor& ownerProcessor);
    ~DynSpectralProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void processBlock(juce::AudioBuffer<float>&, ProcessingSettings settings, bool delta,
                      const juce::AudioBuffer<float>* sidechain = nullptr, int sidechainOffset = 0);

    int getLatencySamples(int lookaheadSamples = 0) const noexcept;
    bool refreshLatencyState() noexcept;

  private:
    static float calculateReduction(float detectorValue, float threshold, float ratio, float knee, float progression = 0.0f, float span = 12.0f) noexcept;
    static float calculateTimeCoefficient(float timeMs, float frameDurationSeconds) noexcept;

    void resetDeltaDelay() noexcept;
    void ensureDeltaDryBufferSize(int channels, int samples);
    void populateAlignedDryBuffer(const juce::AudioBuffer<float>& inputBuffer,
                                  juce::AudioBuffer<float>& delayedDryBuffer, int channelsToUse,
                                  int latencySamples) noexcept;
    void delayMainForLookahead(juce::AudioBuffer<float>& buffer, int channels, int lookaheadSamples) noexcept;
    void populateAlignedSidechainBuffer(const juce::AudioBuffer<float>* sidechain, int offset, int channels,
                                        int samples, int latencySamples) noexcept;

    juce::AudioProcessor& ownerProcessor;

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
        void processBuffer(juce::AudioBuffer<float>& buffer, int numInputChannels, const ProcessingSettings& settings,
                           const juce::AudioBuffer<float>* sidechain = nullptr, int sidechainOffset = 0);
        bool hasProducedOutput() const noexcept { return outputPrimed; }

      private:
        struct ChannelState
        {
            std::array<float, maxFftSize> analysisFifo{};
            std::array<float, maxFftSize> outputAccum{};
            std::array<float, maxFftSize> normalizationAccum{};
            std::array<float, maxQueueSize> readyOutput{};
            std::array<juce::dsp::Complex<float>, maxFftSize> frequencyData{};
            int readyOutputRead = 0;
            int readyOutputWrite = 0;
            int readyOutputCount = 0;
            int analysisFilled = 0;
        };

        void enqueueOutputSample(ChannelState& state, float sample) noexcept;
        float dequeueOutputSample(ChannelState& state) noexcept;
        void processFrame(int channelsToUse, const ProcessingSettings& settings, int fftIndex, int fftSize,
                          int hopSize) noexcept;
        float calculatePublishedThreshold(const ProcessingSettings& settings) const noexcept;
        void prepareFrameSpectrum(int channelsToUse, int fftIndex, int fftSize) noexcept;
        void prepareSidechainSpectrum(int channelsToUse, int fftIndex, int fftSize) noexcept;
        std::array<float, maxChannels>
        measureDetectorLevelsForBin(int channelsToUse, int bin, int fftSize,

                                    std::array<double, maxChannels>& accumulatedDetectorPower,
                                    bool useSidechainInput = false) const noexcept;
        void processLinkedSpectrum(int channelsToUse, const ProcessingSettings& settings, int fftSize,
                                   float publishedThreshold,
                                   std::array<double, maxChannels>& accumulatedDetectorPower) noexcept;
        void processCorrelationSpectrum(int channelsToUse, const ProcessingSettings& settings, int fftSize,
                                        float publishedThreshold,
                                        std::array<double, maxChannels>& accumulatedDetectorPower,
                                        double& accumulatedCorrelation, double& accumulatedCorrelationWeight) noexcept;
        void updateAdaptiveReferences(int channelsToUse, const ProcessingSettings& settings, int fftSize, int hopSize,
                                      const std::array<double, maxChannels>& accumulatedDetectorPower,
                                      double accumulatedCorrelation, double accumulatedCorrelationWeight) noexcept;
        void synthesiseFrame(int channelsToUse, int fftIndex, int fftSize) noexcept;
        void pushOutputChunk(ChannelState& state, int fftSize, int hopSize) noexcept;
        void reconfigure(int channelsToUse, int fftSize, int hopSize) noexcept;
        int getFftIndexForSize(int fftSize) const noexcept;
        void updateCorrelationDetector(const ProcessingSettings& settings, int fftSize) noexcept;
        std::array<std::unique_ptr<juce::dsp::FFT>, 5> ffts;
        std::array<std::array<float, maxFftSize>, 5> windowTables{};
        std::array<ChannelState, maxChannels> channelStates{};
        std::array<std::array<float, maxFftSize>, maxChannels> hopBuffers{};
        std::array<std::array<float, maxFftSize>, maxChannels> sidechainHopBuffers{};
        std::array<std::array<float, maxFftSize>, maxChannels> sidechainAnalysisFifo{};
        std::array<std::array<juce::dsp::Complex<float>, maxFftSize>, maxChannels> sidechainFrequencyData{};
        std::array<float, (maxFftSize / 2) + 1> correlationDetector{};
        std::array<float, (maxFftSize / 2) + 1> detectedCorrelation{};
        std::array<float, (maxFftSize / 2) + 1> correlationRmsDetector{}, detectedRmsCorrelation{};
        std::array<float, correlationColumnCount> correlationColumnSums{};
        std::array<float, correlationColumnCount> smoothedCorrelationColumns{};
        std::array<int, correlationColumnCount> correlationColumnCounts{};
        struct SpectralEnvelope
        {
            dyn::detector::ReleaseMemory releaseMemory;
            float releaseDurationMs = 0.0f;
            float value = 0.0f, start = 0.0f, target = 0.0f, elapsedMs = 0.0f;
            float delayMs = 0.0f, holdMs = 0.0f;
            bool attacking = false;
        };
        std::array<std::array<SpectralEnvelope, (maxFftSize / 2) + 1>, dyn::detector::count> spectralEnvelopes{};
        std::array<std::array<dyn_spectral_envelope::Envelope, (maxFftSize / 2) + 1>, dyn::detector::count> correlationEnvelopes{};
        std::array<float, (maxFftSize / 2) + 1> correlationRmsCrossPower{}, correlationRmsPower{};
        double correlationAdaptiveCrossPower = 0.0, correlationAdaptivePower = 0.0;
        std::array<std::array<dyn_spectral_envelope::Envelope, (maxFftSize / 2) + 1>, dyn::detector::count> tensionEnvelopes{};
        std::array<std::array<float, (maxFftSize / 2) + 1>, dyn::detector::count> tensionDirections{}, correlationResponseDistances{}, correlationActionDistances{};
        std::array<std::array<dyn::detector::ThresholdHysteresis, (maxFftSize / 2) + 1>, dyn::detector::count> thresholdHysteresis{}, tensionThresholdHysteresis{};
        float smoothedTension = 0.0f;
        float spectralAdaptivePower = 0.0f;
        std::array<float, (maxFftSize / 2) + 1> spectralRmsPower{};
        double sampleRate = 44100.0;
        float linkedAdaptiveReferenceDb = 0.0f;
        float correlationAdaptiveReference = -1.0f;
        float linkedAdaptiveHoldRemainingMs = 0.0f;
        float correlationAdaptiveHoldRemainingMs = 0.0f;
        int configuredChannels = 0;
        int currentFftSize = 0;
        int currentHopSize = 0;
        int hopFill = 0;
        bool outputPrimed = false;
        bool lastWasCorrelationMode = false;
    };

    enum class WindowTransitionStage
    {
        none,
        priming,
        fadeOut,
        fadeIn
    };

    void beginWindowTransition(int fftSize, int overlapFactor) noexcept;
    void applyWindowTransition(juce::AudioBuffer<float>& activeOutput, const juce::AudioBuffer<float>* pendingOutput,
                               int channelsToUse) noexcept;
    void completeWindowTransition() noexcept;

    std::array<DynamicProcessor, 2> dynamicProcessors;
    juce::AudioBuffer<float> windowTransitionBuffer;
    int deltaDelayBufferSize = DynamicProcessor::maxFftSize + 1;
    std::array<std::vector<float>, DynamicProcessor::maxChannels> deltaDelayBuffers;
    juce::AudioBuffer<float> deltaDryBuffer;
    std::array<std::vector<float>, DynamicProcessor::maxChannels> sidechainDelayBuffers;
    std::array<std::vector<float>, DynamicProcessor::maxChannels> lookaheadDelayBuffers;
    juce::AudioBuffer<float> internalDetectorBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> lookaheadDelay;
    int lookaheadWriteIndex = 0;
    int maxLookaheadSamples = 0;
    juce::AudioBuffer<float> sidechainListenBuffer;
    int sidechainDelayWriteIndex = 0;
    int preparedBlockSize = 0;
    int deltaDelayWriteIndex = 0;
    int activeLatencySamples = 0;
    std::atomic<int> activeDynamicProcessorIndex{0};
    int activeFftSize = 4096;
    int activeOverlapFactor = 32;
    int pendingFftSize = 0;
    int pendingOverlapFactor = 0;
    int windowTransitionSamples = 1;
    int windowTransitionSamplesRemaining = 0;
    WindowTransitionStage windowTransitionStage = WindowTransitionStage::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DynSpectralProcessor)
};
