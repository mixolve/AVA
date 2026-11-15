#pragma once

#include <JuceHeader.h>
#include "../shared/ParameterHost.h"

#include <array>
#include <atomic>
#include <complex>
#include <memory>
#include <optional>
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
    inline static constexpr auto filterPresetSelectedStateKey = "filter_preset_selected";
    static constexpr int maxFilterCount = 64;

    static juce::String getFilterTypeParamId(int filterIndex);
    static juce::String getFilterPhaseParamId(int filterIndex);
    static juce::String getFilterFrequencyParamId(int filterIndex);
    static juce::String getFilterBandwidthParamId(int filterIndex);
    static juce::String getFilterOrderParamId(int filterIndex);
    static juce::String getFilterGainParamId(int filterIndex);
    static juce::String getFilterBypassParamId(int filterIndex);
    static juce::StringArray getFilterOrderChoices() noexcept;
    static float getSlopeDbPerOctForOrderChoice(int choiceIndex) noexcept;
    static int getOrderChoiceForSlopeDbPerOct(float slope) noexcept;
    juce::String getFilterHeaderText(int filterIndex) const noexcept;
    static FilterType filterTypeFromChoiceIndex(int choiceIndex) noexcept;
    static int choiceIndexFromFilterType(FilterType type) noexcept;
    static void appendEqlParameters(std::vector<std::unique_ptr<juce::RangedAudioParameter>>& parameterLayout);

    EqlModuleProcessor();
    ~EqlModuleProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void releaseResources();
    void resetProcessingState() noexcept;
    void processBlock(juce::AudioBuffer<float>& buffer);
    bool syncParameters() noexcept;
    void setProcessingFilterLimit(int count, bool acceptModel = true) noexcept { processingFilterLimit = count; processingModelUpdatesEnabled = acceptModel; }
    int getLatencySamples() const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;

    int getActiveFilterCount() const noexcept;
    bool isModelEditInProgress() const noexcept { return (modelVersion.load(std::memory_order_acquire) & 1u) != 0; }
    bool addFilter() noexcept;
    using FilterIndexMap = std::array<int, maxFilterCount>;
    std::function<void(const FilterIndexMap&)> onFilterRemapped;
    std::function<void()> onLayoutChanged;
    std::optional<FilterIndexMap> removeFilter(int filterIndex) noexcept;
    bool applyFilterOrder(const std::vector<int>& orderedFilterIndices) noexcept;
    juce::String getDefaultFilterPresetName() const;
    juce::String getSelectedFilterPresetName() const;
    juce::StringArray getFilterPresetNames();
    bool resolveFilterPresetSelection(const juce::XmlElement* catalog);
    bool saveFilterPreset(const juce::String& presetName);
    bool renameFilterPreset(const juce::String& sourcePresetName, const juce::String& newPresetName);
    bool setDefaultFilterPreset(const juce::String& presetName);
    bool loadInitialFilterPreset() noexcept;
    bool loadFilterPreset(const juce::String& presetName) noexcept;
    bool deleteFilterPreset(const juce::String& presetName);
    void markEqlFiltersDirty() noexcept;

private:
    friend class EqlProcessorBank;
    bool restoreState(juce::ValueTree restoredState, bool resolvePreset = true) noexcept;

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

        void transitionTo(const std::array<float, phaseFirSize>& next, int samples) noexcept;

        bool active = false;
        std::array<float, phaseFirSize> taps {};
        std::array<float, phaseFirSize> targetTaps {};
        int transitionRemaining = 0;
        int transitionLength = 0;
        std::array<std::array<float, phaseFirSize * 2>, maxSupportedChannels> state {};
        int writeIndex = 0, historySamples = 0;
    };

    struct FilterDesignState
    {
        bool valid = false;
        bool phase = false;
        bool active = false;
        FilterType type = FilterType::bell;
        float frequency = 0.0f;
        float bandwidth = 0.0f;
        float slope = 0.0f;
        float gainDb = 0.0f;
        double sampleRate = 0.0;
    };

    void parameterChanged(const juce::String&, float) override;
    void setParameterListenersEnabled(bool enabled);
    bool updateSmoothedFilterGains(int samplesPerBlock) noexcept;
    void processFilterSection(juce::AudioBuffer<float>& targetBuffer, int filterIndex, int processChannels);
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
    void resetFilterHistory() noexcept;
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

    template <typename T> struct PreparedFilterSlots {
        void ensure(int count) {
            for (int i = 0; i < count; ++i) if (slots[size_t(i)] == nullptr) slots[size_t(i)] = std::make_unique<T>();
        }
        T& operator[](size_t index) noexcept { jassert(slots[index] != nullptr); return *slots[index]; }
        const T& operator[](size_t index) const noexcept { jassert(slots[index] != nullptr); return *slots[index]; }
        std::array<std::unique_ptr<T>, maxFilterCount> slots;
    };
    void prepareFilterStorage(int count);

    ava::ModuleParameterHost moduleParameterHost;
    juce::AudioProcessorValueTreeState parameters;
    // Model writers may be slow; the audio thread keeps the last complete snapshot.
    struct FilterSettings {
        FilterType type = FilterType::bell;
        float frequency = 632.46f, bandwidth = 1.0f, gain = 0.0f;
        int order = 2;
        bool phase = false, bypass = false;
    };
    struct BlockSettings { std::array<FilterSettings, maxFilterCount> filters {}; int count = 0; };
    struct ModelEdit {
        explicit ModelEdit(EqlModuleProcessor& p) : owner(p), lock(p.modelLock) {
            if (owner.editDepth++ == 0) owner.modelVersion.fetch_add(1, std::memory_order_acq_rel);
        }
        ~ModelEdit() {
            if (--owner.editDepth == 0) owner.modelVersion.fetch_add(1, std::memory_order_release);
        }
        EqlModuleProcessor& owner;
        juce::ScopedLock lock;
    };
    mutable juce::CriticalSection modelLock;
    unsigned editDepth = 0;
    std::atomic<uint64_t> modelVersion { 0 };
    BlockSettings blockSettings;
    int processingFilterLimit = maxFilterCount;
    bool processingModelUpdatesEnabled = true;
    std::atomic<bool> resetRequested { false }, restoreGainsRequested { false };
    std::atomic<int> processingLatency { 0 };
    std::array<std::atomic<float>*, maxFilterCount> filterTypeParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterPhaseParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterFrequencyParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterBandwidthParams {};
    std::array<juce::AudioParameterChoice*, maxFilterCount> filterOrderParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterGainParams {};
    std::array<std::atomic<float>*, maxFilterCount> filterBypassParams {};
    std::array<juce::SmoothedValue<float>, maxFilterCount> filterGainSmoothers {};
    std::array<float, maxFilterCount> effectiveFilterGainDb {};
    PreparedFilterSlots<std::array<BellOrderFilter, 2>> bellOrderFilters;
    PreparedFilterSlots<std::array<BiquadCascade, 2>> shelfOrderFilters;
    PreparedFilterSlots<BiquadCascade> tiltFilters;
    PreparedFilterSlots<BiquadCascade> cutBlendFilters;
    PreparedFilterSlots<PhaseFirFilter> phaseFirFilters;
    juce::dsp::FFT phaseFirFft { phaseFirOrder };
    std::array<FilterDesignState, maxFilterCount> cachedFilterStates {};
    juce::AudioBuffer<float> filterProcessBufferA;
    juce::AudioBuffer<float> filterProcessBufferB;
    int preparedNumChannels = 2;
    double currentSampleRate = 0.0;
    std::atomic<int> activeFilterCount { 0 };
    std::atomic<bool> eqlFiltersDirty { true };
    std::atomic<bool> prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqlModuleProcessor)
};
