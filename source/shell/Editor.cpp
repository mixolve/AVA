#include "EqlCopyEditor.h"
#include "ChoiceControl.h"
#include "Editor.h"
#include "ModuleList.h"
#include "InstanceLink.h"
#include "EditorStateSchema.h"
#include "SvgExport.h"
#include "WindowMoveGesture.h"
#include "LookAndFeel.h"
#include "FilterSection.h"
#include "PresetSection.h"
#include "EditorSetup.h"
#include "../routing/Panel.h"
#include "../crossover/Editor.h"
#include "../routing/State.h"
#include "OscPanel.h"
#include "Prompt.h"
#include "../modules/eql/Processor.h"

#include <algorithm>
#include <cmath>

namespace
{
// Component mouse listeners also receive the synthetic right-click that macOS
// creates from Ctrl+click, so modifier gestures placed on buttons stay reliable.
class SidebarFocusHandle final : public juce::Component
{
public:
    // Only the top margin moves the window; the other margins pass through.
    bool hitTest(int, int y) override
    {
        return y >= 0 && y < 2 * getUiGap(*this);
    }
    void mouseDown(const juce::MouseEvent&) override
    {
        if (auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>())
            editor->grabKeyboardFocus();
    }
};

// Observe actual edit gestures across all nested controls, including routing
// instances. Mouse-up is deferred until component handlers have committed
// their values (many push/toggle controls act on release).
class HostStateActionTracker final : public juce::MouseListener
{
public:
    HostStateActionTracker(AvaAudioProcessorEditor& editorIn, AvaAudioProcessor& processorIn)
        : editor(editorIn), processor(processorIn.getRoutingOwner()) {}
    ~HostStateActionTracker() override
    {
        if (gestureId != 0) processor.endHostStateEdit(gestureId);
        if (deferredEndId != 0) processor.endHostStateEdit(deferredEndId);
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        gestureId = processor.beginHostStateEdit();
        deferredEndId = 0;
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        const auto finished = std::exchange(gestureId, uint64_t { 0 });
        if (finished == 0) return;
        deferredEndId = finished;
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(&editor), finished]
            {
                if (safe != nullptr)
                    static_cast<AvaAudioProcessor*>(safe->getAudioProcessor())->getRoutingOwner()
                        .endHostStateEdit(finished);
            });
    }
private:
    AvaAudioProcessorEditor& editor;
    AvaAudioProcessor& processor;
    uint64_t gestureId = 0;
    uint64_t deferredEndId = 0;
};

class ModifierClickAction final : public juce::MouseListener
{
public:
    ModifierClickAction(std::function<bool(const juce::ModifierKeys&)> matchesIn,
                        std::function<void()> actionIn)
        : matches(std::move(matchesIn)), action(std::move(actionIn)) {}

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (action != nullptr && matches != nullptr && matches(event.mods))
            action();
    }

private:
    std::function<bool(const juce::ModifierKeys&)> matches;
    std::function<void()> action;
};

juce::MemoryBlock makeNoModuleSnapshotFrom(const juce::MemoryBlock& sourceSnapshot, const AvaAudioProcessor& processor)
{
    juce::MemoryBlock noModuleSnapshot;
    auto stateXml = AvaAudioProcessor::getXmlFromBinary(sourceSnapshot.getData(),
                                                       static_cast<int>(sourceSnapshot.getSize()));

    if (stateXml == nullptr)
        return noModuleSnapshot;

    auto state = juce::ValueTree::fromXml(*stateXml);
    processor.removeModuleStateProperties(state);

    if (auto noModuleXml = state.createXml())
        AvaAudioProcessor::copyXmlToBinary(*noModuleXml, noModuleSnapshot);

    return noModuleSnapshot;
}

}

void AvaAudioProcessorEditor::requestSvgExport()
{
    auto* editor = this;
    while (auto* parent = editor->findParentComponentOfClass<AvaAudioProcessorEditor>())
        editor = parent;
    svg_export::chooseFile(*editor);
}

bool AvaAudioProcessorEditor::isSvgExportGesture(const juce::ModifierKeys& modifiers)
{
    return modifiers.isCtrlDown() && modifiers.isShiftDown()
#if JUCE_MAC
        && !modifiers.isCommandDown()
#endif
        ;
}

AvaAudioProcessorEditor::AvaAudioProcessorEditor(AvaAudioProcessor& processorToEdit,
                                                 const int routingInstanceIdIn)
    : AudioProcessorEditor(&processorToEdit),
      audioProcessor(processorToEdit),
      valueTreeState(processorToEdit.getValueTreeState()),
      lookAndFeel(std::make_unique<AvaLookAndFeel>()),
      routingInstanceId(routingInstanceIdIn)
{
    setName("editor");
    const auto restoredInstanceId = static_cast<int>(valueTreeState.state.getProperty(shell_editor_ui::key("active_instance"), 0));
    audioProcessor.setContentEditor(this);

    setLookAndFeel(lookAndFeel.get());
    // A standard JUCE tooltip is shown only while the pointer is over the
    // read-only instance-number badge of a dock tile.
    dockTooltipWindow = std::make_unique<juce::TooltipWindow>(this, 550);
    setOpaque(true);
   #if JUCE_MAC
    // Composite dirty regions into one backing image before handing the frame
    // to Core Animation. Per-control clips/masks otherwise stall its render
    // queue during scrolling even when JUCE's paint calls complete quickly.
    setBufferedToImage(true);
   #endif
    setResizable(false, false);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);
    routingPanel = std::make_unique<RoutingPanel>(audioProcessor.getRoutingOwner());
    routingPanel->setOnOpenRoot([this]
    {
        // When an INSTANCE editor covers the root, selecting the root node
        // from its embedded ROUTING block returns to the root editor.
        if (returnToRoutingAction != nullptr) returnToRoutingAction();
    });
    routingPanel->setOnOpenInstance([this] (const int id)
    {
        if (audioProcessor.isRoutingInstance())
        {
            // Switch instances through the owner, asynchronously: replacing
            // the current instance editor synchronously would destroy this
            // routing callback while it is executing.
            if (auto* owner = audioProcessor.getRoutingOwner().getContentEditor())
                juce::MessageManager::callAsync(
                    [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(owner), id]
                    {
                        if (safe != nullptr) safe->openRoutingInstance(id);
                    });
        }
        else openRoutingInstance(id);
    });
    routingPanel->setOnTopologyChanged([this]
    {
        if (audioProcessor.isRoutingInstance())
        {
            // A nested editor uses the owner's routing graph. Commit changes
            // on the owner *after* this callback returns, in case the edited
            // instance itself was removed from the graph.
            if (auto* owner = audioProcessor.getRoutingOwner().getContentEditor())
                juce::MessageManager::callAsync(
                    [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(owner)]
                    {
                        if (safe == nullptr) return;
                        const auto topology = ava::routing::readState(safe->valueTreeState.state);
                        const auto selected = safe->activeInstanceId;
                        if (selected != 0
                            && (selected == topology.rootInstanceId
                                || std::none_of(topology.nodes.begin(), topology.nodes.end(),
                                    [selected] (const auto& node) { return node.id == selected; })))
                            safe->closeRoutingInstance();
                        safe->audioProcessor.synchronizeRouting();
                    });
            return;
        }
        const auto topology = ava::routing::readState(valueTreeState.state);
        if (activeInstanceId != 0
            && (activeInstanceId == topology.rootInstanceId
                || std::none_of(topology.nodes.begin(), topology.nodes.end(),
                                [this] (const auto& node) { return node.id == activeInstanceId; })))
            closeRoutingInstance();
        audioProcessor.synchronizeRouting();
    });
    routingPanel->setOnRenameRequest([this] (int id, const juce::String& currentName, juce::Component& anchor)
    {
        showTextPrompt(currentName,
            [this, id] (const juce::String& name) { return ava::routing::renameInstance(audioProcessor.getRoutingOwner().getValueTreeState().state, id, name); },
            getLocalArea(&anchor, anchor.getLocalBounds()), {}, {}, true);
        if (auto* prompt = dynamic_cast<PromptComponent*>(textPromptOverlay.get())) prompt->followAnchor(anchor);
    });
    // Every routing INSTANCE presents the same UNIT. Persist its collapsed
    // sections on the routing owner, not on an individual instance APVTS;
    // switching pages must not swap UNIT expansion preferences.
    auto& unitState = audioProcessor.getRoutingOwner().getValueTreeState();
    unitMainContent.collapseBlockInitially("MAIN");
    unitMainContent.bindCollapseState(unitState, "shell.unit.main");
    unitContent.addAndMakeVisible(unitMainContent);
    // ROUTING is a collapsible nested UNIT block, not a separate page.
    unitRoutingContent.collapseBlockInitially("ROUTING");
    unitRoutingContent.bindCollapseState(unitState, "shell.unit.routing");
    unitRoutingContent.addAndMakeVisible(*routingPanel);
    unitContent.addAndMakeVisible(unitRoutingContent);
    OscPanel::Actions oscActions;
    oscActions.getSettings = [this] { return audioProcessor.getOscSettings(); };
    oscActions.setSettings = [this] (const OscSettings& settings)
    {
        return audioProcessor.setOscSettings(settings);
    };
    oscActions.isInputPortBusy = [this] { return audioProcessor.isOscInputPortBusy(); };
    oscPanel = std::make_unique<OscPanel>(std::move(oscActions));
    unitContent.addAndMakeVisible(*oscPanel);
    // The instance LINK control stays in INSTANCE MAIN, not the UNIT navigation.
    unitSyncContent.collapseBlockInitially("SYNC");
    unitSyncContent.bindCollapseState(unitState, "shell.unit.sync");
    unitContent.addAndMakeVisible(unitSyncContent);
    unitContent.addAndMakeVisible(slotsContent);
    eqlContent.setName("eql-page");
    eqlContent.onBackgroundClick = [this](const juce::MouseEvent& event) { handleEqlBackgroundClick(event); };
    filterContent.onBackgroundClick = eqlContent.onBackgroundClick;
    eqlContent.setOpaque(false);
    eqlContent.addAndMakeVisible(filterContent);
    moduleContent.onBackgroundClick = eqlContent.onBackgroundClick;
    mainContent.onBackgroundClick = eqlContent.onBackgroundClick;
    unitRoutingContent.onBackgroundClick = eqlContent.onBackgroundClick;
    unitMainContent.onBackgroundClick = eqlContent.onBackgroundClick;
    instanceContent.onBackgroundClick = eqlContent.onBackgroundClick;
    slotsContent.collapseBlockInitially("SLOTS");
    slotsContent.bindCollapseState(unitState, "shell.unit.slots");
    // Only UNIT, INSTANCE and MODULES start expanded. Stored user choices
    // take precedence over these defaults, as with other collapse states.
    unitContent.bindCollapseState(unitState, "shell.unit");
    mainContent.collapseBlockInitially("MAIN");
    mainContent.bindCollapseState(valueTreeState, "shell.main");
    eqlContent.collapseBlockInitially("EQL");
    instanceContent.bindCollapseState(valueTreeState, "shell.instance");
    oscPanel->collapseBlockInitially("OSC");
    oscPanel->bindCollapseState(unitState, "shell.unit.osc");
    moduleContent.addAndMakeVisible(unitContent);
    moduleContent.addAndMakeVisible(instanceContent);
    instanceContent.addAndMakeVisible(mainContent);
    instanceContent.addAndMakeVisible(modulesContent);
    modulesContent.bindCollapseState(valueTreeState, "shell.instance.modules");
    instanceContent.addAndMakeVisible(eqlContent);
    controlSearchPanel = std::make_unique<ControlSearchPanel>();
    // Range indicators are painted in the editor's final overlay pass; mark
    // this fixed panel as an occluder or sliders scrolling beneath it will
    // draw their handles on top of the search header.
    controlSearchPanel->getProperties().set("foregroundOccluder", true);
    controlSearchPanel->findMatches = [this](const juce::String& query) { return findControls(query); };
    controlSearchPanel->onChoose = [this](ControlSearchEntry result) { navigateToSearchResult(std::move(result)); };
    controlSearchPanel->onDisplayChanged = [this]
    {
        updatePinnedSearchPosition();
        // The UNIT-owned PIN ROUTING strip sits in the root editor, so a
        // nested search field expanding must move that overlay as well.
        if (routingInstanceId != 0)
            if (auto* root = audioProcessor.getRoutingOwner().getContentEditor())
                root->updatePinnedSearchPosition();
    };
    pinnedRoutingViewport.setOpaque(false);
    pinnedRoutingViewport.setViewedComponent(&pinnedRoutingContent, false);
    pinnedRoutingViewport.setScrollBarsShown(false, false, true, true);
    pinnedRoutingViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    pinnedRoutingViewport.setWantsKeyboardFocus(false);
    pinnedRoutingViewport.getProperties().set("foregroundOccluder", true);
    addChildComponent(pinnedRoutingViewport);
    // The quick-access row is a fixed, horizontally scrollable control strip.
    // The selected tiles are native controls bound to the source parameters.
    addAndMakeVisible(quickAccessStrip);
    quickAccessStrip.addAndMakeVisible(quickAccessViewport);
    quickAccessViewport.setViewedComponent(&quickAccessContent, false);
    quickAccessViewport.setScrollBarsShown(false, false, true, true);
    quickAccessViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    quickAccessViewport.setWantsKeyboardFocus(false);
    // Marking remains visible at the source in the scrolling viewport;
    // mirrored dock controls use their ordinary neutral frame and rail.
    quickAccessContent.getProperties().set("suppressManualSelectionHighlight", true);
    quickAccessStrip.getProperties().set("foregroundOccluder", true);
    quickAccessContent.addMouseListener(&quickAccessStrip, true);
    quickAccessStrip.onScroll = [this](const juce::MouseWheelDetails& wheel)
    {
        const auto contentWidth = quickAccessContent.getWidth();
        const auto maxX = juce::jmax(0, contentWidth - quickAccessViewport.getWidth());
        const auto delta = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? -wheel.deltaX : -wheel.deltaY;
        const auto step = juce::roundToInt(delta * (wheel.isSmooth ? 140.0f : 70.0f));
        const bool vertical = isDockOnly() && bool(getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false));
        // In the rotated dock, horizontal swipes must not scroll the Y axis.
        if (vertical && std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) return;
        if (vertical)
            quickAccessViewport.setViewPosition(0, juce::jlimit(0, juce::jmax(0, quickAccessContent.getHeight() - quickAccessViewport.getHeight()), quickAccessViewport.getViewPositionY() + step));
        else quickAccessViewport.setViewPosition(juce::jlimit(0, maxX, quickAccessViewport.getViewPositionX() + step), 0);
    };
    dockSortToggle.onClick = [this]
    {
        dockDragSource = dockDropTarget = -1;
        for (auto& item : quickAccessItems)
        {
            item.control->getProperties().remove("arrangementDragging");
            item.control->repaint();
        }
        dockSortOverlay.setVisible(dockSortToggle.getToggleState());
        dockSortOverlay.repaint();
    };
    dockSortOverlay.onGesture = [this](const juce::MouseEvent& event, int phase) { dragDockControl(event, phase); };
    dockSortOverlay.onScroll = quickAccessStrip.onScroll;
    dockCollapseToggle.setName("dock-collapse");
    dockCollapseToggle.onClick = [this] { setDockOnly(dockCollapseToggle.getToggleState()); };
    addAndMakeVisible(dockSortToggle);
    addAndMakeVisible(dockCollapseToggle);
    dockVerticalToggle.setName("dock-vertical");
    dockVerticalToggle.onClick = [this]
    {
        auto* root = this;
        while (auto* parent = root->findParentComponentOfClass<AvaAudioProcessorEditor>()) root = parent;
        if (!root->isDockOnly())
        {
            dockVerticalToggle.setToggleState(false, juce::dontSendNotification);
            return;
        }
        const bool vertical = dockVerticalToggle.getToggleState();
        root->getRootWindowState().setProperty(shell_editor_ui::key("dock_vertical"), vertical, nullptr);
        const juce::ScopedValueSetter<bool> guard(root->suppressEditorSizeStateSave, true);
        const auto width = getEditorWidth(*root);
        root->applyDockWindowSize(width, root->getMinimumEditorHeight());
        root->resized();
    };
    addAndMakeVisible(dockVerticalToggle);
    dockClipIndicator.setName("dock-clip");
    addAndMakeVisible(dockClipIndicator);
    unitSendIndicator.setName("unit-send");
    unitReceiveIndicator.setName("unit-receive");
    unitSendIndicator.setTooltip("SEND: sending changes to linked units");
    unitReceiveIndicator.setTooltip("RECEIVE: receiving changes from linked units");
    unitSendIndicator.setInterceptsMouseClicks(true, false);
    unitReceiveIndicator.setInterceptsMouseClicks(true, false);
    unitNumberLabel.setFont(makeUiFont());
    unitNumberLabel.setColour(juce::Label::textColourId, uiWhite);
    unitNumberLabel.setJustificationType(juce::Justification::centred);
    unitNumberLabel.setBorderSize({});
    unitNumberLabel.setInterceptsMouseClicks(false, false);
    for (auto* component : std::array<juce::Component*, 3> {
            &unitNumberLabel, &unitSendIndicator, &unitReceiveIndicator })
    {
        component->getProperties().set("foregroundOccluder", true);
        addChildComponent(*component);
    }
    addChildComponent(dockSortOverlay);
    // The search is a fixed overlay, not a child of the scrolling canvas.
    // Its space is still reserved by layoutUnifiedBlocks().
    addAndMakeVisible(*controlSearchPanel);
    filterViewport.setViewedComponent(&moduleContent, false);
    filterViewport.onViewChanged = [this]
    {
        updatePinnedSearchPosition();
        syncMainScrollBar();
        // Keep the persisted scroll coordinate current for viewport wheel,
        // scrollbar thumb and trackpad gestures. Never persist a transient
        // clamp-to-zero while the page is being rebuilt on DAW Undo.
        if (canStoreEditorUiState() && !layingOutEditor)
        {
            const auto key = shell_editor_ui::key("scroll.eql");
            const int y = filterViewport.getViewPositionY();
            if (int(valueTreeState.state.getProperty(key, 0)) != y)
                valueTreeState.state.setProperty(key, y, nullptr);
        }
    };
    filterViewport.onBackgroundClick = [this] { grabKeyboardFocus(); };
    filterViewport.setName("filter-viewport");
    filterContent.setName("filter-content");
    if (auto* holder = eqlContent.getParentComponent()) holder->setName("filter-scroll");
    filterViewport.setScrollBarsShown(false, false, true, false);
    filterViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    filterViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(filterViewport);
    mainScrollBar.setAutoHide(false);
    mainScrollBar.setColour(juce::ScrollBar::backgroundColourId, uiGreyDark);
    mainScrollBar.setColour(juce::ScrollBar::thumbColourId, uiGreyLight);
    mainScrollBar.setSingleStepSize(uiGap);
    mainScrollBar.addListener(this);
    addAndMakeVisible(mainScrollBar);

    sendControl = std::make_unique<LocalChoiceControl>(
        "SEND", juce::StringArray { "OFF", "ON" }, 0);
    sendControl->setBottomIndicatorsVisible(false, false);
    sendControl->onValueChanged = [this]
    {
        auto& root = audioProcessor.getRoutingOwner();
        InstanceLinkRegistry::instance().setSendEnabled(root.getLinkSessionId(),
            sendControl->getSelectedChoiceIndex() == 1);
        root.notifyHostOfStateChange();
    };
    unitSyncContent.addAndMakeVisible(*sendControl);

    receiveControl = std::make_unique<LocalChoiceControl>(
        "RECEIVE", juce::StringArray { "OFF", "ON" }, 0);
    receiveControl->setBottomIndicatorsVisible(false, false);
    receiveControl->onValueChanged = [this]
    {
        const auto source = audioProcessor.getRoutingOwner().getLinkSessionId();
        InstanceLinkRegistry::instance().setReceiveEnabled(
            source, receiveControl->getSelectedChoiceIndex() == 1);
        audioProcessor.getRoutingOwner().notifyHostOfStateChange();
    };
    unitSyncContent.addAndMakeVisible(*receiveControl);

    juce::StringArray linkCounts;
    for (int count = 1; count <= InstanceLinkRegistry::maxUnitLinks; ++count)
        linkCounts.add(juce::String(count));
    linksCountControl = std::make_unique<LocalChoiceControl>("LINKS", linkCounts, 0);
    linksCountControl->setBottomIndicatorsVisible(false, false);
    linksCountControl->onValueChanged = [this]
    {
        auto& root = audioProcessor.getRoutingOwner();
        InstanceLinkRegistry::instance().setUnitLinkCount(root.getLinkSessionId(),
            linksCountControl->getSelectedChoiceIndex() + 1);
        root.notifyHostOfStateChange();
        refreshInstanceLinkControls();
    };
    unitSyncContent.addAndMakeVisible(*linksCountControl);
    for (size_t index = 0; index < linkToControls.size(); ++index)
    {
        auto& control = linkToControls[index];
        control = std::make_unique<LocalChoiceControl>("LINK\nTO", juce::StringArray { "NONE" }, 0);
        control->setBottomIndicatorsVisible(false, false);
        control->onValueChanged = [this, index] { changeLinkDestination(int(index)); };
        unitSyncContent.addChildComponent(*control);
    }

    // Internal ROUTING nodes are different from host UNITs. This independent
    // selector always addresses other INSTANCEs inside the same root AVA.
    linkInstanceControl = std::make_unique<LocalChoiceControl>(
        "INSTANCE\nLINK", juce::StringArray { "NONE" }, 0);
    linkInstanceControl->setBottomIndicatorsVisible(false, false);
    linkInstanceControl->onValueChanged = [this] { changeRoutingLinkDestination(); };
    mainContent.addAndMakeVisible(*linkInstanceControl);
    refreshInstanceLinkControls();

    displayedModule = audioProcessor.getActiveModule();

    setupShellControls();

    setupPresetControls();

    filterDisplayOrder.reserve(EqlModuleProcessor::maxFilterCount);
    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
        filterDisplayOrder.push_back(filterIndex);

    if (auto* initialEqlProcessor = audioProcessor.getEqlModuleProcessor())
        setupEqlControls(initialEqlProcessor->getValueTreeState());

    restoreEditorStateFromValueTree();

    aboutPanel = makeAboutPanel(shell_setup_support::getMixolveInfoMarkdown(),
        [this] { showAllSection(); });
    aboutPanel->setName("about");
    addChildComponent(*aboutPanel);
    aboutControl = std::make_unique<LocalChoiceControl>("ABOUT", juce::StringArray { "OFF", "ON" }, 0);
    aboutControl->setName("info");
    windowDragHandle = std::make_unique<SidebarFocusHandle>();
    windowDragHandle->setName("window-drag-handle");
    windowDragHandle->setWantsKeyboardFocus(false);
    addAndMakeVisible(*windowDragHandle);
    windowMoveGesture = std::make_unique<WindowMoveGesture>(*windowDragHandle, *this);
    aboutControl->setBottomIndicatorsVisible(false, false);
    svgExportClickListener = std::make_unique<ModifierClickAction>(
        [] (const juce::ModifierKeys& modifiers) { return isSvgExportGesture(modifiers); },
        [this] { requestSvgExport(); });
    aboutControl->addMouseListener(svgExportClickListener.get(), true);
    aboutControl->onValueChanged = [this]
    {
        if (isSvgExportGesture(juce::ModifierKeys::getCurrentModifiersRealtime()))
        {
            aboutControl->setSelectedChoiceIndex(aboutExpanded ? 1 : 0, false);
            return;
        }
        toggleAboutSection();
    };
    unitMainContent.addAndMakeVisible(*aboutControl);

    // Native UNIT MAIN controls use the same Ctrl-click quick-access marking
    // as all module controls (no special treatment for action/toggle controls).

    startTimerHz(60);
    registerParameterListeners();

    updateSectionStates();
    setResizeLimits(getEditorWidth(*this), getMinimumEditorHeight(),
                    getEditorWidth(*this), getMaximumEditorHeight());
    getConstrainer()->setFixedAspectRatio(0.0);
    const int previousHeight = static_cast<int>(valueTreeState.state.getProperty(
        AvaAudioProcessor::editorHeightStateKey, getEditorHeight(*this)));
    const juce::Point<int> editorSize { getEditorWidth(*this),
        juce::jlimit(getMinimumEditorHeight(), getMaximumEditorHeight(), previousHeight) };
    setSize(editorSize.x, editorSize.y);
    audioProcessor.setLastEditorSize(editorSize.x, isDockOnly()
        ? juce::jlimit(getEditorWidth(*this), maximumEditorHeight, previousHeight) : editorSize.y);

    restoreEditorUiState();
    restoringEditorUiState = false;
    suppressEditorSizeStateSave = false;

    // Do not resnapshot the entire DSP model on every GUI open. The baseline
    // and both A/B slots are owned by the processor and outlive this editor.
    // Only capture after first creation or if parameters changed while closed.
    auto& historyState = audioProcessor.getHistoryState();
    const bool needsHistoryBaseline = !audioProcessor.isRoutingInstance()
        && (committedHistorySnapshot.isEmpty()
            || historyState.committedRevision != audioProcessor.getStateRevision());
    const bool needsA = !audioProcessor.isABCompareSnapshotValid(0);
    const bool needsB = !audioProcessor.isABCompareSnapshotValid(1);
    if (needsHistoryBaseline || needsA || needsB)
    {
        juce::MemoryBlock initialHistory;
        audioProcessor.getStateInformationForABCompareSnapshot(initialHistory);
        if (needsHistoryBaseline)
        {
            committedHistorySnapshot = initialHistory;
            historyState.committedRevision = audioProcessor.getStateRevision();
        }
        // Missing A/B defaults are UI initialization, not a user action.
        // They must not generate nonParameterStateChanged events in the host.
        if (needsA)
            audioProcessor.setABCompareSnapshot(0, initialHistory, false);
        if (needsB)
            audioProcessor.setABCompareSnapshot(1,
                makeNoModuleSnapshotFrom(initialHistory, audioProcessor), false);
    }

    if (!audioProcessor.isABCompareSnapshotValid(audioProcessor.getABCompareActiveSlot()))
        audioProcessor.setABCompareActiveSlot(0, false);

    // The editor starts with a baseline matching the current processor. A
    // zero revision observer would schedule a redundant full snapshot on the
    // very first timer tick, even without any intervening audio-state edit.
    observedStateRevision = audioProcessor.getStateRevision();
    updateUndoRedoButtons();
    if (routingInstanceId == 0)
    {
        hostStateActionTracker = std::make_unique<HostStateActionTracker>(*this, audioProcessor);
        addMouseListener(hostStateActionTracker.get(), true);
        openRoutingInstance(restoredInstanceId);
    }
}

juce::ValueTree& AvaAudioProcessorEditor::getRootWindowState() const
{
    auto* root = this;
    while (auto* parent = root->findParentComponentOfClass<AvaAudioProcessorEditor>())
        root = parent;

    // Nested INSTANCE editors are constructed before being attached to the
    // root component. Resolve UNIT-wide layout preferences from the owner
    // even during construction, so the page does not change geometry when
    // it is attached (notably when PIN ROUTING is enabled).
    if (root == this && routingInstanceId != 0)
        if (auto* ownerEditor = audioProcessor.getRoutingOwner().getContentEditor())
            if (ownerEditor != this)
                return ownerEditor->getRootWindowState();

    return root->valueTreeState.state;
}

void AvaAudioProcessorEditor::refreshInstanceLinkControls()
{
    if (sendControl == nullptr || receiveControl == nullptr || linksCountControl == nullptr) return;
    auto& registry = InstanceLinkRegistry::instance();
    const auto source = audioProcessor.getRoutingOwner().getLinkSessionId();
    sendControl->setSelectedChoiceIndex(registry.isSendEnabled(source) ? 1 : 0, false);
    receiveControl->setSelectedChoiceIndex(registry.isReceiveEnabled(source) ? 1 : 0, false);
    const auto traffic = registry.activity(source);
    const auto now = juce::Time::getMillisecondCounter();
    constexpr uint32_t activityReleaseMs = 100;
    unitSendIndicator.setActive(traffic.sentAt != 0 && now - traffic.sentAt < activityReleaseMs);
    unitReceiveIndicator.setActive(traffic.receivedAt != 0 && now - traffic.receivedAt < activityReleaseMs);
    const auto peers = registry.peers(source);
    const auto targets = registry.unitLinkTargets(source);
    const auto count = registry.unitLinkCount(source);
    const bool countChanged = count != linkDestinationCount;
    linkDestinationCount = count;
    linksCountControl->setSelectedChoiceIndex(count - 1, false);
    for (int row = 0; row < count; ++row)
    {
        const auto index = size_t(row);
        auto& control = *linkToControls[index];
        juce::StringArray names { "NONE" };
        std::vector<uint64_t> ids { 0 };
        int selected = 0;
        const auto ownTarget = targets[row];
        for (const auto& peer : peers)
        {
            bool usedElsewhere = false;
            for (int other = 0; other < count; ++other)
                if (other != row && targets[other] == peer.uuid) usedElsewhere = true;
            if (usedElsewhere) continue;
            names.add(peer.name);
            ids.push_back(peer.id);
            if (ownTarget == peer.uuid) selected = names.size() - 1;
        }
        const bool online = selected > 0;
        if (ownTarget.isNotEmpty() && !online)
        {
            names.add("OFFLINE");
            ids.push_back(0);
            selected = names.size() - 1;
        }
        if (names != linkPeerNames[index] || ids != linkPeerIds[index])
        {
            linkPeerNames[index] = names;
            linkPeerIds[index] = std::move(ids);
            control.setChoices(names);
        }
        control.setInteractionEnabled(names.size() > 1);
        control.setSelectedChoiceIndex(selected, false);
        control.setSelectionHighlighted(online);
    }
    if (countChanged && !restoringEditorUiState) resized();
    refreshRoutingLinkControl();
}

void AvaAudioProcessorEditor::refreshRoutingLinkControl()
{
    if (linkInstanceControl == nullptr) return;
    auto& registry = InstanceLinkRegistry::instance();
    auto& root = audioProcessor.getRoutingOwner();
    const auto sourceId = audioProcessor.getRoutingInstanceId();
    const auto peers = registry.routingPeers(root, sourceId);
    juce::StringArray names { "NONE" };
    std::vector<uint64_t> ids;
    ids.reserve(peers.size());
    for (const auto& peer : peers)
    {
        names.add(peer.name);
        ids.push_back(peer.id);
    }
    if (peers.size() > 1) names.add("LINK ALL");
    if (names != routingPeerNames || ids != routingPeerIds)
    {
        routingPeerNames = names;
        routingPeerIds = std::move(ids);
        linkInstanceControl->setChoices(routingPeerNames);
    }
    linkInstanceControl->setInteractionEnabled(!peers.empty());
    int selected = 0;
    if (focusedRoutingAll && peers.size() > 1)
        selected = int(peers.size()) + 1;
    else if (focusedRoutingPeerId != 0)
        for (size_t i = 0; i < peers.size(); ++i)
            if (peers[i].id == focusedRoutingPeerId) { selected = int(i) + 1; break; }
    linkInstanceControl->setSelectedChoiceIndex(selected, false);
    const bool selectedIsLinked = selected == int(peers.size()) + 1 && peers.size() > 1
        ? std::all_of(peers.begin(), peers.end(), [](const auto& peer) { return peer.linked; })
        : selected > 0 && selected <= int(peers.size())
            ? peers[size_t(selected - 1)].linked
            : std::any_of(peers.begin(), peers.end(), [](const auto& peer) { return peer.linked; });
    linkInstanceControl->setSelectionHighlighted(selectedIsLinked);
}

void AvaAudioProcessorEditor::changeRoutingLinkDestination()
{
    if (linkInstanceControl == nullptr) return;
    const auto selected = linkInstanceControl->getSelectedChoiceIndex();
    if (selected <= 0) { focusedRoutingPeerId = 0; focusedRoutingAll = false; return; }
    auto& registry = InstanceLinkRegistry::instance();
    auto& root = audioProcessor.getRoutingOwner();
    const auto sourceId = audioProcessor.getRoutingInstanceId();
    const auto peers = registry.routingPeers(root, sourceId);
    if (selected == int(peers.size()) + 1 && peers.size() > 1)
    {
        const bool allLinked = std::all_of(peers.begin(), peers.end(),
            [](const auto& peer) { return peer.linked; });
        for (const auto& peer : peers)
            registry.setRoutingLinked(root, sourceId, int(peer.id), !allLinked);
        focusedRoutingPeerId = 0;
        focusedRoutingAll = true;
    }
    else if (selected > 0 && selected <= int(peers.size()))
    {
        const auto& peer = peers[size_t(selected - 1)];
        registry.setRoutingLinked(root, sourceId, int(peer.id), !peer.linked);
        focusedRoutingPeerId = peer.id;
        focusedRoutingAll = false;
    }
    refreshRoutingLinkControl();
}

void AvaAudioProcessorEditor::changeLinkDestination(int index)
{
    if (index < 0 || index >= linkDestinationCount) return;
    const auto row = size_t(index);
    const auto selected = linkToControls[row]->getSelectedChoiceIndex();
    if (selected < 0 || selected >= int(linkPeerIds[row].size())) return;
    auto& root = audioProcessor.getRoutingOwner();
    InstanceLinkRegistry::instance().setUnitLinkTarget(root.getLinkSessionId(), index,
        linkPeerIds[row][size_t(selected)]);
    root.notifyHostOfStateChange();
    refreshInstanceLinkControls();
}

