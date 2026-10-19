#pragma once

#include "Processor.h"
#include "AnimationSettings.h"
#include "../modules/tls/BlockOrder.h"
#include "../modules/tls/GainOrder.h"
#include "SvgExport.h"
#include "../modules/eql/Processor.h"

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

class BoxTextButton;
class BurningFilmArtwork;
class ChoiceControl;
class LocalChoiceControl;
class LocalParameterControl;
class ParameterControl;
class RoutingPanel;
class OscPanel;

class BackgroundClickComponent : public juce::Component
{
public:
    void mouseDown(const juce::MouseEvent&) override
    {
        if (onBackgroundClick != nullptr) onBackgroundClick();
    }
    std::function<void()> onBackgroundClick;
};

class AvaAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                      public svg_export::ExpandableContent,
                                      private juce::Timer,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::ValueTree::Listener
{
public:
    explicit AvaAudioProcessorEditor(AvaAudioProcessor&, int routingInstanceId = 0);
    ~AvaAudioProcessorEditor() override;

    void prepareForProcessorStateRestore() { detachModuleEditorBindings(); }
    void showAnimationSettings();
    void setAnimationSettings(std::shared_ptr<AnimationSettings>);
    bool keyPressed(const juce::KeyPress& key) override;
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
    void requestSvgExport();
    static bool isSvgExportGesture(const juce::ModifierKeys& modifiers);
    void expandContentForSvgExport() override;
    void restoreContentAfterSvgExport() override;
    int getSvgExportCanvasHeight() const override;
    void prepareSvgExportLayout(int canvasHeight) override;
    void finishSvgExportLayout() override;

public:
    bool handleHostSlotAssignRequest(const juce::String& parameterId,
                                     const juce::String& parameterName,
                                     float normalizedValue);
    void showTextPrompt(const juce::String& currentText,
                        std::function<bool(const juce::String&)> onCommit,
                        juce::Rectangle<int> anchorBounds = {},
                        std::function<void()> onClose = {},
                        std::function<void()> onDismiss = {},
                        bool uppercaseInput = true);
    void showInfoPrompt(const juce::String& markdownText);
    void showBlockArrangePrompt(juce::AudioProcessorValueTreeState& state, size_t range, tls::blocks::Order order,
                                std::function<void(const tls::blocks::Order&)> preview,
                                std::function<void(const tls::blocks::Order&)> commit,
                                std::function<void()> dismiss);
    void setReturnToRoutingAction(std::function<void()> action);
    void showChoicePrompt(const juce::Rectangle<int>& anchorBounds,
                          const juce::StringArray& choices,
                          int selectedIndex,
                          std::vector<bool> itemEnabledStates,
                          juce::Justification itemJustification,
                          std::function<void(int)> onSelect,
                          std::function<void()> onClose = {},
                          std::function<void()> onDismiss = {},
                          bool commitBeforeFade = false);
    void showGainArrangePrompt(juce::AudioProcessorValueTreeState& state, size_t range, tls::gain::Order order,
                               std::function<void(const tls::gain::Order&)> preview,
                               std::function<void(const tls::gain::Order&)> commit,
                               std::function<void()> dismiss);
    juce::Rectangle<int> getInfoPromptAnchorBounds() const noexcept;
    juce::Rectangle<int> getInfoPromptVisibleBounds() const noexcept;
    bool handleOscGlobalAction(const juce::String& actionId, float value);
    bool handleOscEqlAction(size_t bandIndex, const juce::String& action, float value);
private:
    void dismissTextPrompt();
    static std::function<void()> makeScrollPreservingPromptClose(AvaAudioProcessorEditor& editor,
                                                                 std::function<void()> closeHandler);
    void timerCallback() override;
    void normalizeOrderForType(int filterIndex);
    void sortFilterSections();
    void clearAllFilters();
    enum class HistoryAction { undo, redo };
    void performHistoryStep(HistoryAction action);
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
    void storeEditorUiState();
    void restoreEditorUiState();
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
    void updateEqlSectionStates(int activeFilterCount);
    void syncEditorWidthToBounds();
    bool updateEditorResizeLimits();
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;
    void valueTreeRedirected(juce::ValueTree& treeWhichHasBeenChanged) override;
    void scheduleProcessorStateResync();
    void scheduleModuleControlsRefresh(bool rebind);
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
    void setupEqlControls(juce::AudioProcessorValueTreeState& initialEqlState);
    EqlModuleProcessor* getActiveEqlProcessor() noexcept;
    const EqlModuleProcessor* getActiveEqlProcessor() const noexcept;
    void scheduleHistorySnapshot();
    void commitPendingHistorySnapshot(bool force = false);
    bool applyHistorySnapshot(const juce::MemoryBlock& snapshot);
    void updateUndoRedoButtons();
    int getActiveFilterContentHeight() const;
    int getFilterContentHeight() const;
    int getFlatFilterContentHeight() const;
    int getActiveFilterCount() const noexcept;
    void layoutGlobalControlsSection(juce::Rectangle<int>& bounds);
    void layoutBottomControlsSection(juce::Rectangle<int>& bounds);
    void layoutCrossoverSection(juce::Rectangle<int>& bounds);
    void finalizeLayout() noexcept;
    void layoutModuleEditorContent(juce::Rectangle<int>& bounds);
    void layoutEqlModuleSections(juce::Rectangle<int>& bounds);
    void ensureModuleTitle();
    void clearHostSlot(int slotIndex);
    void moveHostSlotAssignment(int slotIndex, int direction);
    void refreshHostSlotButtons();
    void armHostSlotMove(int slotIndex, bool controlDrag = false);
    void cancelHostSlotMove();
    void updateHostSlotMoveTargets();
    void applyHostSlotMove(int destinationIndex);
    void handleHostSlotControlDrag(const juce::MouseEvent& event, bool released);

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
    bool restoringEditorUiState = true;
    std::unique_ptr<juce::Label> globalSpacer;
    std::unique_ptr<BoxTextButton> globalBypassButton;
    std::unique_ptr<ButtonAttachment> globalBypassAttachment;
    std::shared_ptr<AnimationSettings> animationSettings = std::make_shared<AnimationSettings>();
    std::unique_ptr<juce::DocumentWindow> animationSettingsWindow;
    std::unique_ptr<BurningFilmArtwork> topLines;
    std::unique_ptr<BurningFilmArtwork> bottomLines;
    std::unique_ptr<BoxTextButton> undoButton;
    std::unique_ptr<BoxTextButton> redoButton;
    std::unique_ptr<BoxTextButton> abSlotAButton;
    std::unique_ptr<BoxTextButton> abSwitchButton;
    std::unique_ptr<BoxTextButton> abSlotBButton;
    std::unique_ptr<BoxTextButton> sortFiltersButton;
    std::array<std::unique_ptr<BoxTextButton>, AvaAudioProcessor::hostAutomationSlotCount> hostSlotNameFields;
    std::array<std::unique_ptr<BoxTextButton>, AvaAudioProcessor::hostAutomationSlotCount> hostSlotButtons;
    std::array<std::unique_ptr<BoxTextButton>, EqlModuleProcessor::maxFilterCount> filterOrderLabels;
    std::array<std::unique_ptr<FilterSection>, EqlModuleProcessor::maxFilterCount> filterSections;
    juce::AudioProcessorValueTreeState* boundEqlState = nullptr;
    juce::Viewport hostParametersViewport;
    BackgroundClickComponent hostParametersContent;
    juce::Viewport filterViewport;
    BackgroundClickComponent filterContent;
#if JUCE_MAC
    void* svgExportKeyboardMonitor = nullptr;
#endif
    std::unique_ptr<BoxTextButton> footerTab;
    std::unique_ptr<juce::Component> verticalResizeHandle;
    std::unique_ptr<juce::Component> horizontalResizeHandle;
    std::unique_ptr<juce::Component> cornerResizeHandle;
    std::unique_ptr<juce::Component> topResizeHandle;
    std::unique_ptr<juce::Component> leftResizeHandle;
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
    bool eqlModuleLoaded = false;
    bool tlsModuleLoaded = false;
    bool dynModuleLoaded = false;
    bool hostParametersExpanded = false;
    bool routingExpanded = false;
    bool oscExpanded = false;
    std::vector<int> filterDisplayOrder;
    bool suppressFilterSectionValueChangeHandlers = false;
    bool suppressHostSlotAutomationSync = false;
    bool suppressProcessorStateResync = false;
    bool suppressEditorSizeStateSave = true;
    bool suppressHistorySnapshots = false;
    std::vector<bool> svgExportFilterExpandedStates;
    std::vector<juce::Viewport*> svgExportViewports;
    std::vector<juce::Point<int>> svgExportViewportPositions;
    juce::Rectangle<int> svgExportLayoutBounds;
    bool svgExportLayoutActive = false;
    bool shellStateListenerRegistered = false;
    std::atomic<bool> processorStateResyncPending { false };
    std::atomic<bool> moduleControlsRefreshPending { false };
    std::atomic<bool> moduleControlsRebindPending { false };
    juce::MemoryBlock committedHistorySnapshot;
    std::vector<juce::MemoryBlock> undoHistory;
    std::vector<juce::MemoryBlock> redoHistory;
    std::atomic<bool> pendingHistorySnapshot { false };
    std::atomic<uint32_t> lastHistoryChangeTimeMs { 0 };
    uint32_t lastClipIndicatorTimeMs = 0;

    struct HostSlotAssignment
    {
        juce::String parameterId;
        juce::String parameterName;
    };
    std::array<HostSlotAssignment, AvaAudioProcessor::hostAutomationSlotCount> hostSlotAssignments;
    int hostSlotMoveSourceIndex = -1;
    bool hostSlotControlDrag = false;
    bool hostSlotControlDragCancelled = false;
    int filterMoveSourceIndex = -1;

    int getFilterIndexForOrderPosition(int orderIndex) const noexcept;
    int getFilterOrderPositionForIndex(int filterIndex) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AvaAudioProcessorEditor)
};
