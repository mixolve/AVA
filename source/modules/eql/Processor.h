#pragma once

#include <JuceHeader.h>
#include "../ParameterHost.h"

#include <array>
#include <atomic>
#include <complex>
#include <memory>
#include <vector>

class EqlModuleProcessor final : private juce::AudioProcessorValueTreeState::Listener
{
public:
    enum class FilterType
    {
        lowCut,
        lowShelf,
        bell,
        tilt,
        highShelf,
        highCut,
        volume,
    };

    inline static constexpr auto activeFilterCountStateKey = "active_filter_count";
    inline static constexpr float fixedSlopeDbPerOct = 12.0f;
    inline static constexpr auto filterPresetLastSelectedStateKey = "filter_preset_last_selected";
    inline static constexpr auto filterPresetDefaultSelectedStateKey = "filter_preset_default_selected";
    static constexpr int maxFilterCount = 64;

    static juce::String getFilterTypeParamId(int filterIndex);
    static juce::String getFilterPlaceParamId(int filterIndex);
    static juce::String getFilterFrequencyParamId(int filterIndex);
    static juce::String getFilterBandwidthParamId(int filterIndex);
    static juce::String getFilterSlopeParamId(int filterIndex);
    static juce::String getFilterGainParamId(int filterIndex);
    static juce::String getFilterBypassParamId(int filterIndex);
    static juce::StringArray getBellSlopeChoices() noexcept;
    static float getBellSlopeValueForChoiceIndex(int choiceIndex) noexcept;
    static int getBellSlopeChoiceIndexForValue(float slope) noexcept;
    static juce::String getFilterHeaderText(FilterType type, int filterIndex);
    juce::String getFilterHeaderText(int filterIndex, int displayIndex) const noexcept;
    static FilterType filterTypeFromChoiceIndex(int choiceIndex) noexcept;
    static int choiceIndexFromFilterType(FilterType type) noexcept;
    static void appendEqlParameters(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& parameterLayout);

    EqlModuleProcessor();
    ~EqlModuleProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void processBlock(juce::AudioBuffer<float>& buffer);
    int getLatencySamples() const noexcept;

    void getStateInformation(juce::MemoryBlock& destData);
    void setStateInformation(const void* data, int sizeInBytes);
    bool applyStateInformationForABCompare(const void* data, int sizeInBytes);

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;

    int getActiveFilterCount() const noexcept;
    bool addFilter() noexcept;
    bool removeFilter(int filterIndex) noexcept;
    bool clearFilters() noexcept;
    bool applyFilterOrder(const std::vector<int>& orderedFilterIndices) noexcept;
    juce::String getDefaultFilterPresetName() const;
    juce::String getLastFilterPresetName() const;
    juce::StringArray getFilterPresetNames() const;
    bool saveFilterPreset(const juce::String& presetName);
    bool renameFilterPreset(const juce::String& sourcePresetName, const juce::String& newPresetName);
    bool setDefaultFilterPreset(const juce::String& presetName);
    bool loadInitialFilterPreset() noexcept;
    bool loadFilterPreset(const juce::String& presetName) noexcept;
    bool deleteFilterPreset(const juce::String& presetName);
    void markEqlFiltersDirty() noexcept;

private:
    static constexpr size_t maxSupportedChannels = 2;
    static constexpr size_t maxBellOrder = 128;
    static constexpr size_t maxShelfOrder = 128;
    static constexpr size_t maxBellFourthOrderSections = maxBellOrder;
    static constexpr int phaseFirOrder = 13;
    static constexpr int phaseFirSize = 1 << phaseFirOrder;
    static constexpr int phaseFirLatencySamples = phaseFirSize / 2;
    static constexpr double coefficientTransitionSeconds = 0.01;

    struct SecondOrderSection
    {
        void reset() noexcept;
        void setIdentity() noexcept;
        void transitionTo(const SecondOrderSection& target, int transitionSamples) noexcept;
        bool isTransitioning() const noexcept;
        void process(juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

        std::array<double, 3> b { 1.0, 0.0, 0.0 };
        std::array<double, 2> a { 0.0, 0.0 };
        std::array<double, 3> targetB { 1.0, 0.0, 0.0 };
        std::array<double, 2> targetA { 0.0, 0.0 };
        std::array<double, 3> coefficientBStep {};
        std::array<double, 2> coefficientAStep {};
        std::array<std::array<double, 2>, maxSupportedChannels> state {};
        int transitionSamplesRemaining = 0;
    };

    struct FourthOrderSection
    {
        void reset() noexcept;
        void setIdentity() noexcept;
        void process(juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

        std::array<double, 5> b { 1.0, 0.0, 0.0, 0.0, 0.0 };
        std::array<double, 4> a { 0.0, 0.0, 0.0, 0.0 };
        std::array<std::array<double, 4>, maxSupportedChannels> state {};
    };

    struct BellOrderFilter
    {
        void reset() noexcept;
        void setIdentity() noexcept;
        void process(juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

        int sectionCount = 0;
        std::array<FourthOrderSection, maxBellFourthOrderSections> sections;
    };

    struct BiquadCascade
    {
        void reset() noexcept;
        void setIdentity() noexcept;
        void transitionTo(const BiquadCascade& target, int transitionSamples) noexcept;
        void process(juce::AudioBuffer<float>& buffer, int numChannels) noexcept;

        int stageCount = 0;
        int targetStageCount = 0;
        std::array<SecondOrderSection, maxShelfOrder> sections;
    };

    struct PhaseFirFilter
    {
        void reset() noexcept;
        void setIdentity() noexcept;
        void process(juce::AudioBuffer<float>& buffer, int numChannels) noexcept;
        void processWithChannelMask(juce::AudioBuffer<float>& buffer, int numChannels, bool processLeft, bool processRight) noexcept;

        bool active = false;
        std::array<float, phaseFirSize> taps {};
        std::array<std::array<float, phaseFirSize * 2>, maxSupportedChannels> state {};
        int writeIndex = 0;
    };

    struct FilterDesignState
    {
        bool valid = false;
        bool active = false;
        FilterType type = FilterType::bell;
        float frequency = 0.0f;
        float bandwidth = 0.0f;
        float slope = 0.0f;
        float gainDb = 0.0f;
        double sampleRate = 0.0;
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void setParameterListenersEnabled(bool enabled);
    bool updateSmoothedFilterGains(int samplesPerBlock) noexcept;
    FilterType getFilterTypeForSection(size_t filterIndex) const noexcept;
    bool filterDesignMatches(size_t filterIndex,
                             bool active,
                             FilterType type,
                             float frequency,
                             float bandwidth,
                             float slope,
                             float gainDb) const noexcept;
    void storeFilterDesignState(size_t filterIndex,
                                bool active,
                                FilterType type,
                                float frequency,
                                float bandwidth,
                                float slope,
                                float gainDb) noexcept;
    void setActiveFilterCount(int newCount) noexcept;
    void resetFilters() noexcept;
    void setBellIdentityResponse(size_t filterIndex) noexcept;
    void setShelfIdentityResponse(size_t filterIndex) noexcept;
    void setCutIdentityResponse(size_t filterIndex) noexcept;
    void setTiltIdentityResponse(size_t filterIndex) noexcept;
    void setPhaseIdentityResponse(size_t filterIndex) noexcept;
    void updateBellOrderFilter(BellOrderFilter& filter, int order, double frequency, double octaveBandwidth, double gain) noexcept;
    void updateShelfOrderFilterRaw(BiquadCascade& filter, FilterType filterType, int order, double frequency, double octaveBandwidth, double gain) noexcept;
    void updateCutOrderFilterRaw(BiquadCascade& filter, FilterType filterType, int order, double frequency) noexcept;
    void updateTiltFilter(BiquadCascade& filter, double frequency, double gainDb) noexcept;
    void updatePhaseFirFilter(PhaseFirFilter& target,
                              FilterType filterType,
                              double frequency,
                              double octaveBandwidth,
                              double slope,
                              double gainDb) noexcept;
    std::complex<double> evaluateCascadeResponseAt(const BiquadCascade& filter, double frequency) const noexcept;
    double evaluateCascadeMagnitudeAt(const BiquadCascade& filter, double frequency) const noexcept;
    void updateInterpolatedCascadeFilter(BiquadCascade& target,
                                         const BiquadCascade& lower,
                                         const BiquadCascade& upper,
                                         double blend) noexcept;
    void buildCutBlendFilter(BiquadCascade& target,
                             FilterType filterType,
                             double frequency,
                             double slope) noexcept;
    void rebuildCutBlendFilter(size_t filterIndex,
                               FilterType filterType,
                               double frequency,
                               double slope) noexcept;
    void updateFilters();

    ava::ParameterHost parameterHost;
    juce::AudioProcessorValueTreeState parameters;
    mutable juce::CriticalSection filterProcessLock;
    std::array<std::atomic<float>*, maxFilterCount> filterTypeParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterPlaceParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterFrequencyParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterBandwidthParams {};
    std::array<juce::AudioParameterChoice*, maxFilterCount> filterSlopeChoiceParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterGainParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterBypassParams {};
    std::array<juce::SmoothedValue<float>, maxFilterCount> filterGainSmoothers {};
    std::array<float, maxFilterCount> effectiveFilterGainDb {};
    std::array<std::array<BellOrderFilter, maxBellOrder>, maxFilterCount> bellOrderFilters;
    std::array<std::array<BiquadCascade, maxShelfOrder>, maxFilterCount> shelfOrderFilters;
    std::array<BiquadCascade, maxFilterCount> tiltFilters;
    std::array<BiquadCascade, maxFilterCount> cutBlendFilters;
    std::array<PhaseFirFilter, maxFilterCount> phaseFirFilters;
    juce::dsp::FFT phaseFirFft { phaseFirOrder };
    std::array<FilterDesignState, maxFilterCount> cachedFilterStates {};
    juce::AudioBuffer<float> filterProcessBufferA;
    juce::AudioBuffer<float> filterProcessBufferB;
    juce::AudioBuffer<float> placeWorkBuffer;
    juce::AudioBuffer<float> placeAuxBuffer;
    int preparedNumChannels = 2;
    int lastProcessedBlockSize = 0;
    double currentSampleRate = 0.0;
    std::atomic<int> activeFilterCount { 0 };
    std::atomic<bool> eqlFiltersDirty { true };
    std::atomic<bool> suppressEqlFilterDirty { false };
    std::atomic<bool> prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqlModuleProcessor)
};
