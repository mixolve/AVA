#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <vector>
#include <utility>

#include "../crossover/BufferRouter.h"
#include "../modules/shared/ChannelRouting.h"
#include "OscSettings.h"
#include "ModuleOrder.h"
#include "ModuleStack.h"

class EqlModuleProcessor;
class EqlProcessorBank;
class BlockChainProcessor;
class DynModuleProcessor;
class OscController;
struct FilterParameterDomain;
class AvaAudioProcessorEditor;
class InstanceLinkRegistry;
class InstanceLinkAccess;
namespace ava::routing { class Runtime; }

class AvaAudioProcessor final : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::ValueTree::Listener,
                               private juce::AsyncUpdater
{
public:
    inline static constexpr auto paramSyncTypeId = "sync-type";
    inline static constexpr auto paramBpmId = "bpm";
    inline static constexpr auto paramInstanceMuteId = "mt";
    inline static constexpr auto paramGlobalBypassId = "bp";
    inline static constexpr auto paramBypassCompensationId = "bp-compensation-gain";
    inline static constexpr auto paramCrossoverActiveSplitCountId = "split-count";
    inline static constexpr auto paramHostSlotPrefix = "host_slot_";
    inline static constexpr auto linkUuidStateKey = "ava.link.uuid";
    inline static constexpr auto linkTargetsStateKey = "ava.link.targets";
    inline static constexpr auto linkCountStateKey = "ava.link.count";
    inline static constexpr auto linkSendStateKey = "ava.link.send";
    inline static constexpr auto linkReceiveStateKey = "ava.link.receive";
    inline static constexpr auto linkPausedStateKey = "ava.link.paused";
    inline static constexpr auto linkUnitNumberStateKey = "ava.link.unit_number";
    inline static constexpr auto activeModuleStateKey = "ava.active_module";
    inline static constexpr auto moduleStackStateKey = "ava.module_stacks_v1";
    inline static constexpr auto eqlModuleStateKey = "ava.eql_state";
    inline static constexpr auto blockChainModuleStateKey = "ava.chain_state";
    inline static constexpr auto dynModuleStateKey = "ava.dyn_state";
    inline static constexpr auto abCompareSnapshotAStateKey = "ava.ab_compare.a";
    inline static constexpr auto abCompareSnapshotBStateKey = "ava.ab_compare.b";
    inline static constexpr auto abCompareActiveSlotStateKey = "ava.ab_compare.active";
    inline static constexpr auto eqlModuleId = "eql";
    inline static constexpr auto blockChainModuleId = "chain";
    inline static constexpr auto dynModuleId = "dyn";
    inline static constexpr auto editorWidthStateKey = "ava.editor.width";
    inline static constexpr auto editorHeightStateKey = "ava.editor.height";
    inline static constexpr auto oscEnabledStateKey = "ava.osc.enabled";
    inline static constexpr auto oscInputPortStateKey = "ava.osc.input_port";
    inline static constexpr auto oscOutputHostStateKey = "ava.osc.output_host";
    inline static constexpr auto oscOutputPortStateKey = "ava.osc.output_port";
    static constexpr int assignableHostSlotCount = 64;
    static constexpr int hostAutomationSlotCount = assignableHostSlotCount;
    inline static constexpr auto hostSlotDefaultsKey="ava.host_slot.defaults_initialized";
    static constexpr int routingChannelCount = ava::channels::count;
    inline static constexpr auto paramProcessingInputId = "process-input";
    inline static constexpr auto paramOutputMuteId = "out-mute";
    inline static constexpr auto paramSoloInstanceId = "solo-ins";
    inline static constexpr auto paramSoloChannelId = "solo-ch";
    static juce::StringArray getProcessingInputChoices();
    bool isProcessingInputAvailable(int choiceIndex) const;
    std::array<int, 2> getProcessingInputChannels() const noexcept;
    bool isInstanceSoloBypassed() const;

    enum class ActiveModule {
        none,
        chain,
        eql,
        dyn,
    };
    inline static constexpr std::array availableModules {
        ActiveModule::chain,
        ActiveModule::eql,
        ActiveModule::dyn
    };
    explicit AvaAudioProcessor(bool routingInstance = false);
    bool isRestoringState() const noexcept { return restoringModel.load(std::memory_order_acquire); }
    uint64_t getLinkSessionId() const noexcept { return linkSessionId; }
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
    bool applyClipboardStateInformation(const void* data, int sizeInBytes);
    bool applyHistoryStateInformation(const void* data, int sizeInBytes);
    bool applyStateInformationForABCompare(const void* data, int sizeInBytes);
    void removeModuleStateProperties(juce::ValueTree& state) const;
    int getABCompareActiveSlot() const noexcept;
    void setABCompareActiveSlot(int slot, bool notifyHost = true) noexcept;
    bool isABCompareSnapshotValid(int slot) const noexcept;
    juce::MemoryBlock getABCompareSnapshot(int slot) const;
    void setABCompareSnapshot(int slot, const juce::MemoryBlock& snapshot, bool notifyHost = true);

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;
    const juce::AudioProcessorValueTreeState& getValueTreeState() const noexcept;
    static juce::String getHostSlotParameterId(int slotIndex);
    static juce::String getHostSlotLetterLabel(int slotIndex);
    static juce::String getHostSlotTargetStateKey(int slotIndex);
    static juce::String getEditorFilterDisplayOrderStateKey(size_t rangeIndex);
    static juce::String getCrossoverParameterId(const char* suffix);
    static juce::String getCrossoverSoloParameterId(size_t rangeIndex);
    ava::crossover::Settings getCrossoverSettings() const noexcept;
    static const char* stateIdForModule(ActiveModule module) noexcept;
    ActiveModule getActiveModule() const noexcept;
    module_order::Order getModuleOrder() const noexcept;
    module_order::Order getModuleOrderForRange(size_t rangeIndex) const noexcept;
    std::vector<ava::module_stack::Slot> getModuleStackForRange(size_t rangeIndex) const;
    int addModuleCopy(size_t rangeIndex, int type, std::optional<size_t> shownIndex = std::nullopt);
    int insertModuleAt(size_t rangeIndex, int type, size_t shownIndex);
    bool deleteModuleSlot(size_t rangeIndex, int uid);
    bool setModuleSlotEnabled(size_t rangeIndex, int uid, bool enabled);
    bool isModuleSlotBypassed(size_t rangeIndex, int uid) const;
    bool moveModuleSlots(size_t rangeIndex, const std::vector<int>& order);
    bool selectModuleCopy(int uid);
    int getSelectedModuleCopy() const noexcept { return selectedModuleCopy.load(); }
    BlockChainProcessor* getEditingBlockChainProcessor() noexcept;
    DynModuleProcessor* getEditingDynModuleProcessor() noexcept;
    EqlProcessorBank* getEditingEqlProcessorBank() noexcept;
    EqlModuleProcessor* getEditingEqlProcessor() noexcept;
    BlockChainProcessor* getCopiedBlockChainProcessor(int uid) noexcept;
    DynModuleProcessor* getCopiedDynProcessor(int uid) noexcept;
    EqlModuleProcessor* getCopiedEqlProcessor(int uid, size_t band) noexcept;
    bool isModuleHiddenForRange(size_t rangeIndex, int moduleId) const noexcept;
    void setModuleHiddenForRange(size_t rangeIndex, int moduleId, bool hidden);
    bool setModuleSlotShown(size_t rangeIndex, int uid, bool shown);
    juce::RangedAudioParameter* getBandModuleVisibilityParameter(size_t rangeIndex, int moduleId) const noexcept;
    void setModuleOrder(const module_order::Order& order);
    unsigned getModuleBypassMask() const noexcept;
    void setModuleBypassed(int id, bool bypassed);
    void setActiveModule(ActiveModule module);
    bool loadModule(ActiveModule module);
    EqlModuleProcessor* getEqlModuleProcessor() noexcept;
    const EqlModuleProcessor* getEqlModuleProcessor() const noexcept;
    EqlProcessorBank* getEqlProcessorBank() noexcept;
    const EqlProcessorBank* getEqlProcessorBank() const noexcept;
    void setSelectedCrossoverRange(size_t rangeIndex);
    size_t getSelectedCrossoverRange() const noexcept;
    BlockChainProcessor* getBlockChainProcessor() noexcept;
    const BlockChainProcessor* getBlockChainProcessor() const noexcept;
    DynModuleProcessor* getDynModuleProcessor() noexcept;
    const DynModuleProcessor* getDynModuleProcessor() const noexcept;
    void setLastEditorSize(int width, int height) noexcept;
    void notifyHostOfStateChange();
    // While REAPER/another host writes an automation slot, mirroring the value
    // into a child module is not a second (non-parameter) host edit.
    bool isApplyingHostSlotValue() const noexcept
    {
        return getRoutingOwner().applyingHostSlotValue.load(std::memory_order_acquire);
    }
    // One host state notification per completed user gesture (not per slider tick).
    // These are UI-message-thread methods; routing children forward to the owner.
    uint64_t beginHostStateEdit();
    void endHostStateEdit(uint64_t gestureId);
    void notifyHostOfViewChange();
    struct HistoryState {
        struct Entry {
            // Ordinary actions restore the complete plugin snapshot. An EQL
            // preset action instead restores the captured EQL bank directly,
            // without tearing down the routing processors and their editors.
            juce::MemoryBlock snapshot;
            int eqlPresetInstanceId = 0;
            juce::MemoryBlock eqlPresetBank;

            Entry() = default;
            Entry(const juce::MemoryBlock& fullSnapshot) : snapshot(fullSnapshot) {}
            Entry(const juce::MemoryBlock& fullSnapshot, int instanceId,
                  const juce::MemoryBlock& filterBank)
                : snapshot(fullSnapshot), eqlPresetInstanceId(instanceId), eqlPresetBank(filterBank) {}

            bool isEqlPreset() const noexcept
            {
                return eqlPresetInstanceId > 0 && !eqlPresetBank.isEmpty();
            }
            size_t getSize() const noexcept
            {
                return snapshot.getSize() + eqlPresetBank.getSize();
            }
        };
        juce::MemoryBlock committed;
        // A clean baseline survives closing/reopening the editor. Re-capture
        // only if an audio parameter changed while the editor was closed.
        uint64_t committedRevision = 0;
        std::vector<Entry> undo, redo;
        std::atomic<bool> pending { false };
        std::atomic<uint32_t> lastChange { 0 };
    };
    HistoryState& getHistoryState() noexcept { return getRoutingOwner().history; }
    uint64_t getStateRevision() const noexcept { return getRoutingOwner().stateRevision.load(std::memory_order_relaxed); }
    void refreshHostSlotTargets();
    void syncHostSlotValues(bool fromAudioCallback = false);
    float getRoutingInstanceBypassValue(int instanceId) const;
    void setRoutingInstanceBypassValue(int instanceId, bool bypassed);
    OscSettings getOscSettings() const;
    bool setOscSettings(const OscSettings& settings);
    void refreshOscConfiguration();
    bool isOscInputPortBusy() const noexcept;
    juce::String getHostAssignmentTargetId(const juce::String& parameterId) const;
    juce::RangedAudioParameter* findHostSlotTarget(const juce::String& parameterId) const noexcept;
    std::vector<OscParameterInfo> getAssignedOscParameters() const;
    int getRoutingInstanceId() const noexcept;
    juce::String getHostSlotTargetId(int slotIndex) const;
    juce::String getHostSlotDisplayName(int slotIndex) const;
    juce::String getHostSlotOscAddress(int slotIndex) const;
    bool isHostParameterAssigned(const juce::String& parameterId) const;
    bool toggleHostSlotAssignment(const juce::String& parameterId);
    void clearHostSlotAssignment(int slotIndex);
    void swapHostSlotAssignments(int first, int second);
    std::shared_ptr<AvaAudioProcessor> getRoutingInstanceHandle(int instanceId) noexcept;
    AvaAudioProcessorEditor* getContentEditor() const noexcept { return contentEditor; }
    void setContentEditor(AvaAudioProcessorEditor* editor) noexcept { contentEditor = editor; }
    AvaAudioProcessor& getRoutingOwner() noexcept { return routingOwner != nullptr ? *routingOwner : *this; }
    const AvaAudioProcessor& getRoutingOwner() const noexcept { return routingOwner != nullptr ? *routingOwner : *this; }
    void synchronizeRouting();
    bool isRoutingInstance() const noexcept { return routingInstance; }

    // Incremented for every clipped block so short clips between editor timer
    // ticks are not lost.
    uint32_t getGlobalClipEventCount() const noexcept
    {
        return globalClipEventCount.load(std::memory_order_relaxed);
    }
private:
    friend class ava::routing::Runtime;
    friend class InstanceLinkRegistry;
    friend class InstanceLinkAccess;
    friend class SibilLinkAccess;
    class ScopedProcessingSuspend
    {
    public:
        explicit ScopedProcessingSuspend(AvaAudioProcessor& processorIn) noexcept
            : processor(processorIn), wasSuspended(processorIn.isSuspended())
        {
            processor.suspendProcessing(true);
        }

        ~ScopedProcessingSuspend()
        {
            processor.suspendProcessing(wasSuspended);
        }

    private:
        AvaAudioProcessor& processor;
        const bool wasSuspended;
    };

    uint64_t linkSessionId = 0; // root plugin instances only; not internal routing nodes
    static constexpr size_t maxSupportedChannels = 2;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void applyHostSlotValue(int slotIndex, float normalizedValue) noexcept;
    void pollHostSlotValuesFromAudioCallback() noexcept;
    void queueCurrentHostSlotValues();
    void invalidateHostSlotTargets();
    juce::RangedAudioParameter* findLocalHostSlotTarget(const juce::String& parameterId) const noexcept;
    bool createModuleInstance(ActiveModule module);
    static ActiveModule moduleFromStateId(const juce::String& moduleId);
    ava::crossover::BufferRouter::RangeLatencies getModuleChainRangeLatencies(unsigned bypassMask = ~0u) const noexcept;
    void refreshModuleBypasses();
    void registerModuleSlotBypassTarget(size_t band, int uid, int type);
    void assignNewModuleBypassToHostSlot(size_t band, int uid);
    FilterParameterDomain getFilterParameterDomain(const juce::String& id) const;
    void remapEqlFilterTargets(size_t band, const std::array<int, 64>& mapping, int copyUid = -1);
    int getModuleChainLatencySamples() const noexcept;
    void requestLatencySamples(int latencySamples) noexcept;
    void updateShellLatency() noexcept;
    void queueHostStateNotification();
    void flushHostStateNotification();
    void applyPendingShellUpdates();
    void registerLoadedModuleStateListeners();
    void clearLoadedModuleStateListeners();
    void writeStateInformation(juce::MemoryBlock& destData, bool includeABCompareState);
    bool restoreStateInformation(const void* data, int sizeInBytes,
                                 bool includeABCompareState, bool restoreHostSlotValues = true);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;
    void valueTreeRedirected(juce::ValueTree&) override;
    void handleAsyncUpdate() override;
    void ensureActiveCrossoverRangeCount(size_t rangeCount);
    void recordClippedBlock(const juce::AudioBuffer<float>& buffer) noexcept;
    void processOwnBlock(juce::AudioBuffer<float>&, const juce::AudioBuffer<float>* routingInputs);
    unsigned processChannelBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&,
                                 juce::AudioBuffer<float>* routingAndOutput = nullptr);
    unsigned processOwnChannels(juce::AudioBuffer<float>&);
    static void writeOutputChannels(juce::AudioBuffer<float>&, unsigned channelMask, bool muted);
    void applyInstanceMute(juce::AudioBuffer<float>&, unsigned channelMask);
    void delayUnselectedChannels(juce::AudioBuffer<float>&, unsigned selectedMask, int latency);
    struct AudioBuffers {
        void prepare(double sampleRate, int blockSize, int maximumLatency);
        juce::AudioBuffer<float> routingChannels, selectedInputBuffer, routingInputSnapshot, unselectedDelay;
        ava::crossover::BufferRouter crossoverRouter;
        int unselectedDelayPosition = 0, unselectedLatency = 0, blockSize = 0, maximumLatency = 0;
        unsigned lastProcessingMask = 0, lastModuleBypassMask = 0;
        ava::crossover::Settings settings;
        int inputChoice = 0;
        bool globalBypass = false, instanceBypass = false, instanceMuted = false, outputMuted = false;
        float instanceMuteGain = -1.0f;
        unsigned bypassMask = 0;
        module_order::Order order = module_order::defaultOrder;
    };
    struct ModuleCopy {
        int uid = 0, type = 0, range = 0, serial = 0;
        std::shared_ptr<BlockChainProcessor> blocks;
        std::shared_ptr<DynModuleProcessor> dyn;
        std::shared_ptr<EqlProcessorBank> eql;
    };
    struct RenderGraph {
        std::shared_ptr<BlockChainProcessor> blocks;
        std::shared_ptr<DynModuleProcessor> dyn;
        std::shared_ptr<EqlProcessorBank> eql;
        std::shared_ptr<ava::routing::Runtime> routing;
        std::array<ava::module_stack::Order, ava::crossover::BufferRouter::numRanges> moduleStacks;
        std::map<int, std::shared_ptr<ModuleCopy>> moduleCopies;
        std::shared_ptr<AudioBuffers> buffers;
        std::array<int, ava::crossover::BufferRouter::numRanges> filterLimits {};
        // Capture each EQL copy's active filter counts alongside the original.
        // Never read new counts against an old graph's latency buffers.
        std::map<int, std::array<int, ava::crossover::BufferRouter::numRanges>> copiedEqlFilterLimits;
        int maximumLatency = 0;
    };
    class RenderingScope {
    public:
        explicit RenderingScope(AvaAudioProcessor&, RenderGraph* retained = nullptr);
        ~RenderingScope();
        explicit operator bool() const { return graph != nullptr && graph->buffers != nullptr; }
    private:
        AvaAudioProcessor& owner;
        AvaAudioProcessor* previousOwner;
        RenderGraph* previousGraph;
        RenderGraph* graph = nullptr;
        bool acquired = false, previousFrozen = false;
    };
    static thread_local AvaAudioProcessor* renderingOwner;
    static thread_local RenderGraph* renderingGraph;
    static thread_local bool renderingControlsFrozen;
    struct RenderTransition {
        enum class Phase { stable, fadeOut, fadeIn };
        RenderGraph* current = nullptr;
        uint64_t signature = 0;
        Phase phase = Phase::stable;
        int remaining = 0, rampSamples = 1, warmup = 0, previousLatency = 0;
        float gain = 1.0f, step = 0.0f;
    } renderTransition;
    uint64_t getRenderControlSignature(const RenderGraph&) const noexcept;
    void applyRenderTransition(juce::AudioBuffer<float>&) noexcept;
    void resetRenderTransition() noexcept;
    bool isInstanceBypassed() const noexcept;
    std::shared_ptr<RenderGraph> publishedRenderGraph;
    std::vector<std::shared_ptr<RenderGraph>> retiredRenderGraphs;
    std::atomic<RenderGraph*> activeRenderGraph { nullptr }, renderHazard { nullptr }, candidateHazard { nullptr };
    std::atomic<bool> restoringModel { false }, publishingGraph { false };
    void publishRenderGraph(bool newBuffers = false);
    void collectRetiredRenderGraphs();
    std::atomic<bool> renderReclamationPending { false };
    AudioBuffers& audioBuffers() const noexcept;
    ava::routing::Runtime* getRenderRouting() const noexcept;
    int getMaximumChainLatencySamples() const noexcept;
    void syncAudioModuleParameters();
    bool isGlobalBypassed() const noexcept;
    std::atomic<float>* processingInputParam = nullptr;
    std::atomic<float>* soloInstanceParam = nullptr;
    std::atomic<float>* soloChannelParam = nullptr;
    std::atomic<bool> acceptedInstanceBypassed { false };

    juce::AudioProcessorValueTreeState parameters;
    std::unique_ptr<OscController> oscController;
    std::shared_ptr<ava::routing::Runtime> routingRuntime;
    AvaAudioProcessor* routingOwner = nullptr;
    AvaAudioProcessorEditor* contentEditor = nullptr;
    bool routingInstance = false;
    int routingNodeId = 0;
    std::function<void()> parentStateChanged;
    mutable juce::CriticalSection processingLock;
    struct ObservedModuleState
    {
        juce::AudioProcessorValueTreeState* parameters = nullptr;
        std::vector<juce::String> parameterIds;
        juce::ValueTree state;
    };
    std::vector<ObservedModuleState> observedModuleStates;
    std::atomic<float>* globalBypassParam = nullptr;
    std::atomic<float>* bypassCompensationParam = nullptr;
    std::atomic<float>* instanceMuteParam = nullptr;
    std::atomic<float>* outputMuteParam = nullptr;
    std::atomic<float>* crossoverActiveSplitCountParam = nullptr;
    std::array<std::atomic<float>*, ava::crossover::BufferRouter::numSplits> crossoverSplitFrequencyParams {};
    std::array<std::atomic<float>*, ava::crossover::BufferRouter::numRanges> crossoverSoloParams {};
    std::atomic<bool> manualSoloStatePending { false };
    // -1: restored/default flags; -2: no solo; 0: owner; positive: routing node.
    std::array<std::atomic<int>, 2> requestedSolo {{-1, -1}};
    std::atomic<bool> soloStatePending { false };
    static thread_local AvaAudioProcessor* updatingSoloOwner;
    bool synchronisingHostSlots = false; // Protected by processingLock.
    std::atomic<bool> applyingHostSlotValue { false }; // Automation -> module propagation.
    std::array<juce::String, hostAutomationSlotCount> hostSlotParameterIds {};
    // Host parameters are owned by APVTS for the processor lifetime. A raw
    // pointer lets the offline callback read the actual host-supplied value
    // without a ValueTree lookup or depending on a message-thread callback.
    std::array<juce::RangedAudioParameter*, hostAutomationSlotCount> hostSlotParameters {};
    std::array<std::atomic<float>, hostAutomationSlotCount> observedHostSlotValues {};
    struct HostSlotBinding;
    std::array<std::unique_ptr<HostSlotBinding>, hostAutomationSlotCount> hostSlotBindings;
    std::array<std::atomic<juce::RangedAudioParameter*>, hostAutomationSlotCount> hostSlotTargets {};
    std::array<std::atomic<float>, hostAutomationSlotCount> pendingHostSlotValues {};
    // One or more host values must be applied before the next audio block.
    std::atomic<bool> hostSlotAutomationPending { false };
    // Suppress quantization/round-trip echoes of values supplied by the DAW.
    std::array<std::atomic<float>, hostAutomationSlotCount> hostDrivenTargetValues {};
    std::map<int, std::unique_ptr<juce::RangedAudioParameter>> routingBypassTargets;
    std::map<int, std::unique_ptr<juce::RangedAudioParameter>> routingCompensationTargets;
    std::atomic<uint32_t> globalClipEventCount { 0 };
    std::shared_ptr<EqlProcessorBank> eqlProcessorBank;
    std::shared_ptr<BlockChainProcessor> blockChainModuleProcessor;
    std::shared_ptr<DynModuleProcessor> dynModuleProcessor;
    std::map<int, std::shared_ptr<ModuleCopy>> moduleCopies;
    std::array<ava::module_stack::Order, ava::crossover::BufferRouter::numRanges> moduleStacks;
    std::array<std::array<int, ava::module_stack::typeCount>, ava::crossover::BufferRouter::numRanges> lastCreatedModuleNumbers {};
    int nextModuleCopyUid = ava::module_stack::originalCount;
    std::atomic<int> selectedModuleCopy { -1 };
    void resetModuleCopies();
    void saveModuleCopies(juce::ValueTree& state) const;
    bool restoreModuleCopies(const juce::ValueTree& state);
    std::shared_ptr<ModuleCopy> makeModuleCopy(int type,int range,int uid,int serial);
    std::array<std::unique_ptr<juce::RangedAudioParameter>, 3> moduleBypassTargets;
    // One stable, automatable BP proxy per MODULES tile, including module copies.
    std::map<std::pair<size_t, int>, std::unique_ptr<juce::RangedAudioParameter>> moduleSlotBypassTargets;
    std::atomic<unsigned> moduleBypasses { 0 };
    std::array<std::atomic<uint64_t>, ava::crossover::BufferRouter::numRanges> bandModuleOrders {};
    // Local EQL/DYN visibility is independent for each crossover range.
    std::array<std::atomic<unsigned>, ava::crossover::BufferRouter::numRanges> bandHiddenMasks {};
    std::array<std::array<std::unique_ptr<juce::RangedAudioParameter>, 2>,
               ava::crossover::BufferRouter::numRanges> bandVisibilityParameters {};
    std::atomic<ActiveModule> activeModule { ActiveModule::none };
    std::atomic<bool> processingPrepared { false };
    std::atomic<int> lastEditorWidth { 0 };
    std::atomic<int> lastEditorHeight { 0 };
    std::atomic<bool> suppressHostStateNotifications { false };
    HistoryState history;
    std::atomic<uint64_t> stateRevision { 0 };
    std::atomic<uint64_t> viewStateRevision { 0 };
    // Protected by processingLock; revision tags also detect audio-thread edits.
    std::unique_ptr<juce::XmlElement> cachedHostStateXml;
    juce::MemoryBlock cachedHostStateData;
    uint64_t cachedHostAudioRevision = 0, cachedHostViewRevision = 0;
    std::atomic<bool> pendingHostStateNotification { false };
    // The root editor marks a mouse gesture as one logical state edit.
    // Audio callbacks never wait on this mechanism.
    std::atomic<uint64_t> nextHostStateEditId { 0 };
    std::atomic<uint64_t> activeHostStateEditId { 0 };
    std::atomic<bool> activeHostStateEditDirty { false };
    std::atomic<int> requestedLatencySamples { 0 };
    std::atomic<double> tailLengthSeconds { 0.0 };
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
