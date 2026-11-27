#include "Panel.h"
#include "State.h"
#include "../shell/Controls.h"
#include "../shell/Editor.h"
#include "../shell/InstanceLink.h"
#include "../shell/ChoiceControl.h"
#include "../shell/EditorStateSchema.h"
#include "../shell/Style.h"
#include "../shell/DisclosureAnimation.h"

#include <algorithm>
#include <array>
#include <map>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace
{
constexpr int petalThickness = 8;
constexpr int petalGap = 2;
constexpr int petalExtent = petalThickness + petalGap;
constexpr int signalClearance = 2;
constexpr int minimumSignalLeg = 8;
constexpr int serialGap = minimumSignalLeg + 2 * signalClearance;
constexpr int busInset = minimumSignalLeg + signalClearance + 1;
constexpr int groupPadding = busInset + minimumSignalLeg + signalClearance;
void configureContainerButton(BoxTextButton& button)
{
    button.setTextJustification(juce::Justification::centred);
    button.setClickingTogglesState(false);
    button.getProperties().set("textSideInset", 2);
    button.getProperties().set("textTopInset", 2);
    button.getProperties().set("textBottomInset", 2);
}

class RoutingPetal final : public juce::Button
{
public:
    enum class Side { top, right, bottom, left };
    RoutingPetal() : juce::Button({}) { setWantsKeyboardFocus(false); setMouseClickGrabsKeyboardFocus(false); }
    juce::Path getOutline() const { return outline().createPathWithRoundedCorners(float(defaultUiCornerRadius)); }
    void setSide(Side value) { side = value; repaint(); }
    Side getSide() const noexcept { return side; }
    void setDragSource(bool value) { if (dragSource != value) { dragSource = value; repaint(); } }
    bool hitTest(int x, int y) override { return outline().contains(static_cast<float>(x), static_cast<float>(y)); }
    void paintButton(juce::Graphics& g, bool hover, bool down) override
    {
        const auto path = getOutline();
        ControlAppearance style;
        style.fillColour = juce::Colour(isEnabled() && down ? 0xffffffff
            : dragSource || (isEnabled() && isHoverInversionReady(*this, hover, "routing.petal"))
                ? 0xff666666 : 0xff444444);
        style.paintBackground(g, path);
        style.frameColour = isEnabled() ? uiGreyLight : uiIndicatorDisabled;
        style.paintFrame(g, path);
    }
private:
    juce::Path outline() const
    {
        const auto w = static_cast<float>(getWidth());
        const auto h = static_cast<float>(getHeight());
        constexpr float bevel = 4.0f;
        juce::Path path;
        if (side == Side::top || side == Side::bottom)
        {
            path.startNewSubPath(0.0f, h);
            path.lineTo(bevel, 0.0f); path.lineTo(w - bevel, 0.0f);
            path.lineTo(w, h);
            path.closeSubPath();
            if (side == Side::bottom)
                path.applyTransform(juce::AffineTransform::scale(-1.0f, -1.0f).translated(w, h));
        }
        else
        {
            path.startNewSubPath(w, 0.0f);
            path.lineTo(0.0f, bevel); path.lineTo(0.0f, h - bevel);
            path.lineTo(w, h);
            path.closeSubPath();
            if (side == Side::right)
                path.applyTransform(juce::AffineTransform::scale(-1.0f, -1.0f).translated(w, h));
        }
        return path;
    }
    Side side = Side::top;
    bool dragSource = false;
};
}

class RoutingPanel::Content final : public juce::Component
{
    enum class SlideDirection { up, down, left, right };

    struct GroupControls
    {
        std::unique_ptr<juce::Component> revealClip, revealBody;
        std::unique_ptr<DisclosureAnimation> appearance;
        juce::Rectangle<int> fullBounds, startBounds;
        bool hasStartBounds = false;
        SlideDirection direction = SlideDirection::down;
        std::array<std::unique_ptr<RoutingPetal>, 4> petals;
        std::unique_ptr<BoxTextButton> compensation;
    };

    struct SlideOrigin
    {
        int owner = 0;
        bool group = false;
        RoutingPetal::Side side = RoutingPetal::Side::bottom;
        juce::Rectangle<int> bounds;
    };

    struct InstanceControls
    {
        std::unique_ptr<juce::Component> revealClip, revealBody;
        std::unique_ptr<DisclosureAnimation> appearance;
        juce::Rectangle<int> fullBounds, startBounds;
        bool hasStartBounds = false;
        SlideDirection direction = SlideDirection::down;
        std::optional<SlideOrigin> origin;
        std::unique_ptr<BoxTextButton> name;
        std::unique_ptr<RoutingPetal> addSerial;
        std::unique_ptr<RoutingPetal> addParallel;
        std::unique_ptr<RoutingPetal> addSerialBefore;
        std::unique_ptr<RoutingPetal> addParallelBefore;
    };

    struct SignalVisual
    {
        int id = 0;
        bool group = false;
    };

    struct SignalPath
    {
        std::array<juce::Point<int>, 4> points;
        size_t count = 0;
        SignalVisual from, to;
    };

public:
    Content(AvaAudioProcessor& processorIn,
            std::function<void(int, const juce::String&)> openActionIn,
            std::function<void(int, const juce::String&, juce::Component&)> renameActionIn,
            std::function<void()> topologyChangedIn,
            std::function<void()> layoutChangedIn)
        : processor(processorIn), state(processorIn.getValueTreeState()),
          openAction(std::move(openActionIn)),
          renameAction(std::move(renameActionIn)),
          topologyChanged(std::move(topologyChangedIn)),
          layoutChanged(std::move(layoutChangedIn))
    {
        setWantsKeyboardFocus(true);
        getProperties().set("emptyEditorBackground", true);
        reflow.snap(true);
        reflow.onUpdate = [this] { resized(); if (layoutChanged != nullptr) layoutChanged(); };
        refresh();
    }

    void paint(juce::Graphics& graphics) override
    {
        for (const auto& [id, frame] : frames)
        {
            if (! containersVisible()) continue;
            juce::Graphics::ScopedSaveState saved(graphics);
            auto animatedFrame = frame;
            auto frameColour = uiGreyLight;
            if (const auto group = groupControls.find(id); group != groupControls.end())
            {
                graphics.reduceClipRegion(group->second.revealClip->getBounds());
                frameColour = uiGreyLight.withMultipliedAlpha(
                    static_cast<float>(group->second.appearance->value()));
            }
            paintUiFrame(graphics, animatedFrame.toFloat(), frameColour,
                         ControlAppearance::pixelThickness(graphics, 1.0f), float(defaultUiCornerRadius));
        }

        for (const auto& [id, group] : retiringGroups)
        {
            if (! containersVisible()) continue;
            juce::ignoreUnused(id);
            juce::Graphics::ScopedSaveState saved(graphics);
            graphics.reduceClipRegion(group.revealClip->getBounds());
            paintUiFrame(graphics, group.fullBounds.reduced(petalExtent).toFloat(),
                         uiGreyLight.withMultipliedAlpha(static_cast<float>(group.appearance->value())),
                         ControlAppearance::pixelThickness(graphics, 1.0f), float(defaultUiCornerRadius));
        }

        const juce::Graphics::ScopedSaveState signalClip(graphics);
        const auto excludePetal = [this, &graphics](const RoutingPetal& petal)
        {
            auto visible = getLocalArea(&petal, petal.getLocalBounds());
            for (auto* parent = petal.getParentComponent(); parent != nullptr && parent != this; parent = parent->getParentComponent())
                visible = visible.getIntersection(getLocalArea(parent, parent->getLocalBounds()));
            if (!visible.isEmpty()) graphics.excludeClipRegion(visible.expanded(signalClearance));
        };
        const auto excludeInstances = [&excludePetal](const auto& items)
        {
            for (const auto& [id, instance] : items)
            {
                juce::ignoreUnused(id);
                for (const auto* petal : { instance.addSerial.get(), instance.addSerialBefore.get(),
                                           instance.addParallel.get(), instance.addParallelBefore.get() })
                    excludePetal(*petal);
            }
        };
        const auto excludeGroups = [&excludePetal](const auto& items)
        {
            for (const auto& [id, group] : items)
            {
                juce::ignoreUnused(id);
                for (const auto& petal : group.petals) excludePetal(*petal);
            }
        };
        excludeInstances(controls);
        excludeInstances(retiringControls);
        if (containersVisible())
        {
            excludeGroups(groupControls);
            excludeGroups(retiringGroups);
            const auto excludeFrame = [&graphics](juce::Rectangle<int> bounds)
            {
                constexpr int band = 1 + 2 * signalClearance;
                const auto outer = bounds.expanded(signalClearance);
                graphics.excludeClipRegion(outer.withHeight(band));
                graphics.excludeClipRegion(outer.withTop(outer.getBottom() - band));
                graphics.excludeClipRegion(outer.withWidth(band));
                graphics.excludeClipRegion(outer.withLeft(outer.getRight() - band));
            };
            for (const auto& [id, frame] : frames)
            {
                juce::ignoreUnused(id);
                excludeFrame(frame);
            }
            for (const auto& [id, group] : retiringGroups)
            {
                juce::ignoreUnused(id);
                excludeFrame(group.fullBounds.reduced(petalExtent));
            }
        }
        signalPaths.clear();

        paintSignalGroup(graphics, layoutState.entryInstanceId);
        paintSignalPaths(graphics);
    }

    void resized() override
    {
        locations.clear();
        frames.clear();
        groupBodies.clear();
        const auto width = instanceSize();
        const auto group = measureGroup(layoutState.entryInstanceId, width);
        const auto maxTop = juce::jmax(0, availableHeight - group.height);
        const auto groupTop = juce::jlimit(0, maxTop, centeringY - group.height / 2);
        layoutGroup(layoutState.entryInstanceId, canvasPaddingX(), groupTop, width);
        repaint();
    }

    bool cancelMoveMode()
    {
        if (moveSourceId == 0) return false;
        clearMove();
        return true;
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        if (moveSourceId != 0 && key == juce::KeyPress::returnKey)
        {
            clearMove();
            return true;
        }
        return false;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (moveSourceId != 0 && event.mods.isLeftButtonDown())
            clearMove();
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (onBackgroundClick != nullptr && !event.mouseWasDraggedSinceMouseDown()
            && !event.mods.isPopupMenu() && getComponentAt(event.getPosition()) == this)
            onBackgroundClick();
    }

    std::function<void()> onBackgroundClick;

    void setContainersVisible(const bool active)
    {
        if (showContainers == active) return;
        showContainers = active;
        rebuildLayoutState();
        applyContainerClipVisibility();
        resized();
        if (layoutChanged != nullptr) layoutChanged();
    }

    bool containersVisible() const noexcept { return showContainers; }

    void applyContainerClipVisibility()
    {
        const auto visible = containersVisible();
        for (auto& [id, group] : groupControls)
        {
            juce::ignoreUnused(id);
            group.revealClip->setVisible(visible);
        }
        for (auto& [id, group] : retiringGroups)
        {
            juce::ignoreUnused(id);
            group.revealClip->setVisible(visible);
        }
    }

    int getPreferredWidth() const
    {
        auto width = measureGroup(layoutState.entryInstanceId, instanceSize()).width + canvasPaddingX() + terminalTailWidth();
        for (const auto& [id, item] : retiringControls) { juce::ignoreUnused(id); width = juce::jmax(width, item.fullBounds.getRight() + terminalTailWidth()); }
        for (const auto& [id, item] : retiringGroups) { juce::ignoreUnused(id); width = juce::jmax(width, item.fullBounds.getRight() + terminalTailWidth()); }
        return width;
    }

    int getPreferredHeight() const
    {
        auto height = measureGroup(layoutState.entryInstanceId, instanceSize()).height;
        for (const auto& [id, item] : retiringControls) { juce::ignoreUnused(id); height = juce::jmax(height, item.fullBounds.getBottom()); }
        for (const auto& [id, item] : retiringGroups) { juce::ignoreUnused(id); height = juce::jmax(height, item.fullBounds.getBottom()); }
        return height;
    }

    int getTargetPreferredWidth() const { return measureGroup(layoutState.entryInstanceId, instanceSize()).width + canvasPaddingX() + terminalTailWidth(); }
    int getTargetPreferredHeight() const { return measureGroup(layoutState.entryInstanceId, instanceSize()).height; }
    int getReflowGeneration() const noexcept { return reflowGeneration; }
    double getReflowProgress() const noexcept { return reflow.value(); }
    bool isReflowRunning() const noexcept { return reflow.isRunning(); }

    void setCanvasSize(int width, int height, int visibleHeight, int centerY)
    {
        const bool needsRecentering = availableHeight != visibleHeight || centeringY != centerY;
        availableHeight = visibleHeight;
        centeringY = centerY;
        if (getWidth() != width || getHeight() != height)
            setSize(width, height);
        else if (needsRecentering)
            resized();
    }

    juce::String getDisplayNameFor(const int instanceId) const
    {
        return ava::routing::getDisplayName(displayedState, instanceId);
    }

    void updateInstanceLabels()
    {
        for (auto& [id, instance] : controls)
        {
            instance.name->setButtonText(getDisplayNameFor(id));
            instance.name->setTooltip({});
        }
    }

    void refresh()
    {
        const auto newState = ava::routing::readState(state.state);
        const auto sameStructure = newState.entryInstanceId == displayedState.entryInstanceId
            && newState.rootInstanceId == displayedState.rootInstanceId && newState.nodes.size() == displayedState.nodes.size()
            && std::equal(newState.nodes.begin(), newState.nodes.end(), displayedState.nodes.begin(), [](const auto& a, const auto& b) {
                return a.id == b.id && a.serialNext == b.serialNext && a.parallelNext == b.parallelNext
                    && a.groupNext == b.groupNext && a.container == b.container;
            });
        if (sameStructure && !controls.empty())
        {
            // Parameter/name changes must not destroy the rename anchor or
            // the independent buttons while an in-place editor is open.
            displayedState = newState;
            updateInstanceLabels();
            for (auto& [id, group] : groupControls)
                if (const auto* head = findNode(id)) group.compensation->setSelectionHighlighted(head->compensated);
            updateBypassPrompts();
            return;
        }

        const auto previousState = displayedState;
        const auto topologyWasChanged = newState.nodes != displayedState.nodes
            || newState.entryInstanceId != displayedState.entryInstanceId;
        if (topologyWasChanged)
            retiringSignalPaths.insert(retiringSignalPaths.end(), signalPaths.begin(), signalPaths.end());
        const auto transitionStart = juce::Time::getMillisecondCounterHiRes();
        const auto hadControls = ! controls.empty();
        displayedState = newState;
        rebuildLayoutState();
        if (moveSourceId != 0 && findNode(moveSourceId) == nullptr)
            moveSourceId = 0;
        auto previousControls = std::move(controls);
        controls.clear();
        auto previousGroups = std::move(groupControls);
        groupControls.clear();
        for (const auto& node : displayedState.nodes)
        {
            if (auto retired = retiringControls.find(node.id); ! node.container && retired != retiringControls.end())
            {
                previousControls.insert_or_assign(node.id, std::move(retired->second));
                retiringControls.erase(retired);
            }
            if (auto retired = retiringGroups.find(node.id); node.parallelNext != 0 && retired != retiringGroups.end())
            {
                previousGroups.insert_or_assign(node.id, std::move(retired->second));
                retiringGroups.erase(retired);
            }
        }

        for (const auto& node : displayedState.nodes)
        {
            if (node.container) continue;
            const auto id = node.id;
            const auto label = getDisplayNameFor(id);
            InstanceControls instance;
            instance.revealClip = std::make_unique<juce::Component>();
            instance.revealClip->getProperties().set("controlViewportClip", true);
            instance.revealBody = std::make_unique<juce::Component>();
            instance.revealClip->setInterceptsMouseClicks(false, true);
            instance.revealBody->setInterceptsMouseClicks(false, true);
            instance.revealClip->addAndMakeVisible(*instance.revealBody);
            addAndMakeVisible(*instance.revealClip);
            const auto previous = previousControls.find(id);
            if (previous != previousControls.end())
            {
                instance.appearance = std::move(previous->second.appearance);
                instance.appearance->setExpanded(true, transitionStart);
                instance.direction = previous->second.direction;
                instance.origin = previous->second.origin;
                instance.hasStartBounds = true;
                instance.startBounds = topologyWasChanged ? previous->second.fullBounds : previous->second.startBounds;
                instance.fullBounds = previous->second.fullBounds;
            }
            else
            {
                instance.appearance = std::make_unique<DisclosureAnimation>();
                if (previousControls.empty()) instance.appearance->snap(true);
                else instance.appearance->setExpanded(true, transitionStart);
                instance.direction = pendingDirection.value_or(inferDirection(id, previousControls));
                instance.origin = pendingOrigin;
            }
            instance.appearance->onUpdate = [this, id] { updateInstanceAppearance(id); repaint(); scheduleRetiredCleanup(); };
            instance.name = makeButton(label);
            instance.name->setComponentID("routing.instance." + juce::String(id));
            // Ctrl+click must reach the routing deletion action, not manual selection.
            instance.name->getProperties().set("preserveCtrlClick", true);
            instance.name->setDragRequiresControl(false);
            const auto bindSwitch = [this, id, tile = instance.name.get()]
                (int index, const char* parameterId, const char* caption)
            {
                const auto targetId = ava::routing::parameterTargetId(id, parameterId);
                tile->setBottomIndicator(index, {
                    [this, targetId] {
                        auto* parameter = processor.getRoutingOwner().findHostSlotTarget(targetId);
                        return parameter != nullptr ? (parameter->getValue() >= 0.5f ? 1 : 0) : -1;
                    },
                    [this, targetId](bool enabled) {
                        if (auto* parameter = processor.getRoutingOwner().findHostSlotTarget(targetId)) {
                            parameter->beginChangeGesture();
                            parameter->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
                            parameter->endChangeGesture();
                        }
                    }
                }, caption);
            };
            bindSwitch(0, AvaAudioProcessor::paramSoloInstanceId, "S");
            bindSwitch(1, AvaAudioProcessor::paramInstanceMuteId, "MT");
            bindSwitch(2, AvaAudioProcessor::paramGlobalBypassId, "BP");
            const auto removeAction = [this, id]
            {
                if (editRouting(ava::routing::removeInstance, id))
                {
                    if (moveSourceId == id)
                        clearMove();
                    notifyTopologyChanged();
                }
            };
            instance.name->onShiftClick = [this, id, button = instance.name.get()]
            {
                if (renameAction) renameAction(id, getDisplayNameFor(id), *button);
            };
            instance.name->onModifiedClick = [this, id, removeAction] (const juce::ModifierKeys modifiers)
            {
                if (modifiers.isCtrlDown() && !modifiers.isShiftDown() && !modifiers.isAltDown())
                    removeAction();
                else if (modifiers.isCtrlDown() && modifiers.isAltDown() && !modifiers.isShiftDown())
                    toggleBypass(id);
            };
            if (ava::routing::getInstanceCount(displayedState) > 1)
            {
                instance.name->onControlDragStarted = [this, id]
                {
                    controlDragActive = true;
                    moveSourceId = id;
                    grabKeyboardFocus();
                    updateMoveTargets();
                };
                instance.name->onControlDrag = [this] (const juce::MouseEvent& event, const bool released)
                {
                    finishControlDrag(event, released);
                };
            }
            instance.addSerial = makePetal();
            instance.addParallel = makePetal();
            instance.addSerialBefore = makePetal();
            instance.addParallelBefore = makePetal();
            configureAddActions(instance, id);
            instance.name->onClick = [this, id, label]
            {
                if (moveSourceId != 0)
                {
                    if (moveSourceId != id
                        && editRouting(ava::routing::moveInstanceSerialAfter, moveSourceId, id))
                    {
                        clearMove();
                        notifyTopologyChanged();
                    }
                }
                else if (openAction != nullptr)
                    openAction(id, label);
            };

            instance.revealBody->addAndMakeVisible(*instance.name);
            for (auto* petal : { instance.addSerial.get(), instance.addSerialBefore.get(),
                                 instance.addParallel.get(), instance.addParallelBefore.get() })
                instance.revealBody->addAndMakeVisible(*petal);
            controls.emplace(id, std::move(instance));
        }
        for (const auto& node : displayedState.nodes)
        {
            if (node.parallelNext == 0 || std::any_of(displayedState.nodes.begin(), displayedState.nodes.end(),
                [id = node.id](const auto& candidate) { return candidate.parallelNext == id; }))
                continue;
            GroupControls group;
            group.revealClip = std::make_unique<juce::Component>();
            group.revealClip->getProperties().set("controlViewportClip", true);
            group.revealBody = std::make_unique<juce::Component>();
            group.revealClip->setInterceptsMouseClicks(false, true);
            group.revealBody->setInterceptsMouseClicks(false, true);
            group.revealClip->addAndMakeVisible(*group.revealBody);
            addAndMakeVisible(*group.revealClip);
            group.revealClip->setVisible(containersVisible());
            auto previous = previousGroups.find(node.id);
            if (previous == previousGroups.end())
            {
                auto bestOverlap = 1;
                for (auto candidate = previousGroups.begin(); candidate != previousGroups.end(); ++candidate)
                {
                    if (candidate->second.appearance == nullptr) continue;
                    const auto overlap = parallelOverlap(previousState, candidate->first, displayedState, node.id);
                    if (overlap > bestOverlap) { bestOverlap = overlap; previous = candidate; }
                }
            }
            if (previous != previousGroups.end())
            {
                group.appearance = std::move(previous->second.appearance);
                group.appearance->setExpanded(true, transitionStart);
                group.direction = previous->second.direction;
                group.hasStartBounds = true;
                group.startBounds = topologyWasChanged ? previous->second.fullBounds : previous->second.startBounds;
                group.fullBounds = previous->second.fullBounds;
            }
            else
            {
                group.appearance = std::make_unique<DisclosureAnimation>();
                if (previousControls.empty()) group.appearance->snap(true);
                else group.appearance->setExpanded(true, transitionStart);
                group.direction = pendingDirection.value_or(inferDirection(node.id, previousControls));
            }
            group.appearance->onUpdate = [this, id = node.id] { updateGroupAppearance(id); repaint(); scheduleRetiredCleanup(); };
            for (int index = 0; index < 4; ++index)
            {
                group.petals[static_cast<size_t>(index)] = makePetal();
                constexpr const char* directions[] {"before", "after", "above", "below"};
                group.petals[static_cast<size_t>(index)]->setComponentID(juce::String("routing.petal.") + directions[index]);
                group.petals[static_cast<size_t>(index)]->onClick = [this, id = node.id, index]
                {
                    auto changed = false;
                    if (moveSourceId != 0)
                    {
                        switch (index)
                        {
                            case 0: changed = editRouting(ava::routing::moveInstanceBeforeGroup, moveSourceId, id); break;
                            case 1: changed = editRouting(ava::routing::moveInstanceAfterGroup, moveSourceId, id); break;
                            case 2: changed = editRouting(ava::routing::moveParallelOutsideGroup, moveSourceId, id, true); break;
                            case 3: changed = editRouting(ava::routing::moveParallelOutsideGroup, moveSourceId, id, false); break;
                        }
                        if (changed) clearMove();
                    }
                    else
                    {
                        pendingDirection = directionFor(index < 2, index == 0 || index == 2);
                        auto& petal = *groupControls.at(id).petals[static_cast<size_t>(index)];
                        pendingOrigin = SlideOrigin {id, true, petal.getSide(), getLocalArea(&petal, petal.getLocalBounds())};
                        switch (index)
                        {
                            case 0: changed = editRouting(ava::routing::insertInstanceBeforeGroup, id); break;
                            case 1: changed = editRouting(ava::routing::insertInstanceAfterGroup, id); break;
                            case 2: changed = editRouting(ava::routing::insertParallelOutsideGroup, id, true); break;
                            case 3: changed = editRouting(ava::routing::insertParallelOutsideGroup, id, false); break;
                        }
                    }
                    if (changed) notifyTopologyChanged();
                    else { pendingDirection.reset(); pendingOrigin.reset(); }
                };
            }
            group.compensation = makeButton("C");
            group.compensation->setComponentID("routing.compensation." + juce::String(node.id));
            configureContainerButton(*group.compensation);
            group.compensation->setSelectionHighlighted(node.compensated);
            group.compensation->onClick = [this, id = node.id] {
                if (auto* target = processor.findHostSlotTarget(ava::routing::compensationTargetId(id))) {
                    target->beginChangeGesture(); target->setValueNotifyingHost(1.0f - target->getValue()); target->endChangeGesture();
                }
                notifyTopologyChanged();
            };
            group.compensation->onModifiedClick = [this, id = node.id](juce::ModifierKeys modifiers) {
                if (modifiers.isCtrlDown())
                    if (auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>())
                        editor->handleHostSlotAssignRequest(ava::routing::compensationTargetId(id));
            };
            group.revealBody->addAndMakeVisible(*group.compensation);
            for (auto& petal : group.petals) group.revealBody->addAndMakeVisible(*petal);
            groupControls.emplace(node.id, std::move(group));
        }
        for (auto& [id, item] : previousControls)
            if (item.appearance != nullptr)
            {
                item.revealClip->setInterceptsMouseClicks(false, false);
                item.direction = removalDirection(previousState, id, item.fullBounds);
                item.origin.reset();
                item.appearance->setExpanded(false, transitionStart);
                retiringControls.insert_or_assign(id, std::move(item));
            }
        for (auto& [id, item] : previousGroups)
            if (item.appearance != nullptr)
            {
                item.revealClip->setInterceptsMouseClicks(false, false);
                item.direction = removalDirection(previousState, id, item.fullBounds, true);
                item.appearance->setExpanded(false, transitionStart);
                retiringGroups.insert_or_assign(id, std::move(item));
            }
        if (topologyWasChanged && hadControls)
        {
            ++reflowGeneration;
            reflow.snap(false);
            reflow.setExpanded(true, transitionStart);
        }
        pendingDirection.reset();
        pendingOrigin.reset();
        updateInstanceLabels();
        updateMoveTargets();
        updateBypassPrompts();
        // New routing groups must obey the hidden default immediately, not
        // wait for the user to press Shift or for a later topology change.
        applyContainerClipVisibility();
        resized();
        for (auto& [id, item] : retiringControls) { juce::ignoreUnused(item); updateInstanceAppearance(id); }
        for (auto& [id, item] : retiringGroups) { juce::ignoreUnused(item); updateGroupAppearance(id); }
        std::erase_if(retiringSignalPaths, [this](const auto& path) { return !hasRetiringSignalVisual(path); });
        scheduleRetiredCleanup();
    }

private:
    static int parallelOverlap(const ava::routing::State& oldState, const int oldHead,
                               const ava::routing::State& newState, const int newHead)
    {
        auto overlap = 0;
        for (auto oldId = oldHead; oldId != 0;)
        {
            const auto* oldNode = ava::routing::findStateNode(oldState.nodes, oldId);
            if (oldNode == nullptr) break;
            for (auto newId = newHead; newId != 0;)
            {
                const auto* newNode = ava::routing::findStateNode(newState.nodes, newId);
                if (newNode == nullptr) break;
                if (oldId == newId) { ++overlap; break; }
                newId = newNode->parallelNext;
            }
            oldId = oldNode->parallelNext;
        }
        return overlap;
    }

    SlideDirection directionFor(const bool serial, const bool before) const
    {
        return serial ? (before ? SlideDirection::left : SlideDirection::right)
                      : (before ? SlideDirection::up : SlideDirection::down);
    }

    SlideDirection inferDirection(const int id, const std::map<int, InstanceControls>& previous) const
    {
        const auto* node = findNode(id);
        if (node == nullptr) return directionFor(true, false);
        if (previous.contains(id) && node->parallelNext != 0 && ! previous.contains(node->parallelNext))
            return directionFor(false, false);
        for (const auto& parent : displayedState.nodes)
        {
            if (! previous.contains(parent.id)) continue;
            if (parent.parallelNext == id) return directionFor(false, false);
            if (parent.serialNext == id) return directionFor(true, false);
        }
        if (previous.contains(node->parallelNext)) return directionFor(false, true);
        if (previous.contains(node->serialNext)) return directionFor(true, true);
        return directionFor(true, false);
    }

    SlideDirection removalDirection(const ava::routing::State& oldState, const int id,
                                    const juce::Rectangle<int> bounds, const bool group = false) const
    {
        const auto towardSurvivor = [this, bounds](const juce::Rectangle<int> survivor)
        {
            const auto delta = survivor.getCentre() - bounds.getCentre();
            // Collapse towards the surviving neighbour; entry directions use the inverse offset.
            if (std::abs(delta.x) > std::abs(delta.y))
                return delta.x < 0 ? SlideDirection::right : SlideDirection::left;
            if (delta.y != 0)
                return delta.y < 0 ? SlideDirection::down : SlideDirection::up;
            return directionFor(true, true);
        };
        const auto survivingBounds = [this](const int candidate) -> std::optional<juce::Rectangle<int>>
        {
            if (const auto item = controls.find(candidate); item != controls.end()) return item->second.fullBounds;
            return std::nullopt;
        };
        const auto* oldNode = ava::routing::findStateNode(oldState.nodes, id);
        if (! group && oldNode != nullptr)
        {
            // Parallel neighbours determine the side within the current container first.
            for (const auto& node : oldState.nodes)
                if (node.parallelNext == id)
                    if (auto neighbour = survivingBounds(node.id)) return towardSurvivor(*neighbour);
            if (auto neighbour = survivingBounds(oldNode->parallelNext)) return towardSurvivor(*neighbour);
            for (const auto& node : oldState.nodes)
                if (node.serialNext == id || node.groupNext == id)
                    if (auto neighbour = survivingBounds(node.id)) return towardSurvivor(*neighbour);
            if (auto neighbour = survivingBounds(oldNode->serialNext)) return towardSurvivor(*neighbour);
            if (auto neighbour = survivingBounds(oldNode->groupNext)) return towardSurvivor(*neighbour);
        }
        const InstanceControls* nearest = nullptr;
        auto distance = std::numeric_limits<double>::max();
        for (const auto& [candidate, item] : controls)
        {
            juce::ignoreUnused(candidate);
            const auto nextDistance = bounds.getCentre().getDistanceFrom(item.fullBounds.getCentre());
            if (nextDistance < distance) { distance = nextDistance; nearest = &item; }
        }
        return nearest != nullptr ? towardSurvivor(nearest->fullBounds) : directionFor(true, true);
    }

    static juce::Point<int> slideOffset(const SlideDirection direction,
                                      const juce::Rectangle<int> bounds, const double progress)
    {
        const auto x = juce::roundToInt(bounds.getWidth() * (1.0 - progress));
        const auto y = juce::roundToInt(bounds.getHeight() * (1.0 - progress));
        switch (direction)
        {
            case SlideDirection::up: return { 0, y };
            case SlideDirection::down: return { 0, -y };
            case SlideDirection::left: return { x, 0 };
            case SlideDirection::right: return { -x, 0 };
        }
        return {};
    }

    juce::Rectangle<int> interpolateBounds(const juce::Rectangle<int> start,
                                           const juce::Rectangle<int> target, const bool hasStart) const
    {
        if (! hasStart || ! reflow.isRunning()) return target;
        const auto mix = [amount = reflow.value()] (const int from, const int to)
        {
            return juce::roundToInt(from + (to - from) * amount);
        };
        return { mix(start.getX(), target.getX()), mix(start.getY(), target.getY()),
                 mix(start.getWidth(), target.getWidth()), mix(start.getHeight(), target.getHeight()) };
    }

    template <typename Map>
    static typename Map::mapped_type* findVisual(Map& active, Map& retiring, const int id)
    {
        if (const auto found = active.find(id); found != active.end()) return &found->second;
        if (const auto found = retiring.find(id); found != retiring.end()) return &found->second;
        return nullptr;
    }

    void cleanupRetiredVisuals()
    {
        retiredCleanupPending = false;
        const auto finished = [] (const auto& item)
        {
            return ! item.second.appearance->isRunning() && ! item.second.appearance->targetExpanded();
        };
        const auto count = retiringControls.size() + retiringGroups.size();
        std::erase_if(retiringControls, finished);
        std::erase_if(retiringGroups, finished);
        std::erase_if(retiringSignalPaths, [this](const auto& path) { return !hasRetiringSignalVisual(path); });
        if (count != retiringControls.size() + retiringGroups.size())
        {
            if (layoutChanged != nullptr) layoutChanged();
            repaint();
        }
    }

    void scheduleRetiredCleanup()
    {
        if (retiredCleanupPending || (retiringControls.empty() && retiringGroups.empty())) return;
        retiredCleanupPending = true;
        juce::MessageManager::callAsync([safeThis = juce::Component::SafePointer<Content>(this)]
        {
            if (safeThis != nullptr) safeThis->cleanupRetiredVisuals();
        });
    }

    bool isSoloBypassed(const int id) const
    {
        const auto instance = id == displayedState.rootInstanceId
            ? std::shared_ptr<AvaAudioProcessor> {} : processor.getRoutingInstanceHandle(id);
        const auto* target = id == displayedState.rootInstanceId ? &processor : instance.get();
        return target != nullptr && target->isInstanceSoloBypassed();
    }

    float getBypassValue(const int id) const
    {
        if (isSoloBypassed(id)) return 1.0f;
        return processor.getRoutingInstanceBypassValue(id);
    }

    void toggleBypass(const int id)
    {
        if (isSoloBypassed(id)) return;
        if (auto* parameter = processor.findHostSlotTarget(ava::routing::bypassTargetId(id)))
        {
            const auto value = getBypassValue(id) < 0.5f ? 1.0f : 0.0f;
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value);
            parameter->endChangeGesture();
        }
        updateBypassPrompts();
    }

    void updateBypassPrompts()
    {
        for (auto& [id, instance] : controls)
        {
            const auto value = getBypassValue(id);
            instance.name->setFillColour(juce::Colour(value >= 0.5f ? 0xff222222 : 0xff444444));
            instance.name->refreshBottomIndicatorStates();
            instance.name->repaint();
        }
    }

    void configureAddActions(InstanceControls& instance, const int id)
    {
        const auto action = [this, id](const bool serial, const bool before, RoutingPetal& petal)
        {
            auto changed = false;
            if (moveSourceId != 0)
            {
                if (moveSourceId != id)
                    changed = serial
                        ? (before ? editRouting(ava::routing::moveInstanceSerialBefore, moveSourceId, id)
                                  : editRouting(ava::routing::moveInstanceSerialAfter, moveSourceId, id))
                        : (before ? editRouting(ava::routing::moveInstanceParallelBefore, moveSourceId, id)
                                  : editRouting(ava::routing::moveInstanceParallelTo, moveSourceId, id));
                if (changed) clearMove();
            }
            else
            {
                pendingDirection = directionFor(serial, before);
                pendingOrigin = SlideOrigin {id, false, petal.getSide(), getLocalArea(&petal, petal.getLocalBounds())};
                changed = serial
                    ? (before ? editRouting(ava::routing::insertSerialInstanceBefore, id)
                              : editRouting(ava::routing::insertSerialInstance, id))
                    : (before ? editRouting(ava::routing::insertParallelInstanceBefore, id)
                              : editRouting(ava::routing::insertParallelInstance, id));
            }
            if (changed) notifyTopologyChanged();
            else { pendingDirection.reset(); pendingOrigin.reset(); }
        };
        instance.addSerial->setComponentID("routing.petal.after");
        instance.addSerialBefore->setComponentID("routing.petal.before");
        instance.addParallelBefore->setComponentID("routing.petal.above");
        instance.addParallel->setComponentID("routing.petal.below");
        instance.addSerial->onClick = [action, petal = instance.addSerial.get()] { action(true, false, *petal); };
        instance.addSerialBefore->onClick = [action, petal = instance.addSerialBefore.get()] { action(true, true, *petal); };
        instance.addParallel->onClick = [action, petal = instance.addParallel.get()] { action(false, false, *petal); };
        instance.addParallelBefore->onClick = [action, petal = instance.addParallelBefore.get()] { action(false, true, *petal); };

    }

    const ava::routing::State::Node* findNode(const int id) const
    {
        return ava::routing::findStateNode(displayedState.nodes, id);
    }

    const ava::routing::State::Node* findLayoutNode(int id) const
    {
        return ava::routing::findStateNode(layoutState.nodes, id);
    }

    void rebuildLayoutState()
    {
        // Container visibility only changes the frame drawing; the signal path keeps its nesting.
        layoutState = displayedState;
    }

    struct Size
    {
        int width = 0;
        int height = 0;
    };

    int signalGroupPadding() const
    {
        return containersVisible() ? groupPadding : minimumSignalLeg + signalClearance + 1;
    }

    // The containing UNIT block already provides the standard 8 px inset
    // between its inner frame and this viewport. Do not add a second gap.
    int canvasPaddingX() const { return 0; }
    int terminalTailWidth() const { return getUiGap(*this) + signalClearance; }

    std::optional<std::pair<juce::Point<int>, SignalVisual>> findInputSignal(int id) const
    {
        const auto* first = findLayoutNode(id);
        if (first == nullptr) return {};
        if (first->parallelNext != 0) {
            const auto group = groupBodies.find(id);
            if (group == groupBodies.end()) return {};
            const auto& bounds = group->second;
            return std::pair {juce::Point<int>(bounds.getX() - (containersVisible() ? petalExtent : 0), bounds.getCentreY()), SignalVisual {id, true}};
        }
        if (first->container) return findInputSignal(first->serialNext);
        const auto instance = locations.find(id);
        if (instance == locations.end()) return {};
        return std::pair {juce::Point<int>(instance->second.getX() - petalExtent, instance->second.getCentreY()), SignalVisual {id, false}};
    }

    struct SignalPorts
    {
        SignalPorts(juce::Point<int> inputIn, juce::Point<int> outputIn) : input(inputIn), output(outputIn) {}
        juce::Point<int> input, output;
        bool inputHasPetal = true, outputHasPetal = true;
        SignalVisual inputVisual, outputVisual;
    };

    double signalProgress(const SignalVisual visual) const
    {
        if (visual.id == 0) return 1.0;
        const auto progress = [id = visual.id](const auto& active, const auto& retiring)
        {
            if (const auto found = active.find(id); found != active.end()) return found->second.appearance->value();
            if (const auto found = retiring.find(id); found != retiring.end()) return found->second.appearance->value();
            return 1.0;
        };
        return visual.group ? progress(groupControls, retiringGroups) : progress(controls, retiringControls);
    }

    bool hasRetiringSignalVisual(const SignalPath& path) const
    {
        const auto retiring = [this](const SignalVisual visual)
        { return visual.group ? retiringGroups.contains(visual.id) : retiringControls.contains(visual.id); };
        return retiring(path.from) || retiring(path.to);
    }

    void drawSignalPath(juce::Graphics& g, const SignalPath& path) const
    {
        const auto from = signalProgress(path.from), to = signalProgress(path.to);
        const auto fromFades = path.from.group && containersVisible();
        const auto toFades = path.to.group && containersVisible();
        const auto alpha = juce::jmin(fromFades ? from : 1.0, toFades ? to : 1.0);
        const auto fromReveal = fromFades ? 1.0 : from;
        const auto toReveal = toFades ? 1.0 : to;
        const auto reveal = juce::jmin(fromReveal, toReveal);
        if (alpha <= 0.0 || reveal <= 0.0) return;
        auto points = path.points;
        if (fromReveal < toReveal) std::reverse(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(path.count));
        auto remaining = 0.0;
        for (size_t i = 1; i < path.count; ++i)
            remaining += std::abs(points[i].x - points[i-1].x) + std::abs(points[i].y - points[i-1].y);
        remaining *= reveal;
        juce::Path shape;
        for (size_t i = 1; i < path.count && remaining > 0.0; ++i)
        {
            const auto start = points[i-1], end = points[i];
            const auto length = std::abs(end.x-start.x) + std::abs(end.y-start.y);
            if (length == 0) continue;
            const auto amount = juce::jmin(1.0, remaining / length);
            const juce::Point<int> tip { start.x + juce::roundToInt((end.x-start.x) * amount),
                                         start.y + juce::roundToInt((end.y-start.y) * amount) };
            if (start.y == tip.y)
                shape.addRectangle(float(juce::jmin(start.x, tip.x)-1), float(start.y-1), float(std::abs(tip.x-start.x)+2), 2.0f);
            else
                shape.addRectangle(float(start.x-1), float(juce::jmin(start.y, tip.y)-1), 2.0f, float(std::abs(tip.y-start.y)+2));
            remaining -= length;
        }
        g.setColour(uiWhite.withMultipliedAlpha(float(alpha)));
        g.fillPath(shape);
    }

    void paintSignalPaths(juce::Graphics& graphics) const
    {
        for (const auto& path : retiringSignalPaths)
            if (hasRetiringSignalVisual(path)) drawSignalPath(graphics, path);
        for (const auto& path : signalPaths) drawSignalPath(graphics, path);
    }

    void paintSignalLine(juce::Graphics&, juce::Point<int> from, juce::Point<int> to,
                         SignalVisual visual) const
    {
        if (from == to) return;
        const SignalPath path { {from, to, {}, {}}, 2, visual, visual };
        signalPaths.push_back(path);
    }

    void paintSignalConnection(juce::Graphics&, juce::Point<int> from, juce::Point<int> to,
                               SignalVisual fromVisual, SignalVisual toVisual) const
    {
        signalPaths.push_back(makeSignalConnection(from, to, fromVisual, toVisual, (from.x + to.x) / 2));
    }

    static SignalPath makeSignalConnection(juce::Point<int> from, juce::Point<int> to,
                                           SignalVisual fromVisual, SignalVisual toVisual, int elbow)
    {
        return { {from, {elbow, from.y}, {elbow, to.y}, to}, 4, fromVisual, toVisual };
    }

    std::optional<SignalPorts> paintSignalBranch(juce::Graphics& g, const int id) const
    {
        const auto* node = findLayoutNode(id);
        if (node == nullptr) return std::nullopt;
        if (node->container) return paintSignalGroup(g, node->serialNext);
        const auto found = locations.find(id);
        if (found == locations.end()) return std::nullopt;
        const auto bounds = found->second;
        SignalPorts ports { { bounds.getX(), bounds.getCentreY() }, { bounds.getRight(), bounds.getCentreY() } };
        ports.inputVisual = ports.outputVisual = {id, false};
        if (const auto next = paintSignalGroup(g, node->serialNext))
        {
            paintSignalConnection(g, ports.output, next->input, ports.outputVisual, next->inputVisual);
            ports.output = next->output;
            ports.outputHasPetal = next->outputHasPetal;
            ports.outputVisual = next->outputVisual;
        }
        return ports;
    }

    std::optional<SignalPorts> paintSignalGroup(juce::Graphics& g, const int id) const
    {
        const auto* first = findLayoutNode(id);
        if (first == nullptr) return std::nullopt;
        std::vector<SignalPorts> branches;
        for (auto* node = first; node != nullptr; node = findLayoutNode(node->parallelNext))
            if (const auto ports = paintSignalBranch(g, node->id)) branches.push_back(*ports);
        if (branches.empty()) return std::nullopt;
        auto ports = branches.front();
        if (branches.size() > 1)
        {
            const auto found = groupBodies.find(id);
            if (found == groupBodies.end()) return std::nullopt;
            const auto bounds = found->second;
            ports = SignalPorts { { bounds.getX(), bounds.getCentreY() }, { bounds.getRight(), bounds.getCentreY() } };
            ports.inputVisual = ports.outputVisual = {id, true};
            ports.inputHasPetal = ports.outputHasPetal = containersVisible();
            const auto inset = containersVisible() ? busInset : 0;
            const auto inputBus = ports.input.translated(inset, 0);
            const auto outputBus = ports.output.translated(-inset, 0);
            paintSignalLine(g, ports.input, inputBus, ports.inputVisual);
            paintSignalLine(g, outputBus, ports.output, ports.outputVisual);
            auto inputMin = inputBus.y;
            auto inputMax = inputMin;
            auto outputMin = outputBus.y;
            auto outputMax = outputMin;
            for (const auto& branch : branches)
            {
                const auto in = juce::Point<int>(inputBus.x, branch.input.y);
                const auto out = juce::Point<int>(outputBus.x, branch.output.y);
                inputMin = juce::jmin(inputMin, in.y);
                inputMax = juce::jmax(inputMax, in.y);
                outputMin = juce::jmin(outputMin, out.y);
                outputMax = juce::jmax(outputMax, out.y);
                paintSignalConnection(g, in, branch.input, ports.inputVisual, branch.inputVisual);
                paintSignalConnection(g, branch.output, out, branch.outputVisual, ports.outputVisual);
            }
            paintSignalLine(g, juce::Point<int>(inputBus.x, inputMin),
                               juce::Point<int>(inputBus.x, inputMax), ports.inputVisual);
            paintSignalLine(g, juce::Point<int>(outputBus.x, outputMin),
                               juce::Point<int>(outputBus.x, outputMax), ports.outputVisual);
        }
        if (const auto next = paintSignalGroup(g, first->groupNext))
        {
            paintSignalConnection(g, ports.output, next->input, ports.outputVisual, next->inputVisual);
            ports.output = next->output;
            ports.outputHasPetal = next->outputHasPetal;
            ports.outputVisual = next->outputVisual;
        }
        return ports;
    }

    Size measureBranch(const int id, const int width) const
    {
        const auto* node = findLayoutNode(id);
        if (node == nullptr)
            return {};

        const auto child = measureGroup(node->serialNext, width);
        if (node->container) return child;
        return { width + 2 * petalExtent + (child.width == 0 ? 0 : serialGap + child.width),
                 juce::jmax(width + 2 * petalExtent, child.height) };
    }

    Size measureGroupBody(const int firstId, const int width) const
    {
        const auto* first = findLayoutNode(firstId);
        if (first == nullptr)
            return {};

        const auto framed = first->parallelNext != 0;
        Size result;
        auto count = 0;
        for (auto* node = first; node != nullptr; node = findLayoutNode(node->parallelNext))
        {
            const auto branch = measureBranch(node->id, width);
            result.width = juce::jmax(result.width, branch.width);
            result.height += branch.height;
            ++count;
        }
        result.height += getUiGap(*this) * (count - 1);
        if (framed)
        {
            result.width += 2 * signalGroupPadding();
            result.height += groupHeaderHeight() + 2 * getUiGap(*this);
        }
        return result;
    }

    Size measureGroup(const int firstId, const int width) const
    {
        const auto* first = findLayoutNode(firstId);
        if (first == nullptr)
            return {};

        const auto body = measureGroupBody(firstId, width);
        const auto inset = first->parallelNext != 0 && containersVisible() ? petalExtent : 0;
        const Size outer { body.width + 2 * inset, body.height + 2 * inset };
        if (first->groupNext != 0)
        {
            const auto child = measureGroup(first->groupNext, width);
            return { outer.width + serialGap + child.width, juce::jmax(outer.height, child.height) };
        }
        return outer;
    }

    int groupHeaderHeight() const { return juce::jmax(2 * getUiGap(*this), getCompactControlSize(*this) / 2 + 2 * controlBlockHeaderButtonInset); }

    int instanceSize() const
    {
        return getSquareControlSize(*this);
    }

    void layoutBranch(const int id, const int x, const int y, const int width)
    {
        const auto* node = findLayoutNode(id);
        if (node != nullptr && node->container)
        {
            layoutGroup(node->serialNext, x, y, width);
            locations[id] = groupBodies[node->serialNext];
            return;
        }
        const auto found = controls.find(id);
        if (node == nullptr || found == controls.end())
            return;

        const auto branch = measureBranch(id, width);
        const auto nameX = petalExtent + x;
        const auto nameY = petalExtent + y + (branch.height - width - 2 * petalExtent) / 2;
        const juce::Rectangle<int> target { nameX - petalExtent, nameY - petalExtent,
                                            width + 2 * petalExtent, width + 2 * petalExtent };
        found->second.fullBounds = interpolateBounds(found->second.startBounds, target, found->second.hasStartBounds);
        found->second.name->setBounds(petalExtent, petalExtent, width, width);
        locations[id] = found->second.fullBounds.reduced(petalExtent);
        auto placePetal = [&target](RoutingPetal& petal, const juce::Rectangle<int> bounds, RoutingPetal::Side side)
        {
            petal.setSide(side);
            petal.setBounds(bounds.translated(-target.getX(), -target.getY()));
        };
        const juce::Rectangle<int> rightAdd(nameX + width + petalGap, nameY, petalThickness, width);
        const juce::Rectangle<int> bottomAdd(nameX, nameY + width + petalGap, width, petalThickness);
        const juce::Rectangle<int> leftAdd(nameX - petalExtent, nameY, petalThickness, width);
        const juce::Rectangle<int> topAdd(nameX, nameY - petalExtent, width, petalThickness);
        placePetal(*found->second.addSerial, rightAdd, RoutingPetal::Side::right);
        placePetal(*found->second.addSerialBefore, leftAdd, RoutingPetal::Side::left);
        placePetal(*found->second.addParallel, bottomAdd, RoutingPetal::Side::bottom);
        placePetal(*found->second.addParallelBefore, topAdd, RoutingPetal::Side::top);

        updateInstanceAppearance(id);

        if (node->serialNext != 0)
        {
            const auto child = measureGroup(node->serialNext, width);
            layoutGroup(node->serialNext, x + width + 2 * petalExtent + serialGap,
                        y + (branch.height - child.height) / 2, width);
        }
    }

    void layoutGroup(const int firstId, const int x, const int y, const int width)
    {
        const auto* first = findLayoutNode(firstId);
        if (first == nullptr)
            return;

        const auto parallel = first->parallelNext != 0;
        const auto framed = parallel;
        const auto serialPadding = parallel ? signalGroupPadding() : 0;
        const auto group = measureGroup(firstId, width);
        const auto body = measureGroupBody(firstId, width);
        const auto inset = framed && containersVisible() ? petalExtent : 0;
        const auto bodyX = inset + x;
        const auto bodyY = inset + y + (group.height - body.height - 2 * inset) / 2;
        groupBodies[firstId] = { bodyX, bodyY, body.width, body.height };
        if (framed && containersVisible())
        {
            frames[firstId] = groupBodies[firstId];
            if (const auto buttons = groupControls.find(firstId); buttons != groupControls.end())
            {
                buttons->second.fullBounds = interpolateBounds(buttons->second.startBounds,
                    groupBodies[firstId].expanded(petalExtent), buttons->second.hasStartBounds);
                const auto currentBody = buttons->second.fullBounds.reduced(petalExtent);
                frames[firstId] = groupBodies[firstId] = currentBody;
                const auto currentX = currentBody.getX(), currentY = currentBody.getY();
                const auto currentWidth = currentBody.getWidth(), currentHeight = currentBody.getHeight();
                const std::array<juce::Rectangle<int>, 4> bounds {
                    juce::Rectangle<int>(currentX, currentY - petalExtent, currentWidth, petalThickness),
                    juce::Rectangle<int>(currentX, currentY + currentHeight + petalGap, currentWidth, petalThickness),
                    juce::Rectangle<int>(currentX - petalExtent, currentY, petalThickness, currentHeight),
                    juce::Rectangle<int>(currentX + currentWidth + petalGap, currentY, petalThickness, currentHeight) };
                const std::array<RoutingPetal::Side, 4> sides { RoutingPetal::Side::top,
                    RoutingPetal::Side::bottom, RoutingPetal::Side::left, RoutingPetal::Side::right };
                for (int index = 0; index < 4; ++index)
                {
                    const auto physicalSide = index < 2 ? index + 2 : index - 2;
                    auto& petal = buttons->second.petals[static_cast<size_t>(index)];
                    petal->setSide(sides[static_cast<size_t>(physicalSide)]);
                    petal->setBounds(bounds[static_cast<size_t>(physicalSide)].translated(
                        -buttons->second.fullBounds.getX(), -buttons->second.fullBounds.getY()));
                }
                const auto cSize = getCompactControlSize(*this) / 2;
                buttons->second.compensation->setBounds(currentX + currentWidth - cSize - controlBlockHeaderButtonInset - buttons->second.fullBounds.getX(),
                    currentY + controlBlockHeaderButtonInset - buttons->second.fullBounds.getY(), cSize, cSize);
                updateGroupAppearance(firstId);
            }
        }

        auto branchY = bodyY + (framed ? groupHeaderHeight() : 0);
        for (auto* node = first; node != nullptr; node = findLayoutNode(node->parallelNext))
        {
            const auto branch = measureBranch(node->id, width);
            layoutBranch(node->id, bodyX + serialPadding, branchY, width);
            branchY += branch.height + getUiGap(*this);
        }

        if (first->groupNext != 0)
        {
            const auto child = measureGroup(first->groupNext, width);
            layoutGroup(first->groupNext, bodyX + body.width + inset + serialGap,
                        y + (group.height - child.height) / 2, width);
            return;
        }
    }

    void updateGroupAppearance(const int id)
    {
        auto* item = findVisual(groupControls, retiringGroups, id);
        if (item == nullptr) return;
        auto& group = *item;
        const auto progress = group.appearance->value();
        group.revealClip->setVisible(containersVisible());
        group.revealClip->setBounds(group.fullBounds);
        group.revealBody->setBounds(group.fullBounds.withPosition(0, 0));
        group.revealClip->setAlpha(static_cast<float>(progress));
    }

    void updateInstanceAppearance(const int id)
    {
        auto* item = findVisual(controls, retiringControls, id);
        if (item == nullptr) return;
        auto& instance = *item;
        const auto progress = instance.appearance->value();
        if (instance.origin && progress < 1.0)
        {
            auto origin = instance.origin->bounds;
            const auto usePetal = [this, &origin, &instance](const RoutingPetal& petal)
            {
                if (petal.getSide() == instance.origin->side)
                    origin = getLocalArea(&petal, petal.getLocalBounds());
            };
            if (instance.origin->group)
            {
                if (const auto source = groupControls.find(instance.origin->owner); source != groupControls.end())
                    for (const auto& petal : source->second.petals) usePetal(*petal);
            }
            else if (const auto source = controls.find(instance.origin->owner); source != controls.end())
                for (const auto* petal : {source->second.addSerial.get(), source->second.addSerialBefore.get(),
                                          source->second.addParallel.get(), source->second.addParallelBefore.get()}) usePetal(*petal);
            auto start = instance.fullBounds.withCentre(origin.getCentre());
            auto clip = instance.fullBounds.getUnion(start);
            switch (instance.direction)
            {
                case SlideDirection::down:
                    start.setY(origin.getBottom() - start.getHeight());
                    clip = clip.withTop(origin.getBottom()); break;
                case SlideDirection::up:
                    start.setY(origin.getY());
                    clip = clip.withBottom(origin.getY()); break;
                case SlideDirection::right:
                    start.setX(origin.getRight() - start.getWidth());
                    clip = clip.withLeft(origin.getRight()); break;
                case SlideDirection::left:
                    start.setX(origin.getX());
                    clip = clip.withRight(origin.getX()); break;
            }
            const auto target = instance.fullBounds;
            const auto x = juce::roundToInt(start.getX() + (target.getX()-start.getX()) * progress);
            const auto y = juce::roundToInt(start.getY() + (target.getY()-start.getY()) * progress);
            instance.revealClip->setBounds(clip);
            instance.revealBody->setBounds(target.withPosition(x-clip.getX(), y-clip.getY()));
        }
        else
        {
            if (progress >= 1.0) instance.origin.reset();
            const auto offset = slideOffset(instance.direction, instance.fullBounds, progress);
            instance.revealClip->setBounds(instance.fullBounds);
            instance.revealBody->setBounds(instance.fullBounds.withPosition(offset));
        }
        instance.revealClip->setAlpha(1.0f);
    }

    std::unique_ptr<RoutingPetal> makePetal()
    {
        auto button = std::make_unique<RoutingPetal>();
        addAndMakeVisible(*button);
        return button;
    }

    std::unique_ptr<BoxTextButton> makeButton(const juce::String& text)
    {
        auto button = std::make_unique<BoxTextButton>();
        button->setButtonText(text);
        button->setTextJustification(juce::Justification::centred);
        button->setClickingTogglesState(false);
        addAndMakeVisible(*button);
        return button;
    }

    void updateMoveTargets()
    {
        for (auto& [id, instance] : controls)
        {
            const auto isSource = moveSourceId == id;
            const auto isTarget = moveSourceId != 0 && ! isSource;
            instance.name->getProperties().set("routing.instance.dragging", controlDragActive && isSource);
            instance.name->setDragTargetHighlighted(isTarget || (controlDragActive && isSource));
            instance.name->setMoveTargetAvailable(isTarget);
            const auto canInsert = moveSourceId == 0
                ? ava::routing::getInstanceCount(displayedState) < ava::routing::maximumInstanceCount : true;
            for (auto* petal : { instance.addSerial.get(), instance.addParallel.get(),
                                 instance.addSerialBefore.get(), instance.addParallelBefore.get() })
            {
                petal->setDragSource(controlDragActive && isSource);
                petal->setEnabled(canInsert && ! (moveSourceId != 0 && isSource));
            }
        }
        for (auto& [id, group] : groupControls)
        {
            juce::ignoreUnused(id);
            for (auto& petal : group.petals)
            {
                petal->setEnabled(moveSourceId != 0 || ava::routing::getInstanceCount(displayedState) < ava::routing::maximumInstanceCount);
            }
        }
    }

    void finishControlDrag(const juce::MouseEvent& event, const bool released)
    {
        if (moveSourceId == 0)
            return;
        const auto position = getLocalPoint(event.eventComponent, event.position);
        std::function<void()> action;
        juce::Button* hoveredTarget = nullptr;
        const auto petalAction = [this, position] (RoutingPetal& petal) -> std::function<void()>
        {
            const auto local = petal.getLocalPoint(this, position).roundToInt();
            return petal.isShowing() && petal.isEnabled() && petal.hitTest(local.x, local.y) ? petal.onClick : nullptr;
        };
        for (auto& [id, instance] : controls)
        {
            if (id != moveSourceId && getLocalArea(instance.name.get(), instance.name->getLocalBounds()).toFloat().contains(position))
            {
                action = instance.name->onClick;
                hoveredTarget = instance.name.get();
            }
            for (auto* petal : { instance.addSerial.get(), instance.addSerialBefore.get(),
                                 instance.addParallel.get(), instance.addParallelBefore.get() })
                if (auto hit = petalAction(*petal))
                {
                    action = std::move(hit);
                    hoveredTarget = petal;
                }
        }
        for (auto& [id, group] : groupControls)
        {
            juce::ignoreUnused(id);
            for (auto& petal : group.petals)
                if (auto hit = petalAction(*petal))
                {
                    action = std::move(hit);
                    hoveredTarget = petal.get();
                }
        }
        updateControlDragHover(hoveredTarget);
        if (! released)
            return;
        controlDragActive = false;
        if (action != nullptr)
            action();
        clearMove();
    }

    void updateControlDragHover(juce::Button* target)
    {
        const auto update = [target] (juce::Button& button)
        {
            button.setState(&button == target ? juce::Button::buttonOver : juce::Button::buttonNormal);
        };
        for (auto& [id, instance] : controls)
        {
            juce::ignoreUnused(id);
            instance.name->setState(instance.name.get() == target || (controlDragActive && id == moveSourceId)
                ? juce::Button::buttonOver : juce::Button::buttonNormal);
            for (auto* petal : { instance.addSerial.get(), instance.addSerialBefore.get(),
                                 instance.addParallel.get(), instance.addParallelBefore.get() })
                update(*petal);
        }
        for (auto& [id, group] : groupControls)
        {
            juce::ignoreUnused(id);
            for (auto& petal : group.petals)
                update(*petal);
        }
    }

    void clearMove()
    {
        controlDragActive = false;
        updateControlDragHover(nullptr);
        moveSourceId = 0;
        updateMoveTargets();
    }

    bool editRouting(bool (*edit)(juce::ValueTree&, int), int id)
    {
        return InstanceLinkRegistry::instance().editRouting(processor,
            [edit](juce::ValueTree& tree, int first, int) { return edit(tree, first); }, id);
    }

    bool editRouting(bool (*edit)(juce::ValueTree&, int, int), int first, int second)
    {
        return InstanceLinkRegistry::instance().editRouting(processor, edit, first, second);
    }

    template <typename... Args>
    bool editRouting(bool (*edit)(juce::ValueTree&, int, Args...), int first, Args... args)
    {
        return InstanceLinkRegistry::instance().editRouting(processor,
            [edit, args...](juce::ValueTree& tree, int id, int) { return edit(tree, id, args...); }, first);
    }

    bool editRouting(bool (*edit)(juce::ValueTree&, int, int, bool), int first, int second, bool before)
    {
        return InstanceLinkRegistry::instance().editRouting(processor,
            [edit, before](juce::ValueTree& tree, int from, int to) { return edit(tree, from, to, before); },
            first, second);
    }

    void notifyTopologyChanged()
    {
        if (topologyChanged != nullptr)
            topologyChanged();
    }

    AvaAudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::function<void(int, const juce::String&)> openAction;
    std::function<void(int, const juce::String&, juce::Component&)> renameAction;
    std::function<void()> topologyChanged;
    std::function<void()> layoutChanged;
    ava::routing::State displayedState, layoutState;
    std::map<int, InstanceControls> controls;
    std::map<int, GroupControls> groupControls, retiringGroups;
    std::map<int, InstanceControls> retiringControls;
    DisclosureAnimation reflow;
    std::optional<SlideDirection> pendingDirection;
    std::optional<SlideOrigin> pendingOrigin;
    int availableHeight = 0;
    int centeringY = 0;
    int reflowGeneration = 0;
    bool retiredCleanupPending = false;
    std::map<int, juce::Rectangle<int>> locations;
    std::map<int, juce::Rectangle<int>> frames;
    std::map<int, juce::Rectangle<int>> groupBodies;
    mutable std::vector<SignalPath> signalPaths;
    std::vector<SignalPath> retiringSignalPaths;
    bool controlDragActive = false;
    int moveSourceId = 0;
    bool showContainers = true;
};

RoutingPanel::RoutingPanel(AvaAudioProcessor& processor)
    : parameters(processor.getValueTreeState()),
      content(std::make_unique<Content>(processor,
          [this] (const int id, const juce::String&)
          {
              openInstance(id);
          },
          [this] (int id, const juce::String& label, juce::Component& anchor)
          {
              if (onRenameRequest) onRenameRequest(id, label, anchor);
          },
          [this]
          {
              if (onTopologyChanged != nullptr)
                  onTopologyChanged();
          },
          [this]
          {
              resized();
          }))
{
    viewport.onViewChanged = [this]
    {
        repaint();
        if (updatingViewport) return;
        syncScrollbars();
        if (!isShowing()) return;
        const auto position = viewport.getViewPosition();
        parameters.state.setProperty(shell_editor_ui::key("scroll.routing.x"), position.x, nullptr);
        parameters.state.setProperty(shell_editor_ui::key("scroll.routing.y"), position.y, nullptr);
    };
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(false, false, false, false);
    viewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(viewport);
    for (auto* bar : {&horizontalScrollBar, &verticalScrollBar})
    {
        bar->setWantsKeyboardFocus(false);
        bar->addListener(this);
        addAndMakeVisible(*bar);
    }
    containersButton = std::make_unique<BoxTextButton>();
    containersButton->setButtonText("CN");
    containersButton->setComponentID("routing.containers");
    configureContainerButton(*containersButton);
    containersButton->getProperties().set("disableDockSelection", true);
    containersButton->onClick = [this]
    {
        const auto visible = !bool(parameters.state.getProperty(shell_editor_ui::key("routing.containers"), true));
        parameters.state.setProperty(shell_editor_ui::key("routing.containers"), visible, nullptr);
        content->setContainersVisible(visible);
        containersButton->setSelectionHighlighted(visible);
    };
    addAndMakeVisible(*containersButton);
    const auto visible = bool(parameters.state.getProperty(shell_editor_ui::key("routing.containers"), true));
    content->setContainersVisible(visible);
    containersButton->setSelectionHighlighted(visible);

    resized();
    updatingViewport = false;
    startTimerHz(10);
}

RoutingPanel::~RoutingPanel()
{
    stopTimer();
    horizontalScrollBar.removeListener(this);
    verticalScrollBar.removeListener(this);
    viewport.onViewChanged = {};
    viewport.setViewedComponent(nullptr, false);
}

void RoutingPanel::setOnBackgroundClick(std::function<void()> action)
{
    content->onBackgroundClick = std::move(action);
}

void RoutingPanel::setOnOpenRoot(std::function<void()> action)
{
    onOpenRoot = std::move(action);
}

void RoutingPanel::setOnOpenInstance(std::function<void(int)> action)
{
    onOpenInstance = std::move(action);
}

void RoutingPanel::setOnTopologyChanged(std::function<void()> action)
{
    onTopologyChanged = std::move(action);
}

void RoutingPanel::setOnRenameRequest(std::function<void(int, const juce::String&, juce::Component&)> action)
{
    onRenameRequest = std::move(action);
}

void RoutingPanel::openInstance(const int instanceId)
{
    if (instanceId == ava::routing::readState(parameters.state).rootInstanceId)
    {
        if (onOpenRoot != nullptr)
            onOpenRoot();
        return;
    }

    if (onOpenInstance != nullptr) onOpenInstance(instanceId);
}

bool RoutingPanel::cancelMoveMode()
{
    return content->cancelMoveMode();
}

void RoutingPanel::restoreUiState()
{
    const juce::ScopedValueSetter<bool> restoreGuard(updatingViewport, true);
    content->refresh();
    const auto visible = bool(parameters.state.getProperty(shell_editor_ui::key("routing.containers"), true));
    content->setContainersVisible(visible);
    containersButton->setSelectionHighlighted(visible);
    restoreScrollPending = true;
    resized();
}

void RoutingPanel::scrollToOrigin()
{
    restoreScrollPending = false;
    scrollReflowActive = false;
    scrollReflowGeneration = content->getReflowGeneration();
    viewport.setViewPosition(0, 0);
    parameters.state.setProperty(shell_editor_ui::key("scroll.routing.x"), 0, nullptr);
    parameters.state.setProperty(shell_editor_ui::key("scroll.routing.y"), 0, nullptr);
    repaint();
}

void RoutingPanel::setLayoutBounds(juce::Rectangle<int> bounds, int length, int centerY)
{
    const bool needsLayout = scrollbarLength != length || graphCenterY != centerY;
    scrollbarLength = length;
    graphCenterY = centerY;
    if (bounds != getBounds())
        setBounds(bounds);
    else if (needsLayout)
        resized();
}

void RoutingPanel::syncScrollbars()
{
    const auto position = viewport.getViewPosition();
    horizontalScrollBar.setRangeLimits(0.0, content->getWidth(), juce::dontSendNotification);
    horizontalScrollBar.setCurrentRange(position.x, viewport.getWidth(), juce::dontSendNotification);
    verticalScrollBar.setRangeLimits(0.0, content->getHeight(), juce::dontSendNotification);
    verticalScrollBar.setCurrentRange(position.y, viewport.getHeight(), juce::dontSendNotification);
}

void RoutingPanel::scrollBarMoved(juce::ScrollBar* bar, double position)
{
    auto view = viewport.getViewPosition();
    if (bar == &horizontalScrollBar) view.x = juce::roundToInt(position);
    else view.y = juce::roundToInt(position);
    viewport.setViewPosition(view);
}

void RoutingPanel::mouseUp(const juce::MouseEvent& event)
{
    if (content->onBackgroundClick != nullptr && !event.mouseWasDraggedSinceMouseDown()
        && !event.mods.isPopupMenu() && getComponentAt(event.getPosition()) == this)
        content->onBackgroundClick();
}

void RoutingPanel::resized()
{
    const juce::ScopedValueSetter<bool> layoutGuard(updatingViewport, true);
    const auto gap = getUiGap(*this);
    const auto barArea = editorScrollBarWidth + gap;
    const auto contentWidth = content->getPreferredWidth();
    const auto contentHeight = content->getPreferredHeight();
    const auto needsHorizontal = contentWidth > getWidth();
    const auto showVertical = contentHeight > getHeight() - (needsHorizontal ? barArea : 0);
    const auto canvasWidth = juce::jmax(0, getWidth() - (showVertical ? barArea : 0));
    const auto showHorizontal = contentWidth > canvasWidth;
    const auto canvasHeight = juce::jmax(0, getHeight() - (showHorizontal ? barArea : 0));

    viewport.setBounds(0, 0, canvasWidth, canvasHeight);
    verticalScrollBar.setBounds(getWidth() - editorScrollBarWidth, 0,
                                editorScrollBarWidth, juce::jmin(scrollbarLength, canvasHeight));
    horizontalScrollBar.setBounds(0, getHeight() - editorScrollBarWidth, getWidth(), editorScrollBarWidth);
    if (containersButton != nullptr)
    {
        const auto size = getCompactControlSize(*this) / 2;
        containersButton->setBounds(0, juce::jmax(0, canvasHeight - size), size, size);
        containersButton->toFront(false);
    }
    content->setCanvasSize(juce::jmax(canvasWidth, contentWidth), juce::jmax(canvasHeight, contentHeight),
                           canvasHeight, graphCenterY);
    if (scrollReflowGeneration != content->getReflowGeneration())
    {
        scrollReflowGeneration = content->getReflowGeneration();
        scrollReflowStart = viewport.getViewPosition();
        scrollReflowActive = true;
    }
    if (scrollReflowActive)
    {
        const auto targetX = juce::jlimit(0, juce::jmax(0, content->getTargetPreferredWidth() - canvasWidth), scrollReflowStart.x);
        const auto targetY = juce::jlimit(0, juce::jmax(0, content->getTargetPreferredHeight() - canvasHeight), scrollReflowStart.y);
        const auto progress = content->getReflowProgress();
        viewport.setViewPosition(juce::roundToInt(scrollReflowStart.x + (targetX - scrollReflowStart.x) * progress),
                                 juce::roundToInt(scrollReflowStart.y + (targetY - scrollReflowStart.y) * progress));
        if (! content->isReflowRunning()) scrollReflowActive = false;
    }
    if (restoreScrollPending && viewport.getWidth() > 0 && viewport.getHeight() > 0)
    {
        viewport.setViewPosition(static_cast<int>(parameters.state.getProperty(shell_editor_ui::key("scroll.routing.x"), 0)),
                                 static_cast<int>(parameters.state.getProperty(shell_editor_ui::key("scroll.routing.y"), 0)));
        restoreScrollPending = false;
    }
    syncScrollbars();
    repaint();
}

void RoutingPanel::timerCallback()
{
    const auto visible = bool(parameters.state.getProperty(shell_editor_ui::key("routing.containers"), true));
    content->setContainersVisible(visible);
    containersButton->setSelectionHighlighted(visible);
    const auto previousHeight = content->getPreferredHeight();
    const auto previousWidth = content->getPreferredWidth();
    content->refresh();

    if (content->getPreferredHeight() != previousHeight
        || content->getPreferredWidth() != previousWidth)
    {
        resized();
    }
}
