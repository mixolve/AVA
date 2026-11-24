#include "Editor.h"
#include "../routing/Panel.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"
#include "ClipboardTransfer.h"
#include "EditorStateSchema.h"
#include "../routing/State.h"
#include "PinnedRoutingOrder.h"

#include <algorithm>
#include <array>
#include <limits>

namespace
{
std::vector<int> pinnedInstanceIds(const ava::routing::State& topology, const juce::ValueTree& rootState)
{
    std::vector<int> available;
    available.reserve(topology.nodes.size());
    for (const auto& node : topology.nodes)
        if (!node.container) available.push_back(node.id);

    std::vector<int> saved;
    const auto tokens = juce::StringArray::fromTokens(
        rootState.getProperty(shell_editor_ui::key("pinned_routing_order")).toString(), "|", "");
    for (const auto& item : tokens)
    {
        // Old/corrupt presets must never inject invalid routing identifiers.
        const auto id = item.getLargeIntValue();
        if (item.length() <= 10 && item.containsOnly("0123456789")
            && id > 0 && id <= std::numeric_limits<int>::max()
            && item == juce::String(id))
            saved.push_back(static_cast<int>(id));
    }
    return ava::pinned_routing::arrange(available, saved);
}
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

void AvaAudioProcessorEditor::handlePinnedRoutingDrag(const int sourceId,
    const juce::MouseEvent& event, const bool released)
{
    if (sourceId != pinnedRoutingDragSourceId || pinnedRoutingButtons.empty()) return;

    // Match the DOCK/MODULES interaction: rearrange the actual tiles as the
    // pointer crosses each position. There is no extra insertion stripe.
    if (!released && pinnedRoutingViewport.isVisible())
    {
        const auto inViewport = pinnedRoutingViewport.getLocalPoint(event.eventComponent, event.getPosition());
        const auto limit = juce::jmax(0, pinnedRoutingContent.getWidth() - pinnedRoutingViewport.getWidth());
        const int edge = juce::jmax(8, getUiGap(*this) * 2);
        const int step = juce::jmax(4, getUiGap(*this));
        int nextX = pinnedRoutingViewport.getViewPositionX();
        if (inViewport.y >= 0 && inViewport.y < pinnedRoutingViewport.getHeight())
        {
            if (inViewport.x >= 0 && inViewport.x < edge) nextX -= step;
            else if (inViewport.x < pinnedRoutingViewport.getWidth()
                     && inViewport.x >= pinnedRoutingViewport.getWidth() - edge) nextX += step;
        }
        pinnedRoutingViewport.setViewPosition(juce::jlimit(0, limit, nextX), 0);
    }

    const auto position = pinnedRoutingContent.getLocalPoint(event.eventComponent, event.getPosition());
    const int count = int(pinnedRoutingButtonIds.size());
    const auto source = std::find(pinnedRoutingButtonIds.begin(), pinnedRoutingButtonIds.end(), sourceId);
    if (source == pinnedRoutingButtonIds.end()) return;
    const int sourceIndex = int(std::distance(pinnedRoutingButtonIds.begin(), source));

    if (!released && position.y >= 0 && position.y < pinnedRoutingContent.getHeight())
    {
        const int firstX = pinnedRoutingButtons.front()->getX();
        const int side = getSquareControlSize(*this);
        const int step = side + getUiGap(*this);
        const int targetIndex = juce::jlimit(0, count - 1,
            (position.x - firstX + side / 2) / juce::jmax(1, step));
        if (sourceIndex != targetIndex)
        {
            // unique_ptr moves preserve the source button under the mouse,
            // including all Solo/Mute/Bypass callbacks and their parameter IDs.
            ava::pinned_routing::moveToIndex(pinnedRoutingButtons, sourceIndex, targetIndex);
            ava::pinned_routing::moveToIndex(pinnedRoutingButtonIds, sourceIndex, targetIndex);
            for (int i = 0, x = firstX; i < count; ++i, x += step)
                pinnedRoutingButtons[size_t(i)]->setBounds(x, 0, side, getPinnedRoutingTileHeight(*this));
        }
    }

    if (!released) return;
    pinnedRoutingDragSourceId = 0;
    juce::Component::beginDragAutoRepeat(0);
    for (auto& button : pinnedRoutingButtons)
    {
        button->getProperties().set("routing.instance.dragging", false);
        button->repaint();
    }

    // Defer writing state until after the source BoxTextButton::mouseUp.
    // Sorting is presentational; no routing graph or parameter changes.
    const auto order = pinnedRoutingButtonIds;
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<AvaAudioProcessorEditor>(this), order]
        {
            if (safe == nullptr) return;
            auto& rootState = safe->getRootWindowState();
            const auto topology = ava::routing::readState(
                safe->audioProcessor.getRoutingOwner().getValueTreeState().state);
            std::vector<int> available;
            for (const auto& node : topology.nodes)
                if (!node.container) available.push_back(node.id);
            const auto reconciled = ava::pinned_routing::arrange(available, order);
            if (reconciled != pinnedInstanceIds(topology, rootState))
            {
                juce::StringArray tokens;
                for (const auto id : reconciled) tokens.add(juce::String(id));
                rootState.setProperty(shell_editor_ui::key("pinned_routing_order"),
                                      tokens.joinIntoString("|"), nullptr);
            }
            safe->refreshPinnedRoutingStrip();
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
    // Do not restore the persisted order halfway through a drag gesture.
    if (pinnedRoutingDragSourceId != 0) return;
    // The compact dock has no pinned routing strip. Keep its existing tiles
    // untouched until the expanded geometry has been calculated again.
    if (!visible || routingInstanceId != 0 || isDockOnly()) return;

    const auto topology = ava::routing::readState(audioProcessor.getRoutingOwner().getValueTreeState().state);
    const auto selected = activeInstanceId != 0 ? activeInstanceId : topology.rootInstanceId;
    const auto orderedIds = pinnedInstanceIds(topology, getRootWindowState());
    const int gap = getUiGap(*this);
    const int side = getSquareControlSize(*this);
    const int tileHeight = getPinnedRoutingTileHeight(*this);
    // The ribbon tiles share the dock's global grid, but changing that grid's
    // origin must reposition existing tiles, not destroy/recreate them.
    const int firstX = juce::jmax(0, quickAccessStrip.getX() + quickAccessViewport.getX()
                                     - pinnedRoutingViewport.getX());
    juce::String signature = juce::String(shell_editor_ui::zoomStep(getRootWindowState()));
    for (const auto id : orderedIds)
        signature << "|" << id << ":" << ava::routing::getDisplayName(topology, id);
    if (signature == pinnedRoutingSignature)
    {
        size_t index = 0;
        int x = firstX;
        for (const auto id : orderedIds)
        {
            if (index < pinnedRoutingButtons.size())
            {
                auto& button = *pinnedRoutingButtons[index];
                button.setBounds(x, 0, side, tileHeight);
                button.setSelectionHighlighted(id == selected);
                // These switches read parameters directly rather than using
                // attachments. A peer solo/automation change must repaint an
                // otherwise unchanged tile as well as update its audio state.
                button.refreshBottomIndicatorStates();
            }
            x += side + gap;
            ++index;
        }
        pinnedRoutingContent.setSize(juce::jmax(1, x), tileHeight);
        return;
    }
    pinnedRoutingSignature = signature;
    if (pinnedRoutingDragSourceId != 0)
        juce::Component::beginDragAutoRepeat(0);
    pinnedRoutingDragSourceId = 0;

    const int previousScrollX = pinnedRoutingViewport.getViewPositionX();
    pinnedRoutingContent.removeAllChildren();
    pinnedRoutingButtons.clear();
    pinnedRoutingButtonIds.clear();
    int x = firstX;
    for (const auto id : orderedIds)
    {
        const auto label = ava::routing::getDisplayName(topology, id);
        auto button = std::make_unique<BoxTextButton>();
        button->setButtonText(label);
        button->setTextJustification(juce::Justification::centred);
        button->getProperties().set("routingPinnedTile", true);
        button->setSelectionHighlighted(id == selected);
        button->setDragRequiresControl(false);
        button->onControlDragStarted = [this, id, tile = button.get()]
        {
            pinnedRoutingDragSourceId = id;
            for (auto& item : pinnedRoutingButtons)
            {
                item->getProperties().set("routing.instance.dragging", item.get() == tile);
                item->repaint();
            }
            juce::Component::beginDragAutoRepeat(40);
        };
        button->onControlDrag = [this, id](const juce::MouseEvent& event, bool released)
        {
            handlePinnedRoutingDrag(id, event, released);
        };
        button->onClick = [this, id] { switchPinnedRoutingInstance(id); };
        const auto bindSwitch = [this, id, tile = button.get()](int index, const char* parameterId, const char* switchLabel)
        {
            const auto targetId = ava::routing::parameterTargetId(id, parameterId);
            tile->setBottomIndicator(index, {
                [this, targetId] {
                    auto* parameter = audioProcessor.getRoutingOwner().findHostSlotTarget(targetId);
                    return parameter != nullptr ? (parameter->getValue() >= 0.5f ? 1 : 0) : -1;
                },
                [this, targetId](bool enabled) {
                    if (auto* parameter = audioProcessor.getRoutingOwner().findHostSlotTarget(targetId)) {
                        parameter->beginChangeGesture();
                        parameter->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
                        parameter->endChangeGesture();
                    }
                }
            }, switchLabel);
        };
        bindSwitch(0, AvaAudioProcessor::paramSoloInstanceId, "S");
        bindSwitch(1, AvaAudioProcessor::paramInstanceMuteId, "MT");
        bindSwitch(2, AvaAudioProcessor::paramGlobalBypassId, "BP");
        button->setBounds(x, 0, side, tileHeight);
        pinnedRoutingContent.addAndMakeVisible(*button);
        pinnedRoutingButtons.push_back(std::move(button));
        pinnedRoutingButtonIds.push_back(id);
        x += side + gap;
    }
    pinnedRoutingContent.setSize(juce::jmax(1, x), tileHeight);
    pinnedRoutingViewport.setViewPosition(previousScrollX, 0);
}
