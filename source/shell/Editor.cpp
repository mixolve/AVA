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
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>())
            editor->mouseUp(event.getEventRelativeTo(editor));
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
        showUnitPage(UnitPage::main);
        if (returnToRoutingAction != nullptr) returnToRoutingAction();
    });
    routingPanel->setOnOpenInstance([this] (const int id)
    {
        showUnitPage(UnitPage::main);
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
            [this, id] (const juce::String& name)
            {
                return InstanceLinkRegistry::instance().editRouting(audioProcessor,
                    [name](juce::ValueTree& tree, int first, int)
                    { return ava::routing::renameInstance(tree, first, name); }, id);
            },
            getLocalArea(&anchor, anchor.getLocalBounds()), {}, {}, true);
        if (auto* prompt = dynamic_cast<PromptComponent*>(textPromptOverlay.get())) prompt->followAnchor(anchor);
    });
    // A single permanent UNIT row, without nested frames or collapse state.
    unitMainContent.setCollapsible(false);
    unitMainContent.setOpaque(false);
    moduleContent.addAndMakeVisible(unitMainContent);
    unitMainRow.attach("unit-main");
    unitMainRow.setScrollLabel("UNIT\nSCROLL");
    routingPanel->setName("unit-routing");
    routingPanel->setOnBackgroundClick([this] { showUnitPage(UnitPage::main); });
    addChildComponent(*routingPanel);

    slotsContent.setCollapsible(false);
    slotsContent.onBackgroundRelease = [this] { showUnitPage(UnitPage::main); };
    slotsViewport.setName("unit-slots");
    slotsViewport.setViewedComponent(&slotsContent, false);
    slotsViewport.setScrollBarsShown(false, false, false, false);
    slotsViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    slotsViewport.setWantsKeyboardFocus(false);
    addChildComponent(slotsViewport);
    slotsVerticalScrollControl = std::make_unique<LocalContinuousControl>("VERTICAL\nSCROLL");
    slotsVerticalScrollControl->setName("vertical scroll");
    slotsVerticalScrollControl->setComponentID("slots-vertical-scroll");
    slotsVerticalScrollControl->getProperties().set("disableDockSelection", true);
    slotsVerticalScrollControl->onValueChanged = [this]
    {
        const auto maximum = juce::jmax(0, slotsContent.getHeight() - slotsViewport.getHeight());
        slotsViewport.setViewPosition(0, juce::roundToInt(slotsVerticalScrollControl->getValue() * maximum));
    };
    slotsViewport.onViewChanged = [this] { syncSlotsVerticalScrollControl(); };
    addChildComponent(*slotsVerticalScrollControl);
    OscPanel::Actions oscActions;
    oscActions.getSettings = [this] { return audioProcessor.getOscSettings(); };
    oscActions.setSettings = [this] (const OscSettings& settings)
    {
        return audioProcessor.setOscSettings(settings);
    };
    oscActions.isInputPortBusy = [this] { return audioProcessor.isOscInputPortBusy(); };
    // OSC owns its controls, which are reparented into the UNIT row by layout.
    oscPanel = std::make_unique<OscPanel>(std::move(oscActions));
    // INSTANCE LINK belongs to the instance row; UNIT SYNC belongs to the unit row.
    eqlContent.setName("eql-page");
    eqlContent.onBackgroundClick = [this](const juce::MouseEvent& event) { handleEqlBackgroundClick(event); };
    filterContent.onBackgroundClick = eqlContent.onBackgroundClick;
    eqlContent.setOpaque(false);
    eqlContent.addAndMakeVisible(filterContent);
    moduleContent.onBackgroundClick = eqlContent.onBackgroundClick;
    mainContent.onBackgroundClick = eqlContent.onBackgroundClick;
    unitMainContent.onBackgroundClick = eqlContent.onBackgroundClick;
    instanceContent.onBackgroundClick = eqlContent.onBackgroundClick;
    moduleContent.addAndMakeVisible(instanceContent);
    instanceContent.addAndMakeVisible(mainContent);
    instanceMainRow.attach("instance-main");
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
    pinnedRoutingViewport.setScrollBarsShown(false, false, false, false);
    pinnedRoutingViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    pinnedRoutingViewport.setWantsKeyboardFocus(false);
    pinnedRoutingViewport.getProperties().set("foregroundOccluder", true);
    addChildComponent(pinnedRoutingViewport);
    pinnedRoutingScrollControl = std::make_unique<LocalContinuousControl>("INSTS\nSCROLL");
    pinnedRoutingScrollControl->setName("insts scroll");
    pinnedRoutingScrollControl->setComponentID("pinned-routing-scroll");
    pinnedRoutingScrollControl->getProperties().set("disableDockSelection", true);
    pinnedRoutingScrollControl->getProperties().set("foregroundOccluder", true);
    pinnedRoutingScrollControl->onValueChanged = [this]
    {
        const auto maximum = juce::jmax(0, pinnedRoutingContent.getWidth() - pinnedRoutingViewport.getWidth());
        pinnedRoutingViewport.setViewPosition(
            juce::roundToInt(pinnedRoutingScrollControl->getValue() * double(maximum)), 0);
    };
    pinnedRoutingViewport.onViewChanged = [this] { syncPinnedRoutingScrollControl(); };
    pinnedRoutingScrollControl->setBottomComponent(unitSyncControls);
    addChildComponent(*pinnedRoutingScrollControl);
    // The vertical page controller is fixed at the right edge of the pinned
    // INSTANCE strip. Its target is the currently displayed editor/instance.
    pinnedVerticalScrollControl = std::make_unique<LocalContinuousControl>("VERTICAL\nSCROLL");
    pinnedVerticalScrollControl->setName("vertical scroll");
    pinnedVerticalScrollControl->setComponentID("pinned-instance-vertical-scroll");
    pinnedVerticalScrollControl->getProperties().set("disableDockSelection", true);
    pinnedVerticalScrollControl->getProperties().set("foregroundOccluder", true);
    pinnedVerticalScrollControl->onValueChanged = [this]
    {
        auto* visibleEditor = activeInstanceEditor != nullptr ? activeInstanceEditor.get() : this;
        const auto maximum = juce::jmax(0, visibleEditor->moduleContent.getHeight()
                                        - visibleEditor->filterViewport.getHeight());
        visibleEditor->filterViewport.setViewPosition(0,
            juce::roundToInt(pinnedVerticalScrollControl->getValue() * double(maximum)));
    };
    addChildComponent(*pinnedVerticalScrollControl);
    // The selected dock tiles are native controls bound to the source parameters.
    addAndMakeVisible(quickAccessStrip);
    quickAccessStrip.addAndMakeVisible(quickAccessViewport);
    quickAccessViewport.setViewedComponent(&quickAccessContent, false);
    quickAccessViewport.setScrollBarsShown(false, false, false, false);
    quickAccessViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    quickAccessViewport.setWantsKeyboardFocus(false);
    quickAccessScrollControl = std::make_unique<LocalContinuousControl>("DOCK\nSCROLL");
    quickAccessScrollControl->setName("dock scroll");
    quickAccessScrollControl->setComponentID("quick-access-scroll");
    quickAccessScrollControl->getProperties().set("disableDockSelection", true);
    quickAccessScrollControl->onValueChanged = [this]
    {
        const bool vertical = isDockOnly()
            && bool(getRootWindowState().getProperty(shell_editor_ui::key("dock_vertical"), false));
        const auto maximum = juce::jmax(0, vertical
            ? quickAccessContent.getHeight() - quickAccessViewport.getHeight()
            : quickAccessContent.getWidth() - quickAccessViewport.getWidth());
        const auto offset = juce::roundToInt(quickAccessScrollControl->getValue() * double(maximum));
        quickAccessViewport.setViewPosition(vertical ? 0 : offset, vertical ? offset : 0);
    };
    quickAccessViewport.onViewChanged = [this] { syncQuickAccessScrollControl(); };
    quickAccessScrollControl->setBottomComponent(dockControls);
    quickAccessStrip.addAndMakeVisible(*quickAccessScrollControl);
    // Marking remains visible at the source in the scrolling viewport;
    // mirrored dock controls use their ordinary neutral frame and rail.
    quickAccessContent.getProperties().set("suppressManualSelectionHighlight", true);
    quickAccessStrip.getProperties().set("foregroundOccluder", true);
    dockControls.sort.onClick = [this]
    {
        dockDragSource = dockDropTarget = -1;
        for (auto& item : quickAccessItems)
        {
            item.control->getProperties().remove("arrangementDragging");
            item.control->repaint();
        }
        dockSortOverlay.setVisible(dockControls.sort.getToggleState());
        dockSortOverlay.repaint();
    };
    dockSortOverlay.onGesture = [this](const juce::MouseEvent& event, int phase) { dragDockControl(event, phase); };
    dockControls.collapse.onClick = [this] { setDockOnly(dockControls.collapse.getToggleState()); };
    dockControls.vertical.onClick = [this]
    {
        auto* root = this;
        while (auto* parent = root->findParentComponentOfClass<AvaAudioProcessorEditor>()) root = parent;
        if (!root->isDockOnly())
        {
            dockControls.vertical.setToggleState(false, juce::dontSendNotification);
            return;
        }
        const bool vertical = dockControls.vertical.getToggleState();
        root->getRootWindowState().setProperty(shell_editor_ui::key("dock_vertical"), vertical, nullptr);
        const juce::ScopedValueSetter<bool> guard(root->suppressEditorSizeStateSave, true);
        const auto width = getEditorWidth(*root);
        root->applyDockWindowSize(width, root->getMinimumEditorHeight());
        root->resized();
    };
    unitSyncControls.connect.onDrop = [this](juce::Point<int> screen)
    {
        auto* hit = juce::Desktop::getInstance().findComponentAt(screen);
        if (hit == nullptr) return;
        auto* recipient = dynamic_cast<AvaAudioProcessorEditor*>(hit);
        if (recipient == nullptr) recipient = hit->findParentComponentOfClass<AvaAudioProcessorEditor>();
        if (recipient == nullptr) return;
        const auto from = audioProcessor.getRoutingOwner().getLinkSessionId();
        const auto to = recipient->audioProcessor.getRoutingOwner().getLinkSessionId();
        if (InstanceLinkRegistry::instance().linkDraggedUnit(from, to))
        {
            refreshInstanceLinkControls();
            recipient->refreshInstanceLinkControls();
        }
    };
    unitSyncControls.pause.onClick = [this]
    {
        const auto unit = audioProcessor.getRoutingOwner().getLinkSessionId();
        InstanceLinkRegistry::instance().setPaused(unit, unitSyncControls.pause.getToggleState());
        refreshInstanceLinkControls();
    };
    addChildComponent(dockSortOverlay);
    // The search is a fixed overlay, not a child of the scrolling canvas.
    // Its space is still reserved by layoutUnifiedBlocks().
    addAndMakeVisible(*controlSearchPanel);
    filterViewport.setViewedComponent(&moduleContent, false);
    filterViewport.onViewChanged = [this]
    {
        updatePinnedSearchPosition();
        // Nested INSTANCE editors use the root's fixed vertical controller.
        if (routingInstanceId == 0) syncVerticalScrollControl();
        else if (auto* root = audioProcessor.getRoutingOwner().getContentEditor())
            if (root != this) root->syncVerticalScrollControl();
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
    filterViewport.setScrollBarsShown(false, false, false, false);
    filterViewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    filterViewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(filterViewport);

    juce::StringArray unitNumbers;
    for (int number = 1; number <= InstanceLinkRegistry::maxUnitNumber; ++number)
        unitNumbers.add(InstanceLinkRegistry::formatUnitNumber(number));
    unitNumberControl = std::make_unique<LocalChoiceControl>("UNIT\nNUMBER", unitNumbers, 0);
    unitNumberControl->setBottomIndicatorsVisible(false, false);
    unitNumberControl->onValueChanged = [this]
    {
        const auto unit = audioProcessor.getRoutingOwner().getLinkSessionId();
        InstanceLinkRegistry::instance().setUnitNumber(unit, unitNumberControl->getSelectedChoiceIndex() + 1);
        refreshInstanceLinkControls();
    };

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
    for (size_t index = 0; index < linkToControls.size(); ++index)
    {
        auto& control = linkToControls[index];
        control = std::make_unique<LocalChoiceControl>("LINK\nTO", juce::StringArray { "NONE" }, 0);
        control->setBottomIndicatorsVisible(false, false);
        control->onValueChanged = [this, index] { changeLinkDestination(int(index)); };
    }

    // Internal ROUTING nodes are different from host UNITs. This independent
    // selector always addresses other INSTANCEs inside the same root AVA.
    linkInstanceControl = std::make_unique<LocalChoiceControl>(
        "INSTANCE\nLINK", juce::StringArray { "NONE" }, 0);
    linkInstanceControl->setBottomIndicatorsVisible(false, false);
    linkInstanceControl->setAlwaysUseChoiceList(true);
    linkInstanceControl->onValueChanged = [this] { changeRoutingLinkDestination(); };
    instanceMainRow.content.addAndMakeVisible(*linkInstanceControl);
    refreshInstanceLinkControls();

    displayedModule = audioProcessor.getActiveModule();

    setupShellControls();

    setupPresetControls();

    filterDisplayOrder.reserve(EqlModuleProcessor::maxFilterCount);
    for (int filterIndex = 0; filterIndex < EqlModuleProcessor::maxFilterCount; ++filterIndex)
        filterDisplayOrder.push_back(filterIndex);

    if (auto* initialEqlProcessor = audioProcessor.getEqlModuleProcessor())
        setupEqlControls(initialEqlProcessor->getValueTreeState());

    for (size_t index = 0; index < eqlScrollRows.size(); ++index)
    {
        eqlScrollRows[index] = std::make_unique<HeaderControlRow>(eqlContent);
        eqlScrollRows[index]->attach("eql-row-" + juce::String(int(index) + 1));
        eqlScrollRows[index]->setScrollLabel(eqlScrollLabels[index]);
    }

    restoreEditorStateFromValueTree();

    aboutPanel = makeAboutPanel(shell_setup_support::getMixolveInfoMarkdown(),
        [this] { showUnitPage(UnitPage::main); });
    aboutPanel->setName("about");
    addChildComponent(*aboutPanel);
    windowDragHandle = std::make_unique<SidebarFocusHandle>();
    windowDragHandle->setName("window-drag-handle");
    windowDragHandle->setWantsKeyboardFocus(false);
    addAndMakeVisible(*windowDragHandle);
    windowMoveGesture = std::make_unique<WindowMoveGesture>(*windowDragHandle, *this);
    const auto makePageControl = [this](const char* title, const char* name, UnitPage page)
    {
        auto control = std::make_unique<PushControl>(title);
        control->setName(name);
        control->setBottomIndicatorsVisible(false, false);
        control->onTrigger = [this, page] { showUnitPage(page); };
        unitMainRow.content.addAndMakeVisible(*control);
        return control;
    };
    routingControl = makePageControl("ROUTING", "routing", UnitPage::routing);
    slotsControl = makePageControl("SLOTS", "slots", UnitPage::slots);

    aboutControl = makePageControl("ABOUT", "info", UnitPage::about);
    aboutControl->onTrigger = [this]
    {
        if (!isSvgExportGesture(juce::ModifierKeys::getCurrentModifiersRealtime()))
            showUnitPage(UnitPage::about);
    };
    svgExportClickListener = std::make_unique<ModifierClickAction>(
        [] (const juce::ModifierKeys& modifiers) { return isSvgExportGesture(modifiers); },
        [this] { requestSvgExport(); });
    aboutControl->addMouseListener(svgExportClickListener.get(), true);

    // Native UNIT MAIN controls use the same Ctrl-click quick-access marking
    // as all module controls (no special treatment for action/toggle controls).

    startTimerHz(60);
    registerParameterListeners();

    updateSectionStates();
    setResizeLimits(getEditorWidth(*this), getMinimumEditorHeight(),
                    getEditorWidth(*this), getMaximumEditorHeight());
    getConstrainer()->setFixedAspectRatio(0.0);
    const auto defaultExpandedHeight = getExpandedEditorWidth(shell_editor_ui::editorColumns(getRootWindowState()));
    const int previousHeight = static_cast<int>(valueTreeState.state.getProperty(
        AvaAudioProcessor::editorHeightStateKey, defaultExpandedHeight));
    const juce::Point<int> editorSize { getEditorWidth(*this),
        juce::jlimit(getMinimumEditorHeight(), getMaximumEditorHeight(), previousHeight) };
    setSize(editorSize.x, editorSize.y);
    audioProcessor.setLastEditorSize(editorSize.x, isDockOnly()
        ? juce::jlimit(getExpandedMinimumEditorHeight(), maximumEditorHeight, previousHeight) : editorSize.y);

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
    if (unitNumberControl == nullptr || sendControl == nullptr || receiveControl == nullptr || linksCountControl == nullptr) return;
    auto& registry = InstanceLinkRegistry::instance();
    const auto source = audioProcessor.getRoutingOwner().getLinkSessionId();
    const auto number = registry.unitNumber(source);
    unitNumberControl->setSelectedChoiceIndex(number - 1, false);
    const auto unitNumber = InstanceLinkRegistry::formatUnitNumber(number);
    unitMainRow.setScrollLabel("UNIT " + unitNumber + "\nSCROLL");
    unitSyncControls.pause.setToggleState(registry.isPaused(source), juce::dontSendNotification);
    const bool sending = registry.isSendEnabled(source);
    const bool receiving = registry.isReceiveEnabled(source);
    sendControl->setSelectedChoiceIndex(int(sending), false);
    receiveControl->setSelectedChoiceIndex(int(receiving), false);
    const auto traffic = registry.activity(source);
    const auto now = juce::Time::getMillisecondCounter();
    constexpr uint32_t activityReleaseMs = 100;
    unitSyncControls.send.setActive(traffic.sentAt != 0 && now - traffic.sentAt < activityReleaseMs);
    unitSyncControls.receive.setActive(traffic.receivedAt != 0 && now - traffic.receivedAt < activityReleaseMs);
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

