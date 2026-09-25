#pragma once

#include "Processor.h"
#include "../modules/eql/Processor.h"

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class BoxTextButton;
class ChoiceControl;
class LocalChoiceControl;
class LocalParameterControl;
class ParameterControl;
class RoutingPanel;
class OscPanel;

namespace shell_parameter_focus
{
void clearFocus() noexcept;
void clearFocus(juce::Component& owner) noexcept;
}

class ParameterFocusClearingComponent : public juce::Component
{
public:
    void mouseDown(const juce::MouseEvent&) override
    {
        shell_parameter_focus::clearFocus(*this);
    }
};

class AvaAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::ValueTree::Listener
{
public:
    explicit AvaAudioProcessorEditor(AvaAudioProcessor&, int routingInstanceId = 0);
    ~AvaAudioProcessorEditor() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    class AvaLookAndFeel;
    struct PresetsSection;
    struct FilterSection;

    void toggleHostParametersSection();
    void toggleRoutingSection();
    void toggleOscSection();
    void openRoutingInstance(int instanceId);
    void closeRoutingInstance();
    void showModulePicker();
    void closeActiveModule();
    void loadModule(AvaAudioProcessor::ActiveModule module);
    void showOscParameterList();
    void selectFilterSection(int filterIndex);
    void refreshFilterPresetList(const juce::String& preferredSelection = {});
    void reloadFilterPresetFromProcessor();
    void commitFilterDisplayOrderToProcessor();
    void addFilterPreset();
    void saveFilterPreset();
    bool renameFilterPreset(const juce::String& sourcePresetName, const juce::String& newPresetName);
    void setDefaultFilterPreset();
    void deleteSelectedFilterPreset();

public:
    bool handleHostSlotAssignRequest(const juce::String& parameterId,
                                     const juce::String& parameterName,
                                     float normalizedValue);
    void showTextPrompt(const juce::String& currentText,
                        std::function<bool(const juce::String&)> onCommit,
                        juce::Rectangle<int> anchorBounds = {},
                        std::function<void()> onClose = {},
                        std::function<void()> onDismiss = {});
    void showInfoPrompt(const juce::String& markdownText);
    void setReturnToRoutingAction(std::function<void()> action);
    void showChoicePrompt(const juce::Rectangle<int>& anchorBounds,
                          const juce::StringArray& choices,
                          int selectedIndex,
                          std::vector<bool> itemEnabledStates,
                          juce::Justification itemJustification,
                          std::function<void(int)> onSelect,
                          std::function<void()> onClose = {},
                          std::function<void()> onDismiss = {});
    juce::Rectangle<int> getInfoPromptAnchorBounds() const noexcept;
    juce::Rectangle<int> getInfoPromptVisibleBounds() const noexcept;
    bool handleOscGlobalAction(const juce::String& actionId, float value);
    bool handleOscEqlAction(size_t bandIndex, const juce::String& action, float value);
private:
    void dismissTextPrompt();
    void timerCallback() override;
    void normalizeOrderForType(int filterIndex);
    void sortFilterSectionsByPlace();
    void sortFilterSectionsByFrequency();
    void sortFilterSectionsByDuo();
    void clearAllFilters();
    void performUndo();
    void performRedo();
    void switchABState();
    void captureCurrentABState();
    void copyCurrentABStateToOtherSlot();
    bool restoreABStateSnapshot(const juce::MemoryBlock& snapshot);
    void refreshABCompareButton();
    void refreshEqlFilterSectionsFromProcessor();
    void applyFilterSortOrder(const std::vector<int>& orderedIndices);
    void moveFilterSectionTo(int filterIndex, int destinationOrderPosition);
    void enforceSingleExpandedFilterSection(int preferredFilterIndex = -1);
    void restoreEditorStateFromValueTree();
    void restoreFilterDisplayOrderFromValueTree();
    void storeFilterDisplayOrderToValueTree() noexcept;
    void storeEditorStateToValueTree() noexcept;
    void setLoadedModuleFlags(AvaAudioProcessor::ActiveModule activeModule) noexcept;
    juce::Point<int> getRestoredEditorSize() const noexcept;
    juce::Rectangle<int> getFilterSectionBounds(int filterIndex) const;
    void resetFilterSectionUiState(int filterIndex);
    void removeFilterSectionUiState(int removedIndex, int previousCount);
    void updateSectionStates();
    juce::String getInstanceDisplayName() const;
    void updateInstanceHeadings();
    void setPresetsVisible(bool shouldShow);
    void setEqlFilterSectionsVisible(bool shouldShow);
    void setEqlControlsVisible(bool shouldShow);
    void setFftControlsVisible(bool shouldShow);
    void updateEqlSectionStates(int activeFilterCount);
    void syncEditorWidthToBounds();
    void refreshFftAnalyserResponse();
    void syncFocusedParameterControl();
    double getFocusedParameterControlValueForTarget() const noexcept;
    double getFocusedParameterTargetValueForControl() const noexcept;
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;
    void valueTreeRedirected(juce::ValueTree& treeWhichHasBeenChanged) override;
    void scheduleProcessorStateResync();
    void resyncEditorFromProcessorState();
    void registerParameterListeners();
    void unregisterParameterListeners();
    juce::RangedAudioParameter* findHostAssignableParameter(const juce::String& parameterId) const noexcept;
    void syncHostSlotAssignmentValue(int slotIndex, float normalizedValue);
    void registerObservedModuleParameterListeners(juce::AudioProcessorValueTreeState& moduleValueTreeState);
    void refreshModuleStateListeners();
    void clearModuleStateListeners();
    void detachModuleEditorBindings();
    void rebindActiveModuleEditors();
    void setupShellControls();
    void setupPresetControls();
    void setupFftControls(juce::AudioProcessorValueTreeState& fftState,
                          FftModuleProcessor& fftProcessor);
    void rebindFftModeControls(FftModuleProcessor& fftProcessor);
    void refreshFftAnalyserControls(FftModuleProcessor& fftProcessor);
    void setupEqlControls(juce::AudioProcessorValueTreeState& initialEqlState);
    EqlModuleProcessor* getActiveEqlProcessor() noexcept;
    const EqlModuleProcessor* getActiveEqlProcessor() const noexcept;
    void scheduleHistorySnapshot();
    void commitPendingHistorySnapshot(bool force = false);
    bool applyHistorySnapshot(const juce::MemoryBlock& snapshot);
    void updateUndoRedoButtons();
    int getActiveFilterContentHeight() const;
    int getFilterContentHeight() const;
    int getFftMainContentHeight() const;
    int getActiveFilterCount() const noexcept;
    void resetAnalyserPanelBounds();
    void layoutGlobalControlsSection(juce::Rectangle<int>& bounds);
    void layoutFooter(juce::Rectangle<int>& bounds);
    void layoutCrossoverSection(juce::Rectangle<int>& bounds);
    void finalizeLayout() noexcept;
    void layoutModuleEditorContent(juce::Rectangle<int>& bounds);
    void layoutFftModuleSections(juce::Rectangle<int>& bounds);
    void layoutEqlModuleSections(juce::Rectangle<int>& bounds);
    void ensureModuleTitle();
    void clearHostSlot(int slotIndex);
    void moveHostSlotAssignment(int slotIndex, int direction);
    void refreshHostSlotButtons();

    struct ObservedModuleParameterListeners
    {
        juce::AudioProcessorValueTreeState* valueTreeState = nullptr;
        std::vector<juce::String> parameterIds;
    };

    AvaAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& valueTreeState;
    std::vector<juce::ValueTree> observedModuleStates;
    std::vector<ObservedModuleParameterListeners> observedModuleParameterListeners;
    std::unique_ptr<AvaLookAndFeel> lookAndFeel;
    std::unique_ptr<BoxTextButton> clipButton;
    std::unique_ptr<BoxTextButton> hostButton;
    std::unique_ptr<BoxTextButton> routingButton;
    std::unique_ptr<BoxTextButton> oscButton;
    std::unique_ptr<BoxTextButton> moduleAddButton;
    std::unique_ptr<BoxTextButton> moduleTitle;
    std::unique_ptr<juce::Component> crossoverEditor;
    std::unique_ptr<BoxTextButton> addFilterButton;
    std::unique_ptr<PresetsSection> presetsSection;
    std::unique_ptr<BoxTextButton> fftGeneralProcessorHeader;
    std::unique_ptr<BoxTextButton> fftDynamicProcessorHeader;
    std::unique_ptr<ChoiceControl> fftDynamicModeControl;
    std::unique_ptr<ChoiceControl> fftCorrelationTypeControl;
    std::unique_ptr<ChoiceControl> fftDynamicDirectionControl;
    std::unique_ptr<ParameterControl> fftCorrelationSmoothingControl;
    std::unique_ptr<ParameterControl> fftAttackControl;
    std::unique_ptr<ParameterControl> fftReleaseControl;
    std::unique_ptr<ParameterControl> fftKneeControl;
    std::unique_ptr<ParameterControl> fftRatioControl;
    std::unique_ptr<ParameterControl> fftFloorControl;
    std::unique_ptr<ChoiceControl> fftDspFftSizeControl;
    std::unique_ptr<ParameterControl> fftDspSlopeControl;
    std::unique_ptr<ParameterControl> fftCorrelationImpactControl;
    std::unique_ptr<BoxTextButton> fftDeltaButton;
    std::unique_ptr<ButtonAttachment> fftDeltaAttachment;
    std::unique_ptr<ParameterControl> fftDualMonoLeftThresholdControl;
    std::unique_ptr<ParameterControl> fftDualMonoLeftAdaptiveControl;
    std::unique_ptr<ParameterControl> fftDualMonoRightThresholdControl;
    std::unique_ptr<ParameterControl> fftDualMonoRightAdaptiveControl;
    std::unique_ptr<BoxTextButton> fftDualMonoLinkButton;
    std::unique_ptr<ButtonAttachment> fftDualMonoLinkAttachment;
    std::unique_ptr<BoxTextButton> fftAdaptiveSettingsHeader;
    std::unique_ptr<ParameterControl> fftAdaptiveOffsetControl;
    std::unique_ptr<ParameterControl> fftAdaptiveAttackControl;
    std::unique_ptr<ParameterControl> fftAdaptiveHoldControl;
    std::unique_ptr<ParameterControl> fftAdaptiveReleaseControl;
    std::unique_ptr<BoxTextButton> fftDetectorRangeHeader;
    std::unique_ptr<ParameterControl> fftDetectorLowCutControl;
    std::unique_ptr<ParameterControl> fftDetectorHighCutControl;
    std::unique_ptr<ChoiceControl> fftDspOverlapControl;
    std::unique_ptr<LocalParameterControl> fftAnalyserTimeControl;
    std::unique_ptr<LocalParameterControl> fftAnalyserHighControl;
    std::unique_ptr<LocalParameterControl> fftAnalyserLowControl;
    std::unique_ptr<BoxTextButton> globalBypassButton;
    std::unique_ptr<ButtonAttachment> globalBypassAttachment;
    std::unique_ptr<BoxTextButton> undoButton;
    std::unique_ptr<BoxTextButton> redoButton;
    std::unique_ptr<BoxTextButton> abSlotAButton;
    std::unique_ptr<BoxTextButton> abSwitchButton;
    std::unique_ptr<BoxTextButton> abSlotBButton;
    std::unique_ptr<BoxTextButton> sortPlaceButton;
    std::unique_ptr<BoxTextButton> sortFreqButton;
    std::unique_ptr<BoxTextButton> sortDuoButton;
    std::array<std::unique_ptr<BoxTextButton>, AvaAudioProcessor::hostAutomationSlotCount> hostSlotNameFields;
    std::array<std::unique_ptr<BoxTextButton>, AvaAudioProcessor::hostAutomationSlotCount> hostSlotButtons;
    std::array<std::unique_ptr<BoxTextButton>, EqlModuleProcessor::maxFilterCount> filterOrderLabels;
    std::array<std::unique_ptr<FilterSection>, EqlModuleProcessor::maxFilterCount> filterSections;
    juce::AudioProcessorValueTreeState* boundEqlState = nullptr;
    juce::Viewport hostParametersViewport;
    ParameterFocusClearingComponent hostParametersContent;
    juce::Viewport filterViewport;
    ParameterFocusClearingComponent filterContent;
    std::unique_ptr<juce::Slider> focusedParameterControl;
    std::unique_ptr<BoxTextButton> footerTab;
    std::unique_ptr<juce::Component> verticalResizeHandle;
    std::unique_ptr<juce::Component> fftAnalyserComponent;
    std::unique_ptr<juce::Component> textPromptOverlay;
    std::unique_ptr<juce::DocumentWindow> oscParameterListWindow;
    std::unique_ptr<RoutingPanel> routingPanel;
    std::shared_ptr<AvaAudioProcessor> activeInstanceProcessor;
    std::unique_ptr<AvaAudioProcessorEditor> activeInstanceEditor;
    std::function<void()> returnToRoutingAction;
    int activeInstanceId = 0;
    int routingInstanceId = 0;
    std::unique_ptr<OscPanel> oscPanel;
    std::unique_ptr<juce::Component> tlsModuleEditor;
    std::unique_ptr<juce::Component> dynModuleEditor;
    std::unique_ptr<juce::Component> trsModuleEditor;
    bool eqlModuleLoaded = false;
    bool fftModuleLoaded = false;
    bool tlsModuleLoaded = false;
    bool dynModuleLoaded = false;
    bool trsModuleLoaded = false;
    bool hostParametersExpanded = false;
    bool routingExpanded = false;
    bool oscExpanded = false;
    std::vector<int> filterDisplayOrder;
    bool suppressFilterSectionValueChangeHandlers = false;
    bool suppressFftAnalyserControlChangeHandlers = false;
    bool suppressFocusedParameterControlChangeHandlers = false;
    bool suppressHostSlotAutomationSync = false;
    bool suppressProcessorStateResync = false;
    bool suppressEditorSizeStateSave = true;
    bool suppressHistorySnapshots = false;
    bool shellStateListenerRegistered = false;
    std::atomic<bool> processorStateResyncPending { false };
    juce::MemoryBlock committedHistorySnapshot;
    std::vector<juce::MemoryBlock> undoHistory;
    std::vector<juce::MemoryBlock> redoHistory;
    std::atomic<bool> pendingHistorySnapshot { false };
    std::atomic<uint32_t> lastHistoryChangeTimeMs { 0 };
    juce::Slider* focusedParameterTargetSlider = nullptr;
    uint32_t lastClipIndicatorTimeMs = 0;

    struct HostSlotAssignment
    {
        juce::String parameterId;
        juce::String parameterName;
    };
    std::array<HostSlotAssignment, AvaAudioProcessor::hostAutomationSlotCount> hostSlotAssignments;
    int hostSlotMoveSourceIndex = -1;
    int filterMoveSourceIndex = -1;

    int getFilterIndexForOrderPosition(int orderIndex) const noexcept;
    int getFilterOrderPositionForIndex(int filterIndex) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AvaAudioProcessorEditor)
};
