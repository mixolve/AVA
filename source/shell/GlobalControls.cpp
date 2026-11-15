#include "Editor.h"
#include "../routing/Panel.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "ClipboardTransfer.h"
#include "EditorStateSchema.h"
#include "../routing/State.h"

#include <algorithm>
#include <array>

void AvaAudioProcessorEditor::setupShellControls()
{
    // This control is plugin-wide even inside an embedded routing-instance editor.
    // Binding it to the instance APVTS would bypass only the currently edited node.
    // Per-instance bypass remains available in the routing panel.
    globalBypassButton = std::make_unique<ParameterControl>(
        audioProcessor.getRoutingOwner().getValueTreeState(),
        AvaAudioProcessor::paramGlobalBypassId, "BYPASS", 0);
    globalBypassButton->getProperties().set(juce::Identifier("oscParameterId"), AvaAudioProcessor::paramGlobalBypassId);
    globalBypassButton->setBottomIndicatorsVisible(true, false);
    bypassCompensationControl = std::make_unique<ParameterControl>(
        audioProcessor.getRoutingOwner().getValueTreeState(),
        AvaAudioProcessor::paramBypassCompensationId, "COMPEN\nSATION", 2);
    bypassCompensationControl->getProperties().set(juce::Identifier("oscParameterId"),
        AvaAudioProcessor::paramBypassCompensationId);
    // COMPENSATION is not assignable to an SL slot from UNIT / MAIN.
    // Keep all other control behaviour and indicator policies unchanged.
    bypassCompensationControl->setBottomIndicatorsVisible(false, true);
    unitMainContent.addAndMakeVisible(*bypassCompensationControl);

    globalBypassButton->onValueChanged = [this] {
        const auto bypassActive = globalBypassButton->getValue() >= 0.5;
        globalBypassButton->setSelectionHighlighted(bypassActive);
    };
    globalBypassButton->onValueChanged();
    unitMainContent.addAndMakeVisible(*globalBypassButton);

    undoButton = std::make_unique<PushControl>("UNDO");
    undoButton->setBottomIndicatorsVisible(false, false);
    undoButton->onTrigger = [this]
    {
        performHistoryStep(HistoryAction::undo);
        clearKeyboardFocus(*this);
    };
    unitMainContent.addAndMakeVisible(*undoButton);

    redoButton = std::make_unique<PushControl>("REDO");
    redoButton->setBottomIndicatorsVisible(false, false);
    redoButton->onTrigger = [this]
    {
        performHistoryStep(HistoryAction::redo);
        clearKeyboardFocus(*this);
    };
    unitMainContent.addAndMakeVisible(*redoButton);

    // A/B is a persistent two-state choice, not a momentary action button.
    abControl = std::make_unique<LocalChoiceControl>("A/B", juce::StringArray { "OFF", "ON" },
        audioProcessor.getABCompareActiveSlot());
    abControl->setName("ab");
    abControl->setBottomIndicatorsVisible(false, false);
    abControl->onValueChanged = [this]
    {
        if (abControl->getSelectedChoiceIndex() != audioProcessor.getABCompareActiveSlot())
            switchABState();
    };
    unitMainContent.addAndMakeVisible(*abControl);

    // Copy the current live A/B state into the opposite snapshot. This is a
    // momentary action (commit on release), not a second A/B selection toggle.
    abCopyButton = std::make_unique<PushControl>("COPY\nA TO B");
    abCopyButton->setBottomIndicatorsVisible(false, false);
    abCopyButton->onTrigger = [this]
    {
        captureCurrentABState();
        const auto activeSlot = audioProcessor.getABCompareActiveSlot();
        audioProcessor.setABCompareSnapshot(1 - activeSlot,
                                            audioProcessor.getABCompareSnapshot(activeSlot));
        clearKeyboardFocus(*this);
    };
    unitMainContent.addAndMakeVisible(*abCopyButton);

    unitCopyButton = std::make_unique<PushControl>("COPY");
    unitCopyButton->setBottomIndicatorsVisible(false, false);
    unitCopyButton->onTrigger = [owner = &audioProcessor.getRoutingOwner()]
    {
        ava::clipboard::copyPlugin(*owner);
    };
    unitMainContent.addAndMakeVisible(*unitCopyButton);

    unitPasteButton = std::make_unique<PushControl>("PASTE");
    unitPasteButton->setBottomIndicatorsVisible(false, false);
    unitPasteButton->onTrigger = [owner = &audioProcessor.getRoutingOwner()]
    {
        // Restore may replace editor objects; defer until the click returns.
        juce::MessageManager::callAsync([owner] { ava::clipboard::pastePlugin(*owner); });
    };
    unitMainContent.addAndMakeVisible(*unitPasteButton);

    unitZoomControl = std::make_unique<LocalChoiceControl>("ZOOM", shell_editor_ui::zoomChoices(),
        shell_editor_ui::zoomChoiceIndex(shell_editor_ui::zoomStep(getRootWindowState())));
    unitZoomControl->setBottomIndicatorsVisible(false, false);
    unitZoomControl->onValueChanged = [this]
    {
        getRootWindowState().setProperty(shell_editor_ui::key("zoom_step"),
            shell_editor_ui::zoomStepForChoice(unitZoomControl->getSelectedChoiceIndex()), nullptr);
    };
    unitMainContent.addAndMakeVisible(*unitZoomControl);
    juce::StringArray dockSizes;
    for (int size = 1; size <= 16; ++size) dockSizes.add(juce::String(size));
    unitDockSizeControl = std::make_unique<LocalChoiceControl>("DOCK\nSIZE", dockSizes,
        shell_editor_ui::dockControlCount(getRootWindowState()) - 1);
    unitDockSizeControl->setBottomIndicatorsVisible(false, false);
    unitDockSizeControl->onValueChanged = [this]
    {
        getRootWindowState().setProperty(shell_editor_ui::key("dock_count"),
            unitDockSizeControl->getSelectedChoiceIndex() + 1, nullptr);
    };
    unitMainContent.addAndMakeVisible(*unitDockSizeControl);

    pinRoutingControl = std::make_unique<LocalChoiceControl>(
        "PIN\nINSTS", juce::StringArray { "OFF", "ON" },
        bool(getRootWindowState().getProperty(shell_editor_ui::key("pin_routing"), true)) ? 1 : 0);
    pinRoutingControl->setBottomIndicatorsVisible(false, false);
    pinRoutingControl->onValueChanged = [this]
    {
        const bool enabled = pinRoutingControl->getSelectedChoiceIndex() == 1;
        getRootWindowState().setProperty(shell_editor_ui::key("pin_routing"), enabled, nullptr);
        // PIN INSTS changes only the editor view, not the host's audio state.
        // Only the UNIT-owned strip is visible, including in nested editors.
        if (auto* root = audioProcessor.getRoutingOwner().getContentEditor())
        {
            root->refreshPinnedRoutingStrip();
            root->updatePinnedSearchPosition();
        }
        resized();
    };
    unitMainContent.addAndMakeVisible(*pinRoutingControl);

    unitSearchControl = std::make_unique<LocalChoiceControl>(
        "PIN\nSEARCH", juce::StringArray { "OFF", "ON" },
        bool(getRootWindowState().getProperty(shell_editor_ui::key("search_visible"), true)) ? 1 : 0);
    unitSearchControl->setBottomIndicatorsVisible(false, false);
    unitSearchControl->onValueChanged = [this]
    {
        auto& rootState = getRootWindowState();
        rootState.setProperty(shell_editor_ui::key("search_visible"),
            unitSearchControl->getSelectedChoiceIndex() == 1, nullptr);
        if (auto* root = audioProcessor.getRoutingOwner().getContentEditor())
        {
            root->resized();
            root->updatePinnedSearchPosition();
            if (root->activeInstanceEditor != nullptr)
                root->activeInstanceEditor->resized();
        }
    };
    unitMainContent.addAndMakeVisible(*unitSearchControl);

    // Read-only status tile: no potentiometer, no input handler and no dock pin.
    unitSampleRateLabel = std::make_unique<SquareTextLabel>();
    unitSampleRateLabel->setName("sample-rate");
    unitSampleRateLabel->setColour(juce::Label::outlineColourId, uiGreyLight);
    unitSampleRateLabel->getProperties().set("multilineStatusText", true);
    unitSampleRateLabel->setJustificationType(juce::Justification::centred);
    unitSampleRateLabel->setText("—", juce::dontSendNotification);
    unitSampleRateLabel->getProperties().set("disableDockSelection", true);
    unitSampleRateLabel->setWantsKeyboardFocus(false);
    unitSampleRateLabel->setInterceptsMouseClicks(false, false);
    unitMainContent.addAndMakeVisible(*unitSampleRateLabel);

    slotsContent.onBackgroundClick = [this](const juce::MouseEvent&) { cancelHostSlotMove(); };

    for (int slotIndex = 0; slotIndex < static_cast<int>(hostSlotButtons.size()); ++slotIndex)
    {
        auto slotNameField = std::make_unique<BoxTextButton>();
        slotNameField->setComponentID("host-slot-number-" + juce::String(slotIndex + 1));
        slotNameField->setButtonText(juce::String::formatted("%02d-",slotIndex+1)+AvaAudioProcessor::getHostSlotLetterLabel(slotIndex));
        slotNameField->setTextJustification(juce::Justification::centred);
        slotNameField->setInteractionOutlineEnabled(false);
        const auto copyOscAddress = [this, slotIndex](juce::ModifierKeys modifiers)
        {
            if (!modifiers.isCtrlDown() || modifiers.isShiftDown() || modifiers.isAltDown()) return;
            const auto address = audioProcessor.getHostSlotOscAddress(slotIndex);
            if (address.isNotEmpty())
            {
                juce::SystemClipboard::copyTextToClipboard(address);
                hostSlotButtons[size_t(slotIndex)]->flashConfirmation();
            }
        };

        auto slotButton = std::make_unique<BoxTextButton>();
        slotButton->setComponentID("host-slot-name-" + juce::String(slotIndex + 1));
        slotButton->getProperties().set("textContentInset", getUiGap(*this));
        slotButton->getProperties().set("singleLineText", true);
        slotButton->setTextJustification(juce::Justification::centredLeft);
        slotButton->setButtonText({});
        slotButton->onClick = [this, slotIndex] { applyHostSlotMove(slotIndex); };
        slotButton->onModifiedClick = [this, slotIndex, copyOscAddress](juce::ModifierKeys modifiers)
        {
            if (modifiers.isCtrlDown() && modifiers.isShiftDown() && !modifiers.isAltDown())
                clearHostSlot(slotIndex);
            else
                copyOscAddress(modifiers);
        };
        slotNameField->onClick=slotButton->onClick;
        slotNameField->onModifiedClick=slotButton->onModifiedClick;
        for (auto* button : { slotNameField.get(), slotButton.get() })
        {
            button->getProperties().set("suppressPressFeedback",true);
            button->getProperties().set("textSurfaceInset",8);
            button->getProperties().set("preserveCtrlClick", true);
            button->setDragRequiresControl(false);
            button->onControlDragStarted = [this, slotIndex] { armHostSlotMove(slotIndex); };
            button->onControlDrag = [this] (const juce::MouseEvent& event, const bool released)
            {
                handleHostSlotControlDrag(event, released);
            };
        }

        slotsContent.addAndMakeVisible(*slotNameField);
        slotsContent.addAndMakeVisible(*slotButton);
        hostSlotNameFields[static_cast<size_t>(slotIndex)] = std::move(slotNameField);
        hostSlotButtons[static_cast<size_t>(slotIndex)] = std::move(slotButton);
    }

    sortFiltersButton = std::make_unique<PushControl>("ALL\nSORT");
    sortFiltersButton->getProperties().set("disableDockSelection", true);
    sortFiltersButton->setBottomIndicatorsVisible(false, false);
    sortFiltersButton->onTrigger = [this]
    {
        sortFilterSections();
        clearKeyboardFocus(*this);
    };
    filterContent.addAndMakeVisible(*sortFiltersButton);

}

void AvaAudioProcessorEditor::clearHostSlot(const int slotIndex)
{
    if (hostSlotMoveSourceIndex == slotIndex) cancelHostSlotMove();
    audioProcessor.clearHostSlotAssignment(slotIndex);
    refreshHostSlotButtons();
}

void AvaAudioProcessorEditor::moveHostSlotAssignment(const int slotIndex, const int offset)
{
    const auto destination = slotIndex + offset;
    if (!juce::isPositiveAndBelow(slotIndex,AvaAudioProcessor::assignableHostSlotCount)
        || !juce::isPositiveAndBelow(destination,AvaAudioProcessor::assignableHostSlotCount)
        || audioProcessor.getHostSlotTargetId(slotIndex).isEmpty()) return;
    audioProcessor.swapHostSlotAssignments(slotIndex, destination);
    refreshHostSlotButtons();
    updateSectionStates();
    scheduleHistorySnapshot();
}

void AvaAudioProcessorEditor::armHostSlotMove(const int slotIndex)
{
    if (! juce::isPositiveAndBelow(slotIndex,AvaAudioProcessor::assignableHostSlotCount)
        || audioProcessor.getHostSlotTargetId(slotIndex).isEmpty())
    {
        cancelHostSlotMove();
        return;
    }
    hostSlotMoveSourceIndex = slotIndex;
    juce::Component::beginDragAutoRepeat(30);
    grabKeyboardFocus();
    updateHostSlotMoveTargets();
}

void AvaAudioProcessorEditor::updateHostSlotMoveTargets()
{
    for (int index = 0; index < static_cast<int>(hostSlotButtons.size()); ++index)
    {
        const auto target = hostSlotMoveSourceIndex >= 0 && index != hostSlotMoveSourceIndex;
        const auto source = index == hostSlotMoveSourceIndex;
        const auto assigned = audioProcessor.getHostSlotTargetId(index).isNotEmpty();
        for(auto* cell:{hostSlotNameFields[size_t(index)].get(),hostSlotButtons[size_t(index)].get()})
        {
            if(cell==nullptr)continue;
            cell->setEnabled(assigned || target);
            const auto highlighted=source || (target && bool(cell->getProperties().getWithDefault("osc.move.hovered",false)));
            if(!target)cell->getProperties().set("osc.move.hovered",false);
            cell->getProperties().set("osc.move.highlighted", highlighted);
            cell->setDragTargetHighlighted(highlighted);
            cell->setMoveTargetAvailable(target);
            cell->setInteractionOutlineEnabled(target || source);
            cell->setState(highlighted?juce::Button::buttonOver:juce::Button::buttonNormal);
        }
    }
}

void AvaAudioProcessorEditor::cancelHostSlotMove()
{
    hostSlotMoveSourceIndex = -1;
    juce::Component::beginDragAutoRepeat(0);
    updateHostSlotMoveTargets();
}

void AvaAudioProcessorEditor::applyHostSlotMove(const int destinationIndex)
{
    const auto sourceIndex = hostSlotMoveSourceIndex;
    if (sourceIndex < 0) return;
    cancelHostSlotMove();
    if (sourceIndex != destinationIndex)
        moveHostSlotAssignment(sourceIndex, destinationIndex - sourceIndex);
    clearKeyboardFocus(*this);
}

void AvaAudioProcessorEditor::handleHostSlotControlDrag(const juce::MouseEvent& event, const bool released)
{
    if (hostSlotMoveSourceIndex < 0) return;
    // The captured source moves with the scrolled content. Use the pointer's
    // screen position so repeated drag events do not drift with that component.
    const auto screenPosition=event.source.getScreenPosition();
    const auto viewportPosition=filterViewport.getLocalPoint(nullptr,screenPosition);
    if(!released)
    {
        const auto edge=getUiGap(*this)*2;
        filterViewport.autoScroll(juce::roundToInt(viewportPosition.x),juce::roundToInt(viewportPosition.y),
            edge,getUiGap(*this));
    }
    const auto position=slotsContent.getLocalPoint(nullptr,screenPosition);
    const auto inViewport=filterViewport.getLocalBounds().toFloat().contains(viewportPosition);
    auto destination = -1;
    for (int index = 0; index < static_cast<int>(hostSlotButtons.size()); ++index)
    {
        auto* field = hostSlotNameFields[static_cast<size_t>(index)].get();
        auto* button = hostSlotButtons[static_cast<size_t>(index)].get();
        if (field == nullptr || button == nullptr) continue;
        const auto hovered = inViewport && index != hostSlotMoveSourceIndex
            && field->getBounds().getUnion(button->getBounds()).toFloat().contains(position);
        const auto highlighted=index == hostSlotMoveSourceIndex || hovered;
        for(auto* cell:{field,button})
        {
            cell->getProperties().set("osc.move.hovered",hovered);
            cell->getProperties().set("osc.move.highlighted", highlighted);
            cell->setDragTargetHighlighted(highlighted);
            cell->setState(highlighted?juce::Button::buttonOver:juce::Button::buttonNormal);
        }
        if (hovered) destination = index;
    }
    if (released)
    {
        if (destination >= 0) applyHostSlotMove(destination);
        else cancelHostSlotMove();
    }
}

void AvaAudioProcessorEditor::refreshHostSlotButtons()
{
    for (int slotIndex = 0; slotIndex < AvaAudioProcessor::hostAutomationSlotCount; ++slotIndex)
    {
        auto* slotNameField = hostSlotNameFields[static_cast<size_t>(slotIndex)].get();
        auto* slotButton = hostSlotButtons[static_cast<size_t>(slotIndex)].get();

        if (slotNameField == nullptr || slotButton == nullptr)
            continue;

        const auto assignment = audioProcessor.getHostSlotTargetId(slotIndex);
        slotNameField->setTooltip(audioProcessor.getHostSlotOscAddress(slotIndex));

        slotButton->setAlpha(1.0f);

        if (assignment.isEmpty())
            slotButton->setButtonText({});
        else
        {
            auto parameterName = audioProcessor.getHostSlotDisplayName(slotIndex);

            if (parameterName.isEmpty())
                parameterName = assignment;

            slotButton->setButtonText(parameterName.toUpperCase());
        }
    }
    updateHostSlotMoveTargets();
    repaint();
}

bool AvaAudioProcessorEditor::isHostSlotAssigned(const juce::String& parameterId) const
{
    // UNIT BYPASS always belongs to the root processor, including inside a
    // nested routing-instance editor. Other parameters use their own instance.
    if (parameterId == AvaAudioProcessor::paramGlobalBypassId)
        return audioProcessor.getRoutingOwner().isHostParameterAssigned(parameterId);
    return audioProcessor.isHostParameterAssigned(parameterId);
}

bool AvaAudioProcessorEditor::handleHostSlotAssignRequest(const juce::String& parameterId)
{
    const auto assigned = parameterId == AvaAudioProcessor::paramGlobalBypassId
        ? audioProcessor.getRoutingOwner().toggleHostSlotAssignment(parameterId)
        : audioProcessor.toggleHostSlotAssignment(parameterId);
    if (!assigned) return false;
    refreshHostSlotButtons();
    updateSectionStates();
    resized();
    return true;
}

void AvaAudioProcessorEditor::switchPinnedRoutingInstance(const int instanceId)
{
    // A nested editor may be destroyed when a different routing node opens.
    // Do not change the active editor from inside one of its own button callbacks.
    auto* root = audioProcessor.getRoutingOwner().getContentEditor();
    if (root == nullptr) return;
    juce::MessageManager::callAsync(
        [safeRoot = juce::Component::SafePointer<AvaAudioProcessorEditor>(root), instanceId]
        {
            if (safeRoot == nullptr) return;
            const auto state = ava::routing::readState(safeRoot->audioProcessor.getValueTreeState().state);
            if (instanceId == state.rootInstanceId)
            {
                if (safeRoot->activeInstanceId != 0) safeRoot->closeRoutingInstance();
                return;
            }
            const auto found = std::find_if(state.nodes.begin(), state.nodes.end(),
                [instanceId](const auto& node) { return node.id == instanceId && !node.container; });
            if (found == state.nodes.end() || safeRoot->activeInstanceId == instanceId) return;
            safeRoot->openRoutingInstance(instanceId);
        });
}

void AvaAudioProcessorEditor::refreshPinnedRoutingStrip()
{
    const bool visible = bool(getRootWindowState().getProperty(shell_editor_ui::key("pin_routing"), true));
    if (pinRoutingControl != nullptr && pinRoutingControl->getSelectedChoiceIndex() != int(visible))
        pinRoutingControl->setSelectedChoiceIndex(visible ? 1 : 0, false);
    if (visible != pinnedRoutingVisible)
    {
        pinnedRoutingVisible = visible;
        // During resized(), the parent layout is already responsible for
        // positioning the pinned strip. Avoid recursively laying out the editor.
        if (!layingOutEditor && getWidth() > 0 && controlSearchPanel != nullptr) resized();
    }
    // The compact dock has no pinned routing strip. Keep its existing tiles
    // untouched until the expanded geometry has been calculated again.
    if (!visible || routingInstanceId != 0 || isDockOnly()) return;

    const auto topology = ava::routing::readState(audioProcessor.getRoutingOwner().getValueTreeState().state);
    const auto selected = activeInstanceId != 0 ? activeInstanceId : topology.rootInstanceId;
    const int gap = getUiGap(*this);
    const int side = getSquareControlSize(*this);
    const int tileHeight = standardControlTextZoneHeight + 2 * controlEdgeInset;
    // The ribbon tiles share the dock's global grid, but changing that grid's
    // origin must reposition existing tiles, not destroy/recreate them.
    const int firstX = juce::jmax(0, quickAccessStrip.getX() + quickAccessViewport.getX()
                                     - pinnedRoutingViewport.getX());
    juce::String signature = juce::String(shell_editor_ui::zoomStep(getRootWindowState()));
    for (const auto& node : topology.nodes)
        if (!node.container)
            signature << "|" << node.id << ":" << ava::routing::getDisplayName(topology, node.id);
    if (signature == pinnedRoutingSignature)
    {
        // Changing INSTANCE only changes which existing tile is highlighted.
        // Recreating all buttons was responsible for the strip flashing.
        size_t index = 0;
        int x = firstX;
        for (const auto& node : topology.nodes)
        {
            if (node.container) continue;
            if (index < pinnedRoutingButtons.size())
            {
                auto& button = *pinnedRoutingButtons[index];
                button.setBounds(x, 0, side, tileHeight);
                button.setSelectionHighlighted(node.id == selected);
            }
            x += side + gap;
            ++index;
        }
        pinnedRoutingContent.setSize(juce::jmax(1, x), tileHeight);
        return;
    }
    pinnedRoutingSignature = signature;

    const int previousScrollX = pinnedRoutingViewport.getViewPositionX();
    pinnedRoutingContent.removeAllChildren();
    pinnedRoutingButtons.clear();
    int x = firstX;
    for (const auto& node : topology.nodes)
    {
        if (node.container) continue;
        const auto label = ava::routing::getDisplayName(topology, node.id);
        auto button = std::make_unique<BoxTextButton>();
        button->setButtonText(label);
        button->setTextJustification(juce::Justification::centred);
        button->getProperties().set("routingPinnedTile", true);
        button->setSelectionHighlighted(node.id == selected);
        const int id = node.id;
        button->onClick = [this, id] { switchPinnedRoutingInstance(id); };
        button->setBounds(x, 0, side, tileHeight);
        pinnedRoutingContent.addAndMakeVisible(*button);
        pinnedRoutingButtons.push_back(std::move(button));
        x += side + gap;
    }
    pinnedRoutingContent.setSize(juce::jmax(1, x), tileHeight);
    pinnedRoutingViewport.setViewPosition(previousScrollX, 0);
}
