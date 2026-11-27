#pragma once

#include "Processor.h"
#include "../modules/shared/BlockCatalog.h"
#include "../modules/gain/Order.h"
#include "SvgExport.h"
#include "Style.h"
#include "Controls.h"
#include "ControlSearch.h"
#include "../modules/eql/Processor.h"

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <map>
#include <span>
#include <vector>

class BoxTextButton;
class ChoiceControl;
class LocalChoiceControl;
class PushControl;
class ParameterControl;
class RoutingPanel;
class OscPanel;
class ModuleList;

// Implemented by the zoom/DPI wrapper that owns the host-facing editor window.
class DockWindowResizer
{
public:
    virtual ~DockWindowResizer() = default;
    virtual void resizeDockContent(int width, int height) = 0;
};

class BackgroundClickComponent : public ControlBlockComponent
{
public:
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (onBackgroundClick != nullptr) onBackgroundClick(event);
        ControlBlockComponent::mouseDown(event);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (onBackgroundRelease != nullptr && !event.mouseWasDraggedSinceMouseDown()
            && !event.mods.isPopupMenu() && getComponentAt(event.getPosition()) == this)
            onBackgroundRelease();
        ControlBlockComponent::mouseUp(event);
    }
    void paint(juce::Graphics& graphics) override
    {
        if (isOpaque())
        {
            paintPluginBackground(graphics);
            
        }
    }
    std::function<void(const juce::MouseEvent&)> onBackgroundClick;
    std::function<void()> onBackgroundRelease;
};

class DockSlotContent final : public juce::Component
{
public:
    void setEmptySlots(std::vector<juce::Rectangle<int>> slots)
    {
        if (emptySlots == slots) return;
        emptySlots = std::move(slots);
        repaint();
    }
    void paint(juce::Graphics& graphics) override
    {
        for (const auto bounds : emptySlots)
            paintUiFrame(graphics, bounds.toFloat(), uiGreyLight,
                ControlAppearance::pixelThickness(graphics, 1.0f), 0.0f);
    }
private:
    std::vector<juce::Rectangle<int>> emptySlots;
};

class QuickAccessStrip final : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.setColour(uiPanelBackground);
        g.fillAll();
    }
};

class SquareToggleButton final : public juce::Button
{
public:
    explicit SquareToggleButton(const juce::String& name) : juce::Button(name)
    {
        setClickingTogglesState(true);
        setWantsKeyboardFocus(false);
    }
    void paintButton(juce::Graphics& g, bool hovered, bool held) override
    {
        g.setColour(held ? uiWhite : isHoverInversionReady(*this, hovered && isEnabled(), "dock.button")
            ? uiGreyDark : getToggleState() ? uiWhite : uiGreyLight);
        g.fillRect(getLocalBounds());
    }
};

class StatusIndicator final : public juce::Component
{
public:
    StatusIndicator() { setName("status-indicator"); setInterceptsMouseClicks(false, false); }
    void setActive(bool activeIn) { if (active != activeIn) { active = activeIn; repaint(); } }
    void paint(juce::Graphics& g) override
    {
        g.setColour(active ? uiWhite : uiGreyLight);
        g.fillRect(getLocalBounds());
    }
private:
    bool active = false;
};

class DockSortOverlay final : public juce::Component
{
public:
    DockSortOverlay() { setMouseCursor(juce::MouseCursor::NormalCursor); }
    ~DockSortOverlay() override { if (dragging) beginDragAutoRepeat(0); }
    std::function<void(const juce::MouseEvent&, int)> onGesture;
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        dragging = true;
        beginDragAutoRepeat(40);
        if (onGesture) onGesture(event, 0);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!dragging || event.getDistanceFromDragStart() < 4) return;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        if (onGesture) onGesture(event, 1);
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        dragging = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        beginDragAutoRepeat(0);
        if (onGesture) onGesture(event, 2);
    }
private:
    bool dragging = false;
};

class HorizontalControlViewport final : public UiScrollViewport
{
public:
    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(uiPanelBackground);
    }
};

class HeaderControlRow final : private juce::ScrollBar::Listener, private juce::ComponentListener
{
public:
    explicit HeaderControlRow(ControlBlockComponent& blockIn, juce::String titleIn = "MAIN")
        : block(blockIn), title(std::move(titleIn)) {}
    ~HeaderControlRow() override;

    void attach(const juce::String& name);
    void layout(juce::Rectangle<int> header, std::span<juce::Component* const> controls);
    void layoutContents(std::span<juce::Component* const> controls);
    int getPreferredHeight() const;
    void syncScrollBar();

    DockSlotContent content;
    HorizontalControlViewport viewport;

private:
    void scrollBarMoved(juce::ScrollBar*, double position) override;
    void componentMovedOrResized(juce::Component&, bool moved, bool) override;
    void componentParentHierarchyChanged(juce::Component&) override;
    void componentVisibilityChanged(juce::Component&) override;
    juce::Rectangle<int> getFrameBounds() const;

    ControlBlockComponent& block;
    const juce::String title;
    CursorDragScrollBar scrollbar {false, CursorDragScrollBar::TrackClick::drag};
};

class AvaAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                      private juce::Timer,
                                      private juce::ScrollBar::Listener,
                                      private juce::AudioProcessorValueTreeState::Listener,
                                      private juce::ValueTree::Listener
{
public:
    explicit AvaAudioProcessorEditor(AvaAudioProcessor&, int routingInstanceId = 0);
    ~AvaAudioProcessorEditor() override;

    void prepareForProcessorStateRestore() { detachModuleEditorBindings(); }
    void requestStructureRestoreResync() { scheduleProcessorStateResync(); }
    // Capture the *visible* viewport position before DAW Undo replaces APVTS.
    // Scroll metadata can lag behind the viewport (e.g. thumb dragging).
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    juce::ValueTree& getRootWindowState() const;
    bool isDockOnly() const;
    int getMinimumEditorHeight() const;
    int getMaximumEditorHeight() const;
    void setDockOnly(bool dockOnly);
    void applyDockWindowSize(int width, int height);
    void applyDockVisibility();
    bool isControlManuallySelected(const juce::Component& component) const;
    void toggleControlManualSelection(juce::Component& component);
    void refreshQuickAccessControls();

private:
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    class AvaLookAndFeel;
    struct PresetsSection;
    struct FilterSection;
    class EqlCopyEditor;

    juce::Identifier controlSelectionStateKey(const juce::Component& component) const;
    enum class UnitPage { main, about, routing, slots };
    bool hasUnitPage() const noexcept { return unitPage != UnitPage::main; }
    void showUnitPage(UnitPage page);
    void layoutUnitPage(juce::Rectangle<int> bounds);
    void openRoutingInstance(int instanceId);
    void closeRoutingInstance();
    void selectFilterSection(int filterIndex);
    void refreshFilterPresetList();
    void reloadFilterPresetFromProcessor();
    void commitFilterDisplayOrderToProcessor();
    void addFilterPreset();
    void saveFilterPreset();
    bool renameFilterPreset(const juce::String& sourcePresetName, const juce::String& newPresetName);
    void setDefaultFilterPreset();
    void deleteSelectedFilterPreset();
    void copyAllEqlFilters();
    void pasteAllEqlFilters();
    void requestSvgExport();
    static bool isSvgExportGesture(const juce::ModifierKeys& modifiers);

public:
    bool isHostSlotAssigned(const juce::String& parameterId) const;
    bool handleHostSlotAssignRequest(const juce::String& parameterId);
    void showTextPrompt(const juce::String& currentText,
                        std::function<bool(const juce::String&)> onCommit,
                        juce::Rectangle<int> anchorBounds = {},
                        std::function<void()> onClose = {},
                        std::function<void()> onDismiss = {},
                        bool uppercaseInput = true,
                        juce::Slider* numericControl = nullptr,
                        std::function<double(const juce::String&)> textToValue = {},
                        std::function<juce::String()> valueToText = {},
                        std::function<void()> resetAction = {},
                        juce::Component* anchorComponent = nullptr);
    void setReturnToRoutingAction(std::function<void()> action);
    void showChoicePrompt(const juce::Rectangle<int>& anchorBounds,
                          const juce::StringArray& choices,
                          int selectedIndex,
                          std::vector<bool> itemEnabledStates,
                          juce::Justification itemJustification,
                          std::function<void(int)> onSelect,
                          std::function<void()> onClose = {},
                          std::function<void()> onDismiss = {},
                          bool commitBeforeFade = false,
                          std::vector<bool> itemActiveStates = {});
private:
    void dismissTextPrompt();
    void prepareChildPrompt();
    void closeTextPrompt();
    void ensureModuleList();
    void refreshInstanceLinkControls();
    void changeLinkDestination(int index);
    void refreshRoutingLinkControl();
    void changeRoutingLinkDestination();
    static std::function<void()> makeScrollPreservingPromptClose(AvaAudioProcessorEditor& editor,
                                                                 std::function<void()> closeHandler);
    void timerCallback() override;
    void normalizeOrderForType(int filterIndex);
    void sortFilterSections();
    void handleFilterDrag(int filterIndex, juce::Point<float> screenPosition, bool released);
    void cancelFilterDrag();
    enum class HistoryAction { undo, redo };
    void performHistoryStep(HistoryAction action);
    void switchABState();
    void captureCurrentABState();
    bool restoreABStateSnapshot(const juce::MemoryBlock& snapshot);
    void refreshABCompareButton();
    void refreshEqlFilterSectionsFromProcessor();
    void applyFilterSortOrder(const std::vector<int>& orderedIndices);
    void enforceSingleExpandedFilterSection(int preferredFilterIndex = -1);
    void restoreEditorStateFromValueTree();
    void restoreFilterDisplayOrderFromValueTree();
    void storeFilterDisplayOrderToValueTree() noexcept;
    void storeEditorStateToValueTree() noexcept;
    bool canStoreEditorUiState() const noexcept;
    void storeEditorUiState();
    void restoreEditorUiState();
    void resetFilterSectionUiState(int filterIndex);
    void updateSectionStates();
    juce::String getInstanceDisplayName() const;
    void updateInstanceHeadings();
    void setPresetsVisible(bool shouldShow);
    void updateEqlSectionStates(int activeFilterCount);
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
    void scheduleHistorySnapshot(bool notifyHost = true);
    void commitPendingHistorySnapshot(bool force = false);
    void recordPresetHistoryChange(const juce::MemoryBlock& before,
                                   const juce::MemoryBlock& after,
                                   int instanceId,
                                   const juce::MemoryBlock& eqlBefore,
                                   const juce::MemoryBlock& eqlAfter);
    bool applyEqlPresetHistoryState(int instanceId,
                                    const juce::MemoryBlock& bankState,
                                    juce::MemoryBlock& previousBank);
    bool applyHistorySnapshot(const juce::MemoryBlock& snapshot);
    void updateUndoRedoButtons();
    int getActiveFilterContentHeight() const;
    void handleEqlBackgroundClick(const juce::MouseEvent& event);
    int getActiveFilterCount() const noexcept;
    void layoutGlobalSections(juce::Rectangle<int>& bounds);
    void layoutQuickAccessStrip();
    void syncQuickAccessControls();
    void layoutCrossoverSection(juce::Rectangle<int>& bounds);
    void finalizeLayout() noexcept;
    void applyPromptVisibility();
    void layoutUnifiedBlocks(juce::Rectangle<int>& bounds);
    std::vector<ControlSearchEntry> findControls(const juce::String& query);
    void navigateToSearchResult(ControlSearchEntry result);
    void updateSearchFlash();
    void updatePinnedSearchPosition();
    void refreshPinnedRoutingStrip();
    void switchPinnedRoutingInstance(int instanceId);
    void handlePinnedRoutingDrag(int sourceId, const juce::MouseEvent& event, bool released);
    void layoutEqlModuleSections(juce::Rectangle<int>& bounds);
    juce::Rectangle<int> getModuleViewportBounds() const;
    void clearHostSlot(int slotIndex);
    void moveHostSlotAssignment(int slotIndex, int direction);
    void refreshHostSlotButtons();
    void armHostSlotMove(int slotIndex);
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
    std::unique_ptr<juce::Component> windowDragHandle;
    std::unique_ptr<LocalChoiceControl> unitNumberControl;
    std::unique_ptr<LocalChoiceControl> sendControl;
    std::unique_ptr<LocalChoiceControl> receiveControl;
    std::unique_ptr<LocalChoiceControl> linksCountControl;
    std::array<std::unique_ptr<LocalChoiceControl>, 12> linkToControls;
    int linkDestinationCount = 1;
    std::unique_ptr<LocalChoiceControl> linkInstanceControl;
    std::vector<uint64_t> routingPeerIds;
    juce::StringArray routingPeerNames;
    uint64_t focusedRoutingPeerId = 0;
    bool focusedRoutingAll = false;
    std::array<std::vector<uint64_t>, 12> linkPeerIds;
    std::array<juce::StringArray, 12> linkPeerNames;
    std::unique_ptr<juce::Component> crossoverEditor;
    std::unique_ptr<PushControl> addFilterButton;
    std::unique_ptr<PushControl> deleteFilterButton;
    std::unique_ptr<PushControl> copyFilterButton;
    std::unique_ptr<PushControl> pasteFilterButton;
    std::unique_ptr<PresetsSection> presetsSection;
    bool restoringEditorUiState = true;
    bool layingOutEditor = false;
    std::unique_ptr<ParameterControl> globalBypassButton;
    std::unique_ptr<ParameterControl> bypassCompensationControl;
    std::unique_ptr<PushControl> undoButton;
    std::unique_ptr<PushControl> redoButton;
    std::unique_ptr<LocalChoiceControl> abControl;
    std::unique_ptr<PushControl> abCopyButton;
    std::unique_ptr<PushControl> unitCopyButton;
    std::unique_ptr<PushControl> unitPasteButton;
    std::unique_ptr<LocalChoiceControl> unitZoomControl;
    std::unique_ptr<LocalChoiceControl> unitWidthControl;
    std::unique_ptr<LocalChoiceControl> unitDockSizeControl;
    std::unique_ptr<SquareTextLabel> unitSampleRateLabel;
    std::unique_ptr<PushControl> sortFiltersButton;
    std::array<std::unique_ptr<BoxTextButton>, AvaAudioProcessor::hostAutomationSlotCount> hostSlotNameFields;
    std::array<std::unique_ptr<BoxTextButton>, AvaAudioProcessor::hostAutomationSlotCount> hostSlotButtons;
    std::array<std::unique_ptr<FilterSection>, EqlModuleProcessor::maxFilterCount> filterSections;
    juce::AudioProcessorValueTreeState* boundEqlState = nullptr;
    BackgroundClickComponent slotsContent;
    UiScrollViewport slotsViewport;
    juce::Rectangle<int> getMainScrollBarBounds() const;
    void syncMainScrollBar();
    void scrollBarMoved(juce::ScrollBar*, double) override;
    UiScrollViewport filterViewport;
    CursorDragScrollBar mainScrollBar {true, CursorDragScrollBar::TrackClick::ignore};
    int mainScrollPosition = 0;
    BackgroundClickComponent moduleContent;
    QuickAccessStrip quickAccessStrip;
    SquareToggleButton dockSortToggle {"dock-sort"};
    SquareToggleButton dockCollapseToggle {"dock-collapse"};
    SquareToggleButton dockVerticalToggle {"dock-vertical"};
    StatusIndicator dockClipIndicator;
    juce::Component unitSyncPlate;
    StatusIndicator unitSendIndicator;
    StatusIndicator unitReceiveIndicator;
    SquareToggleButton unitSyncPauseToggle {"unit-sync-pause"};
    struct UnitNumberLabel final : juce::Label
    {
        std::function<void(juce::Point<int>)> onDrop;
        bool dragging = false;
        void mouseDown(const juce::MouseEvent&) override { dragging = false; }
        void mouseDrag(const juce::MouseEvent& event) override
        {
            dragging |= event.mods.isLeftButtonDown() && event.mouseWasDraggedSinceMouseDown();
        }
        void mouseUp(const juce::MouseEvent& event) override
        {
            if (std::exchange(dragging, false) && onDrop != nullptr) onDrop(event.getScreenPosition());
        }
    } unitNumberLabel;
    DockSortOverlay dockSortOverlay;
    int dockDragSource = -1;
    int dockDropTarget = -1;
    void dragDockControl(const juce::MouseEvent&, int phase);
    UiScrollViewport quickAccessViewport;
    DockSlotContent quickAccessContent;
    struct QuickAccessItem
    {
        juce::String key;
        juce::Component::SafePointer<juce::Component> source;
        // Hold the routing processor alive until its APVTS attachments detach.
        std::shared_ptr<AvaAudioProcessor> instanceHandle;
        std::unique_ptr<juce::Component> control;
        // Owned separately, but parented into the mirrored ParameterField.
        // Destruction order removes the accessory before its parent field.
        std::unique_ptr<juce::Component> accessory;
    };
    std::vector<QuickAccessItem> quickAccessItems;
    juce::String quickAccessOrderCache;
    std::unique_ptr<juce::TooltipWindow> dockTooltipWindow;
    BackgroundClickComponent unitContent;
    std::unique_ptr<ControlSearchPanel> controlSearchPanel;
    HorizontalControlViewport pinnedRoutingViewport;
    juce::Component pinnedRoutingContent;
    std::vector<std::unique_ptr<BoxTextButton>> pinnedRoutingButtons;
    std::vector<int> pinnedRoutingButtonIds;
    int pinnedRoutingDragSourceId = 0;
    juce::String pinnedRoutingSignature;
    bool pinnedRoutingVisible = false;

    bool updatingPinnedSearchPosition = false;
    juce::Component::SafePointer<juce::Component> searchFlashTarget;
    juce::String searchFlashBlockTitle;
    uint32_t searchFlashStarted = 0;
    bool searchFlashOn = false;
    BackgroundClickComponent unitMainContent;
    HeaderControlRow unitMainRow {unitMainContent};
    ControlBlockComponent unitSyncContent;
    BackgroundClickComponent mainContent;
    HeaderControlRow instanceMainRow {mainContent};
    std::unique_ptr<ModuleList> moduleList;
    int moduleListBand = -1;
    BackgroundClickComponent instanceContent;
    BackgroundClickComponent eqlContent;
    BackgroundClickComponent filterContent;
    std::unique_ptr<juce::MouseListener> svgExportClickListener;
    std::unique_ptr<juce::MouseListener> hostStateActionTracker;
    std::unique_ptr<juce::MouseListener> windowMoveGesture;
    std::unique_ptr<PushControl> aboutControl;
    std::unique_ptr<PushControl> routingControl;
    std::unique_ptr<PushControl> slotsControl;
    std::unique_ptr<LocalChoiceControl> pinRoutingControl;
    std::unique_ptr<LocalChoiceControl> unitSearchControl;
    std::unique_ptr<juce::Component> aboutPanel;
    std::vector<std::unique_ptr<juce::Component>> suspendedPromptOverlays;
    std::unique_ptr<juce::Component> textPromptOverlay;
    std::unique_ptr<RoutingPanel> routingPanel;
    std::shared_ptr<AvaAudioProcessor> activeInstanceProcessor;
    std::unique_ptr<AvaAudioProcessorEditor> activeInstanceEditor;
    std::function<void()> returnToRoutingAction;
    int activeInstanceId = 0;
    int routingInstanceId = 0;
    std::unique_ptr<OscPanel> oscPanel;
    std::unique_ptr<juce::Component> blockChainModuleEditor;
    std::unique_ptr<juce::Component> dynModuleEditor;
    std::map<int, std::unique_ptr<juce::Component>> copiedModuleEditors;
    std::map<int, std::unique_ptr<EqlCopyEditor>> eqlCopyEditors;
    // Newly added MODULES entries must also be shown on the underlying ALL page.
    // The popup obscures that page, so reveal the new entry when it closes.
    AvaAudioProcessor::ActiveModule displayedModule = AvaAudioProcessor::ActiveModule::none;
    UnitPage unitPage = UnitPage::main;
    bool updatingEditorResizeLimits = false;
    // Keep the expanded window height independently of the compact dock size.
    int expandedHeightBeforeDock = 0;
    std::vector<int> filterDisplayOrder;
    int filterDragSourceIndex = -1;
    bool suppressFilterSectionValueChangeHandlers = false;
    bool initialKeyboardFocusPending = true;
    bool suppressProcessorStateResync = false;
    bool suppressEditorSizeStateSave = true;
    std::atomic<bool> suppressHistorySnapshots { false };
    bool shellStateListenerRegistered = false;
    std::atomic<bool> processorStateResyncPending { false };
    std::atomic<bool> moduleControlsRefreshPending { false };
    std::atomic<bool> moduleControlsRebindPending { false };
    juce::MemoryBlock& committedHistorySnapshot = audioProcessor.getHistoryState().committed;
    std::vector<AvaAudioProcessor::HistoryState::Entry>& undoHistory = audioProcessor.getHistoryState().undo;
    std::vector<AvaAudioProcessor::HistoryState::Entry>& redoHistory = audioProcessor.getHistoryState().redo;
    std::atomic<bool>& pendingHistorySnapshot = audioProcessor.getHistoryState().pending;
    std::atomic<uint32_t>& lastHistoryChangeTimeMs = audioProcessor.getHistoryState().lastChange;
    uint64_t observedStateRevision = 0;
    uint32_t lastClipIndicatorTimeMs = 0;
    uint32_t lastClipEventCount = 0;
    bool clipIndicatorShown = false;

    int hostSlotMoveSourceIndex = -1;

    int getFilterIndexForOrderPosition(int orderIndex) const noexcept;
    int getFilterOrderPositionForIndex(int filterIndex) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AvaAudioProcessorEditor)
};
