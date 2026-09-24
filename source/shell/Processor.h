#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>

#include "../crossover/BufferRouter.h"
#include "OscSettings.h"

class FftModuleProcessor;
class EqlModuleProcessor;
class FftProcessorBank;
class EqlProcessorBank;
class TlsModuleProcessor;
class DynModuleProcessor;
class TrsModuleProcessor;
class OscController;
class AvaAudioProcessorEditor;
namespace ava::routing { class Runtime; }

class AvaAudioProcessor final : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::ValueTree::Listener,
                               private juce::AsyncUpdater
{
public:
    inline static constexpr auto paramGlobalBypassId = "bp";
    inline static constexpr auto oscGlobalAbSlotAId = "a";
    inline static constexpr auto oscGlobalAbSwitchId = "ab-switch.icon";
    inline static constexpr auto oscGlobalAbSlotBId = "b.ab";
    inline static constexpr auto oscGlobalUndoId = "undo.icon";
    inline static constexpr auto oscGlobalRedoId = "redo.icon";
    inline static constexpr auto oscGlobalClipId = "clip";
    inline static constexpr auto oscAddModuleId = "add-module";
    inline static constexpr auto oscCloseModuleId = "close-module.hidden";
    inline static constexpr auto oscXovAddId = "xov-add";
    inline static constexpr auto oscXovDelId = "xov-del";
    inline static constexpr auto oscSoloModeId = "solo-mode";
    inline static constexpr auto paramCrossoverActiveSplitCountId = "split-count";
    inline static constexpr auto paramHostSlotPrefix = "host_slot_";
    inline static constexpr auto activeModuleStateKey = "ava.active_module";
    inline static constexpr auto eqlModuleStateKey = "ava.eql_state";
    inline static constexpr auto fftModuleStateKey = "ava.fft_state";
    inline static constexpr auto tlsModuleStateKey = "ava.tls_state";
    inline static constexpr auto dynModuleStateKey = "ava.dyn_state";
    inline static constexpr auto trsModuleStateKey = "ava.trs_state";
    inline static constexpr auto abCompareSnapshotAStateKey = "ava.ab_compare.a";
    inline static constexpr auto abCompareSnapshotBStateKey = "ava.ab_compare.b";
    inline static constexpr auto abCompareActiveSlotStateKey = "ava.ab_compare.active";
    inline static constexpr auto eqlModuleId = "eql";
    inline static constexpr auto fftModuleId = "fft";
    inline static constexpr auto tlsModuleId = "tls";
    inline static constexpr auto dynModuleId = "dyn";
    inline static constexpr auto trsModuleId = "trs";
    inline static constexpr auto editorWidthStateKey = "ava.editor.width";
    inline static constexpr auto editorHeightStateKey = "ava.editor.height";
    inline static constexpr auto editorHostParametersExpandedStateKey = "ava.editor.host_parameters_expanded";
    inline static constexpr auto editorRoutingExpandedStateKey = "ava.editor.routing_expanded";
    inline static constexpr auto oscEnabledStateKey = "ava.osc.enabled";
    inline static constexpr auto oscInputPortStateKey = "ava.osc.input_port";
    inline static constexpr auto oscOutputHostStateKey = "ava.osc.output_host";
    inline static constexpr auto oscOutputPortStateKey = "ava.osc.output_port";
    inline static constexpr auto oscInstanceNameStateKey = "ava.osc.instance_name";
    static constexpr int hostAutomationSlotCount = 64;

    enum class ActiveModule
    {
        none,
        tls,
        eql,
        fft,
        dyn,
        trs,
    };
    explicit AvaAudioProcessor(bool routingInstance = false);
    ~AvaAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void getStateInformationForABCompareSnapshot(juce::MemoryBlock& destData);
    void setStateInformation(const void* data, int sizeInBytes) override;
    bool applyHistoryStateInformation(const void* data, int sizeInBytes);
    bool applyStateInformationForABCompare(const void* data, int sizeInBytes);
    static void removeModuleStateProperties(juce::ValueTree& state);
    int getABCompareActiveSlot() const noexcept;
    void setABCompareActiveSlot(int slot) noexcept;
    bool isABCompareSnapshotValid(int slot) const noexcept;
    juce::MemoryBlock getABCompareSnapshot(int slot) const;
    void setABCompareSnapshot(int slot, const juce::MemoryBlock& snapshot);

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;
    static juce::String getHostSlotParameterId(int slotIndex);
    static juce::String getHostSlotLetterLabel(int slotIndex);
    static juce::String getHostSlotTargetStateKey(int slotIndex);
    static juce::String getEditorFilterDisplayOrderStateKey(size_t rangeIndex);
    static juce::String getCrossoverParameterId(const char* suffix);
    static juce::String getCrossoverSoloParameterId(size_t rangeIndex);
    static juce::String getEqlAddActionId(size_t rangeIndex);
    static juce::String getEqlDeleteAllActionId(size_t rangeIndex);
    static juce::String getEqlFilterDeleteActionId(size_t rangeIndex, int filterIndex);
    static juce::String getEqlPresetSelectionId(size_t rangeIndex);
    ava::crossover::Settings getCrossoverSettings() const noexcept;
    static const char* stateIdForModule(ActiveModule module) noexcept;
    ActiveModule getActiveModule() const noexcept;
    void setActiveModule(ActiveModule module);
    bool loadModule(ActiveModule module);
    bool clearLoadedModule();
    EqlModuleProcessor* getEqlModuleProcessor() noexcept;
    const EqlModuleProcessor* getEqlModuleProcessor() const noexcept;
    EqlProcessorBank* getEqlProcessorBank() noexcept;
    const EqlProcessorBank* getEqlProcessorBank() const noexcept;
    void setSelectedCrossoverRange(size_t rangeIndex);
    size_t getSelectedCrossoverRange() const noexcept;
    FftModuleProcessor* getFftModuleProcessor() noexcept;
    const FftModuleProcessor* getFftModuleProcessor() const noexcept;
    FftProcessorBank* getFftProcessorBank() noexcept;
    const FftProcessorBank* getFftProcessorBank() const noexcept;
    TlsModuleProcessor* getTlsModuleProcessor() noexcept;
    const TlsModuleProcessor* getTlsModuleProcessor() const noexcept;
    DynModuleProcessor* getDynModuleProcessor() noexcept;
    const DynModuleProcessor* getDynModuleProcessor() const noexcept;
    TrsModuleProcessor* getTrsModuleProcessor() noexcept;
    const TrsModuleProcessor* getTrsModuleProcessor() const noexcept;
    juce::Point<int> getLastEditorSize() const noexcept;
    void setLastEditorSize(int width, int height) noexcept;
    void notifyHostOfStateChange();
    void refreshHostSlotTargets();
    OscSettings getOscSettings() const;
    bool setOscSettings(const OscSettings& settings);
    void refreshOscConfiguration();
    bool isOscInputPortBusy() const noexcept;
    std::vector<OscParameterInfo> getVisibleOscParameters() const;
    std::shared_ptr<AvaAudioProcessor> getRoutingInstanceHandle(int instanceId) noexcept;
    AvaAudioProcessorEditor* getOscActionEditor() const noexcept { return oscActionEditor; }
    void setOscActionEditor(AvaAudioProcessorEditor* editor) noexcept { oscActionEditor = editor; }
    AvaAudioProcessor& getOscOwner() noexcept { return routingOwner != nullptr ? *routingOwner : *this; }
    const AvaAudioProcessor& getOscOwner() const noexcept { return routingOwner != nullptr ? *routingOwner : *this; }
    void synchronizeRouting();
    bool isRoutingInstance() const noexcept { return routingInstance; }

    float getGlobalClipIndicator() const noexcept
    {
        return globalClipIndicator.load(std::memory_order_relaxed);
    }
private:
    friend class ava::routing::Runtime;
    class ScopedProcessingSuspend
    {
    public:
        explicit ScopedProcessingSuspend(AvaAudioProcessor& processorIn) noexcept
            : processor(processorIn)
        {
            processor.suspendProcessing(true);
        }

        ~ScopedProcessingSuspend()
        {
            processor.suspendProcessing(false);
        }

    private:
        AvaAudioProcessor& processor;
    };

    static constexpr size_t maxSupportedChannels = 2;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::RangedAudioParameter* findHostSlotTarget(const juce::String& parameterId) noexcept;
    void applyHostSlotValue(int slotIndex, float normalizedValue) noexcept;
    bool createModuleInstance(ActiveModule module);
    void resetModuleProcessors() noexcept;
    static ActiveModule moduleFromStateId(const juce::String& moduleId);
    int getActiveModuleLatencySamples() const noexcept;
    void requestLatencySamples(int latencySamples) noexcept;
    void updateShellLatency() noexcept;
    void applyPendingShellUpdates();
    void registerActiveModuleStateListeners();
    void clearActiveModuleStateListeners();
    void writeStateInformation(juce::MemoryBlock& destData, bool includeABCompareState);
    bool restoreStateInformation(const void* data, int sizeInBytes, bool includeABCompareState);
    bool restoreStateInformationPreservingLoadedModule(const void* data,
                                                       int sizeInBytes,
                                                       bool includeABCompareState,
                                                       bool suspendProcessingForRestore);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;
    void handleAsyncUpdate() override;
    void ensureActiveCrossoverRangeCount(size_t rangeCount);
    void processOwnBlock(juce::AudioBuffer<float>&);

    juce::AudioProcessorValueTreeState parameters;
    std::unique_ptr<OscController> oscController;
    std::unique_ptr<ava::routing::Runtime> routingRuntime;
    AvaAudioProcessor* routingOwner = nullptr;
    AvaAudioProcessorEditor* oscActionEditor = nullptr;
    bool routingInstance = false;
    std::function<void()> parentStateChanged;
    ava::crossover::BufferRouter crossoverRouter;
    mutable juce::CriticalSection processingLock;
    juce::AudioProcessorValueTreeState* observedModuleValueTreeState = nullptr;
    std::vector<juce::String> observedModuleParameterIds;
    juce::ValueTree observedModuleState;
    std::atomic<float>* globalBypassParam = nullptr;
    std::atomic<float>* crossoverActiveSplitCountParam = nullptr;
    std::array<std::atomic<float>*, ava::crossover::BufferRouter::numSplits> crossoverSplitFrequencyParams {};
    std::array<std::atomic<float>*, ava::crossover::BufferRouter::numRanges> crossoverSoloParams {};
    std::array<std::atomic<float>*, 7> globalListenParams {};
    std::array<juce::String, hostAutomationSlotCount> hostSlotParameterIds {};
    std::array<std::atomic<juce::RangedAudioParameter*>, hostAutomationSlotCount> hostSlotTargets {};
    std::atomic<float> globalClipIndicator { 0.0f };
    std::unique_ptr<EqlProcessorBank> eqlProcessorBank;
    std::unique_ptr<FftProcessorBank> fftProcessorBank;
    std::unique_ptr<TlsModuleProcessor> tlsModuleProcessor;
    std::unique_ptr<DynModuleProcessor> dynModuleProcessor;
    std::unique_ptr<TrsModuleProcessor> trsModuleProcessor;
    std::atomic<ActiveModule> activeModule { ActiveModule::none };
    std::atomic<bool> processingPrepared { false };
    std::atomic<int> lastEditorWidth { 0 };
    std::atomic<int> lastEditorHeight { 0 };
    std::atomic<bool> suppressHostStateNotifications { false };
    std::atomic<bool> pendingHostStateNotification { false };
    std::atomic<int> requestedLatencySamples { 0 };
    std::atomic<size_t> requestedCrossoverRangeCount { 1 };
    std::atomic<size_t> selectedCrossoverRange { 0 };
    mutable juce::CriticalSection abCompareLock;
    std::array<juce::MemoryBlock, 2> abCompareSnapshots;
    std::atomic<int> abCompareActiveSlot { 0 };
    std::atomic<bool> abCompareLatencyLocked { false };
    std::atomic<int> abCompareLatencyFloorSamples { 0 };
    int preparedNumChannels = 2;
    int preparedBlockSize = 0;
    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AvaAudioProcessor)
};
