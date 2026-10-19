#include "Panel.h"
#include "State.h"
#include "../shell/Controls.h"
#include "../shell/Editor.h"
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
class RoutingPetal final : public juce::Button
{
public:
    enum class Side { top, right, bottom, left };
    RoutingPetal() : juce::Button({}) { setWantsKeyboardFocus(false); setMouseClickGrabsKeyboardFocus(false); }
    void setSide(Side value) { side = value; repaint(); }
    void setMoveTarget(bool value) { moveTarget = value; repaint(); }
    bool hitTest(int x, int y) override { return outline().contains(static_cast<float>(x), static_cast<float>(y)); }
    void paintButton(juce::Graphics& g, bool hover, bool down) override
    {
        auto path = outline();
        const auto amount=hoverAnimation.amount(isEnabled() && (hover || down));
        ControlAppearance style(moveTarget,amount);
        style.paintBackground(g,path);
        const auto w = getWidth();
        const auto h = getHeight();
        if (side == Side::top || side == Side::bottom)
            g.fillRect(0, side == Side::top ? h - 1 : 0, w, 1);
        else
            g.fillRect(side == Side::left ? w - 1 : 0, 0, 1, h);
        style.normalBorder=uiGreyLight.withMultipliedAlpha(isEnabled()?1.0f:0.4f);
        style.paintFrame(g,outline(false));
    }
private:
    juce::Path outline(const bool closeShape = true) const
    {
        const auto w = static_cast<float>(getWidth());
        const auto h = static_cast<float>(getHeight());
        constexpr float bevel = 4.0f;
        juce::Path path;
        if (side == Side::top || side == Side::bottom)
        {
            path.startNewSubPath(0.5f, h - 0.5f);
            path.lineTo(bevel, 0.5f); path.lineTo(w - bevel, 0.5f);
            path.lineTo(w - 0.5f, h - 0.5f);
            if (closeShape) path.closeSubPath();
            if (side == Side::bottom)
                path.applyTransform(juce::AffineTransform::scale(-1.0f, -1.0f).translated(w, h));
        }
        else
        {
            path.startNewSubPath(w - 0.5f, 0.5f);
            path.lineTo(0.5f, bevel); path.lineTo(0.5f, h - bevel);
            path.lineTo(w - 0.5f, h - 0.5f);
            if (closeShape) path.closeSubPath();
            if (side == Side::right)
                path.applyTransform(juce::AffineTransform::scale(-1.0f, -1.0f).translated(w, h));
        }
        return path;
    }
    HoverAnimation hoverAnimation { *this };
    Side side = Side::top;
    bool moveTarget = false;
};
}

class RoutingPanel::Content final : public juce::Component, private juce::Timer
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
    };

    struct InstanceControls
    {
        std::unique_ptr<juce::Component> revealClip, revealBody;
        std::unique_ptr<DisclosureAnimation> appearance;
        juce::Rectangle<int> fullBounds, startBounds;
        bool hasStartBounds = false;
        SlideDirection direction = SlideDirection::down;
        std::unique_ptr<BoxTextButton> name;
        std::unique_ptr<RoutingPetal> addSerial;
        std::unique_ptr<RoutingPetal> addParallel;
        std::unique_ptr<RoutingPetal> addSerialBefore;
        std::unique_ptr<RoutingPetal> addParallelBefore;
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
        setOpaque(true);
        setWantsKeyboardFocus(true);
        reflow.snap(true);
        reflow.onUpdate = [this] { resized(); if (layoutChanged != nullptr) layoutChanged(); };
        refresh();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(uiBackground);
        graphics.setColour(uiGreyLight);
        for (const auto& [id, frame] : frames)
        {
            juce::Graphics::ScopedSaveState saved(graphics);
            auto animatedFrame = frame;
            if (const auto group = groupControls.find(id); group != groupControls.end())
            {
                graphics.reduceClipRegion(group->second.revealClip->getBounds());
                graphics.setColour(uiGreyLight.withMultipliedAlpha(
                    static_cast<float>(group->second.appearance->value())));
            }
            graphics.drawRect(animatedFrame, 1);
        }

        for (const auto& [id, group] : retiringGroups)
        {
            juce::ignoreUnused(id);
            juce::Graphics::ScopedSaveState saved(graphics);
            graphics.reduceClipRegion(group.revealClip->getBounds());
            graphics.setColour(uiGreyLight.withMultipliedAlpha(static_cast<float>(group.appearance->value())));
            graphics.drawRect(group.fullBounds.reduced(petalThickness), 1);
        }

        graphics.setColour(uiGreyLight);
        for (const auto& node : displayedState.nodes)
        {
            const auto found = locations.find(node.id);
            if (found == locations.end())
                continue;
            const auto& bounds = found->second;
            if (node.serialNext != 0 && ! node.container)
            {
                const auto child = locations.find(node.serialNext);
                if (child != locations.end())
                {
                    const auto childFrame = frames.find(node.serialNext);
                    const auto target = childFrame == frames.end() ? child->second : childFrame->second;
                    if (horizontal)
                        graphics.drawLine(static_cast<float>(bounds.getRight()),
                                          static_cast<float>(bounds.getCentreY()),
                                          static_cast<float>(target.getX()),
                                          static_cast<float>(target.getCentreY()), 1.0f);
                    else
                        graphics.drawLine(static_cast<float>(bounds.getCentreX()),
                                          static_cast<float>(bounds.getBottom()),
                                          static_cast<float>(target.getCentreX()),
                                          static_cast<float>(target.getY()), 1.0f);
                }
            }
            if (node.parallelNext != 0)
            {
                const auto sibling = locations.find(node.parallelNext);
                if (sibling != locations.end())
                {
                    if (horizontal)
                        graphics.drawLine(static_cast<float>(bounds.getCentreX()),
                                          static_cast<float>(bounds.getBottom()),
                                          static_cast<float>(sibling->second.getCentreX()),
                                          static_cast<float>(sibling->second.getY()), 1.0f);
                    else
                        graphics.drawLine(static_cast<float>(bounds.getRight()),
                                          static_cast<float>(bounds.getCentreY()),
                                          static_cast<float>(sibling->second.getX()),
                                          static_cast<float>(sibling->second.getCentreY()), 1.0f);
                }
            }
        }

        for (const auto& node : displayedState.nodes)
        {
            if (node.groupNext == 0)
                continue;
            const auto body = groupBodies.find(node.id);
            const auto next = locations.find(node.groupNext);
            if (body == groupBodies.end() || next == locations.end())
                continue;
            const auto nextFrame = frames.find(node.groupNext);
            const auto target = nextFrame == frames.end() ? next->second : nextFrame->second;
            if (horizontal)
                graphics.drawLine(static_cast<float>(body->second.getRight()),
                                  static_cast<float>(body->second.getCentreY()),
                                  static_cast<float>(target.getX()),
                                  static_cast<float>(target.getCentreY()), 1.0f);
            else
                graphics.drawLine(static_cast<float>(body->second.getCentreX()),
                                  static_cast<float>(body->second.getBottom()),
                                  static_cast<float>(target.getCentreX()),
                                  static_cast<float>(target.getY()), 1.0f);
        }
    }

    void resized() override
    {
        locations.clear();
        frames.clear();
        groupBodies.clear();
        const auto width = laneWidth();
        const auto group = measureGroup(displayedState.entryInstanceId, width);
        const auto canvasWidth = availableWidth > 0 ? juce::jmax(availableWidth, group.width + 2 * uiGap) : getWidth();
        const auto canvasHeight = availableHeight > 0 ? juce::jmax(availableHeight, group.height + 2 * uiGap) : getHeight();
        if (horizontal)
            layoutGroup(displayedState.entryInstanceId,
                        uiGap,
                        juce::jmax(uiGap, (canvasHeight - group.height) / 2),
                        width);
        else
            layoutGroup(displayedState.entryInstanceId,
                        juce::jmax(uiGap, (canvasWidth - group.width) / 2),
                        uiGap,
                        width);
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
        if (moveSourceId != 0 && (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey))
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
        if (! event.mods.isPopupMenu() || event.mouseWasDraggedSinceMouseDown())
            return;

        auto* editor = findParentComponentOfClass<AvaAudioProcessorEditor>();
        if (editor == nullptr)
            return;

        auto anchorBounds = juce::Rectangle<int>(120, rowHeight);
        anchorBounds.setCentre(event.getPosition());
        editor->showChoicePrompt(editor->getLocalArea(this, anchorBounds),
                                 { "VERTICAL", "HORIZONTAL" },
                                 horizontal ? 1 : 0,
                                 { true, true },
                                 juce::Justification::centred,
                                 [safeThis = juce::Component::SafePointer<Content>(this)] (const int choice)
                                 {
                                     if (safeThis != nullptr && (choice == 0 || choice == 1))
                                         safeThis->setHorizontalOrientation(choice == 1);
                                 });
    }

    void setHorizontalOrientation(const bool shouldBeHorizontal)
    {
        if (horizontal == shouldBeHorizontal)
            return;

        horizontal = shouldBeHorizontal;
        resized();
        if (layoutChanged != nullptr)
            layoutChanged();
    }

    int getPreferredWidth() const
    {
        auto width = measureGroup(displayedState.entryInstanceId, laneWidth()).width + 2 * uiGap;
        for (const auto& [id, item] : retiringControls) { juce::ignoreUnused(id); width = juce::jmax(width, item.fullBounds.getRight() + uiGap); }
        for (const auto& [id, item] : retiringGroups) { juce::ignoreUnused(id); width = juce::jmax(width, item.fullBounds.getRight() + uiGap); }
        return width;
    }

    int getPreferredHeight() const
    {
        auto height = measureGroup(displayedState.entryInstanceId, laneWidth()).height + 2 * uiGap;
        for (const auto& [id, item] : retiringControls) { juce::ignoreUnused(id); height = juce::jmax(height, item.fullBounds.getBottom() + uiGap); }
        for (const auto& [id, item] : retiringGroups) { juce::ignoreUnused(id); height = juce::jmax(height, item.fullBounds.getBottom() + uiGap); }
        return height;
    }

    int getTargetPreferredWidth() const { return measureGroup(displayedState.entryInstanceId, laneWidth()).width + 2 * uiGap; }
    int getTargetPreferredHeight() const { return measureGroup(displayedState.entryInstanceId, laneWidth()).height + 2 * uiGap; }
    int getReflowGeneration() const noexcept { return reflowGeneration; }
    double getReflowProgress() const noexcept { return reflow.value(); }
    bool isReflowRunning() const noexcept { return reflow.isRunning(); }

    void setAvailableSize(const int width, const int height)
    {
        availableWidth = width;
        availableHeight = height;
    }

    juce::String getDisplayNameFor(const int instanceId) const
    {
        return ava::routing::getDisplayName(displayedState, instanceId);
    }

    void refresh()
    {
        const auto newState = ava::routing::readState(state.state);
        if (newState == displayedState && ! controls.empty())
        {
            updateBypassPrompts();
            return;
        }

        const auto previousState = displayedState;
        const auto topologyWasChanged = newState.nodes != displayedState.nodes
            || newState.entryInstanceId != displayedState.entryInstanceId;
        const auto transitionStart = juce::Time::getMillisecondCounterHiRes();
        const auto hadControls = ! controls.empty();
        displayedState = newState;
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
            }
            instance.appearance->onUpdate = [this, id] { updateInstanceAppearance(id); scheduleRetiredCleanup(); };
            instance.name = makeButton(label);
            instance.name->onDoubleClick = [this, id, label, button = instance.name.get()]
            {
                if (renameAction != nullptr)
                    renameAction(id, label, *button);
            };
            const auto removeAction = [this, id]
            {
                if (ava::routing::removeInstance(state.state, id))
                {
                    if (moveSourceId == id)
                        clearMove();
                    notifyTopologyChanged();
                }
            };
            instance.name->setLongPressPromptActions(removeAction, [this, id] { toggleBypass(id); });
            instance.name->onModifiedClick = [this, id, removeAction] (const juce::ModifierKeys modifiers)
            {
                if (modifiers.isAltDown())
                    removeAction();
                else if (modifiers.isCtrlDown())
                    toggleBypass(id);
            };
            instance.name->setLongPressPrimaryPromptIcon("trash");
            instance.name->setLongPressPrimaryPromptEnabled(ava::routing::getInstanceCount(displayedState) > 1);
            instance.name->setLongPressSecondaryPromptIcon("plug-off");
            instance.name->setLongPressMovePromptEnabled(ava::routing::getInstanceCount(displayedState) > 1);
            instance.name->onMoveArmed = [this, id]
            {
                if (ava::routing::getInstanceCount(displayedState) <= 1) return;
                moveSourceId = id;
                grabKeyboardFocus();
                updateMoveTargets();
            };
            instance.name->onControlDragStarted = [this, id]
            {
                if (ava::routing::getInstanceCount(displayedState) <= 1) return;
                controlDragCancelled = false;
                moveSourceId = id;
                grabKeyboardFocus();
                updateMoveTargets();
                startTimerHz(60);
            };
            instance.name->onControlDrag = [this] (const juce::MouseEvent& event, const bool released)
            {
                finishControlDrag(event, released);
            };
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
                        && ava::routing::moveInstanceSerialAfter(state.state, moveSourceId, id))
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
            group.revealBody = std::make_unique<juce::Component>();
            group.revealClip->setInterceptsMouseClicks(false, true);
            group.revealBody->setInterceptsMouseClicks(false, true);
            group.revealClip->addAndMakeVisible(*group.revealBody);
            addAndMakeVisible(*group.revealClip);
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
                group.petals[static_cast<size_t>(index)]->onClick = [this, id = node.id, index]
                {
                    auto changed = false;
                    if (moveSourceId != 0)
                    {
                        switch (index)
                        {
                            case 0: changed = ava::routing::moveInstanceBeforeGroup(state.state, moveSourceId, id); break;
                            case 1: changed = ava::routing::moveInstanceAfterGroup(state.state, moveSourceId, id); break;
                            case 2: changed = ava::routing::moveParallelOutsideGroup(state.state, moveSourceId, id, true); break;
                            case 3: changed = ava::routing::moveParallelOutsideGroup(state.state, moveSourceId, id, false); break;
                        }
                        if (changed) clearMove();
                    }
                    else
                    {
                        pendingDirection = directionFor(index < 2, index == 0 || index == 2);
                        switch (index)
                        {
                            case 0: changed = ava::routing::insertInstanceBeforeGroup(state.state, id); break;
                            case 1: changed = ava::routing::insertInstanceAfterGroup(state.state, id); break;
                            case 2: changed = ava::routing::insertParallelOutsideGroup(state.state, id, true); break;
                            case 3: changed = ava::routing::insertParallelOutsideGroup(state.state, id, false); break;
                        }
                    }
                    if (changed) notifyTopologyChanged();
                    else pendingDirection.reset();
                };
            }
            for (auto& petal : group.petals) group.revealBody->addAndMakeVisible(*petal);
            groupControls.emplace(node.id, std::move(group));
        }
        for (auto& [id, item] : previousControls)
            if (item.appearance != nullptr)
            {
                item.revealClip->setInterceptsMouseClicks(false, false);
                item.direction = removalDirection(previousState, id, item.fullBounds);
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
        updateMoveTargets();
        updateBypassPrompts();
        resized();
        for (auto& [id, item] : retiringControls) { juce::ignoreUnused(item); updateInstanceAppearance(id); }
        for (auto& [id, item] : retiringGroups) { juce::ignoreUnused(item); updateGroupAppearance(id); }
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
        const auto alongX = horizontal ? serial : ! serial;
        return alongX ? (before ? SlideDirection::left : SlideDirection::right)
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
        if (id == displayedState.rootInstanceId && ava::routing::getInstanceCount(displayedState) > 1)
            return static_cast<bool>(state.state.getProperty(ava::routing::rootBypassStateKey, false)) ? 1.0f : 0.0f;
        const auto instance = id == displayedState.rootInstanceId
            ? std::shared_ptr<AvaAudioProcessor> {}
            : processor.getRoutingInstanceHandle(id);
        auto* target = id == displayedState.rootInstanceId ? &processor : instance.get();
        if (target == nullptr)
            return -1.0f;
        if (auto* parameter = target->getValueTreeState().getParameter(AvaAudioProcessor::paramGlobalBypassId))
            return parameter->getValue();
        return -1.0f;
    }

    void toggleBypass(const int id)
    {
        if (isSoloBypassed(id)) return;
        if (id == displayedState.rootInstanceId && ava::routing::getInstanceCount(displayedState) > 1)
        {
            state.state.setProperty(ava::routing::rootBypassStateKey, getBypassValue(id) < 0.5f, nullptr);
            notifyTopologyChanged();
            updateBypassPrompts();
            return;
        }
        const auto instance = id == displayedState.rootInstanceId
            ? std::shared_ptr<AvaAudioProcessor> {}
            : processor.getRoutingInstanceHandle(id);
        auto* target = id == displayedState.rootInstanceId ? &processor : instance.get();
        if (target == nullptr)
            return;
        if (auto* parameter = target->getValueTreeState().getParameter(AvaAudioProcessor::paramGlobalBypassId))
            parameter->setValueNotifyingHost(parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
        updateBypassPrompts();
    }

    void updateBypassPrompts()
    {
        for (auto& [id, instance] : controls)
        {
            const auto value = getBypassValue(id);
            instance.name->setLongPressSecondaryPromptActive(value >= 0.5f);
            instance.name->setFillVisible(true);
            instance.name->setFillColour(juce::Colour(value >= 0.5f ? 0xff222222 : 0xff444444));
        }
    }

    void configureAddActions(InstanceControls& instance, const int id)
    {
        const auto action = [this, id](const bool serial, const bool before)
        {
            auto changed = false;
            if (moveSourceId != 0)
            {
                if (moveSourceId != id)
                    changed = serial
                        ? (before ? ava::routing::moveInstanceSerialBefore(state.state, moveSourceId, id)
                                  : ava::routing::moveInstanceSerialAfter(state.state, moveSourceId, id))
                        : (before ? ava::routing::moveInstanceParallelBefore(state.state, moveSourceId, id)
                                  : ava::routing::moveInstanceParallelTo(state.state, moveSourceId, id));
                if (changed) clearMove();
            }
            else
            {
                pendingDirection = directionFor(serial, before);
                changed = serial
                    ? (before ? ava::routing::insertSerialInstanceBefore(state.state, id)
                              : ava::routing::insertSerialInstance(state.state, id))
                    : (before ? ava::routing::insertParallelInstanceBefore(state.state, id)
                              : ava::routing::insertParallelInstance(state.state, id));
            }
            if (changed) notifyTopologyChanged();
            else pendingDirection.reset();
        };
        instance.addSerial->onClick = [action] { action(true, false); };
        instance.addSerialBefore->onClick = [action] { action(true, true); };
        instance.addParallel->onClick = [action] { action(false, false); };
        instance.addParallelBefore->onClick = [action] { action(false, true); };
    }

    const ava::routing::State::Node* findNode(const int id) const
    {
        return ava::routing::findStateNode(displayedState.nodes, id);
    }

    struct Size
    {
        int width = 0;
        int height = 0;
    };

    int horizontalGroupGap(const int firstId) const
    {
        const auto* first = findNode(firstId);
        if (first == nullptr)
            return 0;

        // A parallel group's frame supplies one of the two gaps before its first button.
        return first->parallelNext != 0 ? uiGap : uiGapDouble;
    }

    Size measureBranch(const int id, const int width) const
    {
        const auto* node = findNode(id);
        if (node == nullptr)
            return {};

        const auto child = measureGroup(node->serialNext, width);
        if (node->container) return child;
        if (horizontal)
            return { width + 2 * petalThickness + (child.width == 0 ? 0 : horizontalGroupGap(node->serialNext) + child.width),
                     juce::jmax(rowHeight + 2 * petalThickness, child.height) };

        return { juce::jmax(width + 2 * petalThickness, child.width),
                 rowHeight + 2 * petalThickness + (child.height == 0 ? 0 : uiGap + child.height) };
    }

    Size measureGroupBody(const int firstId, const int width) const
    {
        const auto* first = findNode(firstId);
        if (first == nullptr)
            return {};

        const auto framed = first->parallelNext != 0;
        Size result;
        auto count = 0;
        for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
        {
            const auto branch = measureBranch(node->id, width);
            if (horizontal)
            {
                result.width = juce::jmax(result.width, branch.width);
                result.height += branch.height;
            }
            else
            {
                result.width += branch.width;
                result.height = juce::jmax(result.height, branch.height);
            }
            ++count;
        }
        if (horizontal)
            result.height += uiGap * (count - 1);
        else
            result.width += uiGap * (count - 1);
        if (framed)
        {
            result.width += 2 * uiGap;
            result.height += 2 * uiGap;
        }
        return result;
    }

    Size measureGroup(const int firstId, const int width) const
    {
        const auto* first = findNode(firstId);
        if (first == nullptr)
            return {};

        const auto body = measureGroupBody(firstId, width);
        const auto inset = first->parallelNext != 0 ? petalThickness : 0;
        const Size outer { body.width + 2 * inset, body.height + 2 * inset };
        if (first->groupNext != 0)
        {
            const auto child = measureGroup(first->groupNext, width);
            if (horizontal)
                return { outer.width + uiGapDouble + child.width, juce::jmax(outer.height, child.height) };
            return { juce::jmax(outer.width, child.width), outer.height + uiGapDouble + child.height };
        }
        return outer;
    }

    int laneWidth() const
    {
        return 90;
    }

    void layoutBranch(const int id, const int x, const int y, const int width)
    {
        const auto* node = findNode(id);
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
        const auto nameX = petalThickness + (horizontal ? x : x + (branch.width - width - 2 * petalThickness) / 2);
        const auto nameY = petalThickness + (horizontal ? y + (branch.height - rowHeight - 2 * petalThickness) / 2 : y);
        const juce::Rectangle<int> target { nameX - petalThickness, nameY - petalThickness,
                                            width + 2 * petalThickness, rowHeight + 2 * petalThickness };
        found->second.fullBounds = interpolateBounds(found->second.startBounds, target, found->second.hasStartBounds);
        found->second.name->setBounds(petalThickness, petalThickness, width, rowHeight);
        locations[id] = found->second.fullBounds.reduced(petalThickness);
        auto placePetal = [&target](RoutingPetal& petal, const juce::Rectangle<int> bounds, RoutingPetal::Side side)
        {
            petal.setSide(side);
            petal.setBounds(bounds.translated(-target.getX(), -target.getY()));
        };
        const juce::Rectangle<int> rightAdd(nameX + width, nameY, petalThickness, rowHeight);
        const juce::Rectangle<int> bottomAdd(nameX, nameY + rowHeight, width, petalThickness);
        const juce::Rectangle<int> leftAdd(nameX - petalThickness, nameY, petalThickness, rowHeight);
        const juce::Rectangle<int> topAdd(nameX, nameY - petalThickness, width, petalThickness);
        placePetal(*found->second.addSerial, horizontal ? rightAdd : bottomAdd,
                   horizontal ? RoutingPetal::Side::right : RoutingPetal::Side::bottom);
        placePetal(*found->second.addSerialBefore, horizontal ? leftAdd : topAdd,
                   horizontal ? RoutingPetal::Side::left : RoutingPetal::Side::top);
        placePetal(*found->second.addParallel, horizontal ? bottomAdd : rightAdd,
                   horizontal ? RoutingPetal::Side::bottom : RoutingPetal::Side::right);
        placePetal(*found->second.addParallelBefore, horizontal ? topAdd : leftAdd,
                   horizontal ? RoutingPetal::Side::top : RoutingPetal::Side::left);

        updateInstanceAppearance(id);

        if (node->serialNext != 0)
        {
            const auto child = measureGroup(node->serialNext, width);
            if (horizontal)
                layoutGroup(node->serialNext, x + width + 2 * petalThickness + horizontalGroupGap(node->serialNext),
                            y + (branch.height - child.height) / 2, width);
            else
                layoutGroup(node->serialNext, x + (branch.width - child.width) / 2,
                            y + rowHeight + 2 * petalThickness + uiGap, width);
        }
    }

    void layoutGroup(const int firstId, const int x, const int y, const int width)
    {
        const auto* first = findNode(firstId);
        if (first == nullptr)
            return;

        const auto framed = first->parallelNext != 0;
        const auto group = measureGroup(firstId, width);
        const auto body = measureGroupBody(firstId, width);
        const auto inset = framed ? petalThickness : 0;
        const auto bodyX = inset + (horizontal ? x : x + (group.width - body.width - 2 * inset) / 2);
        const auto bodyY = inset + (horizontal ? y + (group.height - body.height - 2 * inset) / 2 : y);
        groupBodies[firstId] = { bodyX, bodyY, body.width, body.height };
        if (framed)
        {
            frames[firstId] = groupBodies[firstId];
            if (const auto buttons = groupControls.find(firstId); buttons != groupControls.end())
            {
                buttons->second.fullBounds = interpolateBounds(buttons->second.startBounds,
                    groupBodies[firstId].expanded(petalThickness), buttons->second.hasStartBounds);
                const auto currentBody = buttons->second.fullBounds.reduced(petalThickness);
                frames[firstId] = groupBodies[firstId] = currentBody;
                const auto currentX = currentBody.getX(), currentY = currentBody.getY();
                const auto currentWidth = currentBody.getWidth(), currentHeight = currentBody.getHeight();
                const std::array<juce::Rectangle<int>, 4> bounds {
                    juce::Rectangle<int>(currentX, currentY - petalThickness, currentWidth, petalThickness),
                    juce::Rectangle<int>(currentX, currentY + currentHeight, currentWidth, petalThickness),
                    juce::Rectangle<int>(currentX - petalThickness, currentY, petalThickness, currentHeight),
                    juce::Rectangle<int>(currentX + currentWidth, currentY, petalThickness, currentHeight) };
                const std::array<RoutingPetal::Side, 4> sides { RoutingPetal::Side::top,
                    RoutingPetal::Side::bottom, RoutingPetal::Side::left, RoutingPetal::Side::right };
                for (int index = 0; index < 4; ++index)
                {
                    const auto physicalSide = horizontal ? (index < 2 ? index + 2 : index - 2) : index;
                    auto& petal = buttons->second.petals[static_cast<size_t>(index)];
                    petal->setSide(sides[static_cast<size_t>(physicalSide)]);
                    petal->setBounds(bounds[static_cast<size_t>(physicalSide)].translated(
                        -buttons->second.fullBounds.getX(), -buttons->second.fullBounds.getY()));
                }
                updateGroupAppearance(firstId);
            }
        }

        if (horizontal)
        {
            auto branchY = bodyY + (framed ? uiGap : 0);
            for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
            {
                const auto branch = measureBranch(node->id, width);
                layoutBranch(node->id, bodyX + (framed ? uiGap : 0), branchY, width);
                branchY += branch.height + uiGap;
            }
        }
        else
        {
            auto branchX = bodyX + (framed ? uiGap : 0);
            const auto branchY = bodyY + (framed ? uiGap : 0);
            for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
            {
                layoutBranch(node->id, branchX, branchY, width);
                branchX += measureBranch(node->id, width).width + uiGap;
            }
        }

        if (first->groupNext != 0)
        {
            const auto child = measureGroup(first->groupNext, width);
            if (horizontal)
                layoutGroup(first->groupNext, bodyX + body.width + inset + uiGapDouble,
                            y + (group.height - child.height) / 2, width);
            else
                layoutGroup(first->groupNext, x + (group.width - child.width) / 2,
                            bodyY + body.height + inset + uiGapDouble, width);
            return;
        }
    }

    void updateGroupAppearance(const int id)
    {
        auto* item = findVisual(groupControls, retiringGroups, id);
        if (item == nullptr) return;
        auto& group = *item;
        const auto progress = group.appearance->value();
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
        const auto offset = slideOffset(instance.direction, instance.fullBounds, progress);
        instance.revealClip->setBounds(instance.fullBounds);
        instance.revealBody->setBounds(instance.fullBounds.withPosition(offset));
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
        auto button = std::make_unique<BoxTextButton>(uiGreyLight);
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
            instance.name->setDragTargetHighlighted(isTarget);
            instance.name->setMoveTargetAvailable(isTarget);
            const auto canInsert = moveSourceId == 0
                ? ava::routing::getInstanceCount(displayedState) < ava::routing::maximumInstanceCount : true;
            for (auto* petal : { instance.addSerial.get(), instance.addParallel.get(),
                                 instance.addSerialBefore.get(), instance.addParallelBefore.get() })
            {
                petal->setEnabled(canInsert);
                petal->setMoveTarget(moveSourceId != 0);
            }
        }
        for (auto& [id, group] : groupControls)
        {
            juce::ignoreUnused(id);
            for (auto& petal : group.petals)
            {
                petal->setEnabled(moveSourceId != 0 || ava::routing::getInstanceCount(displayedState) < ava::routing::maximumInstanceCount);
                petal->setMoveTarget(moveSourceId != 0);
            }
        }
    }

    void timerCallback() override
    {
        if (! juce::ModifierKeys::getCurrentModifiersRealtime().isCtrlDown())
        {
            controlDragCancelled = true;
            clearMove();
        }
    }

    void finishControlDrag(const juce::MouseEvent& event, const bool released)
    {
        if (controlDragCancelled || moveSourceId == 0)
            return;
        if (! event.mods.isCtrlDown())
        {
            controlDragCancelled = true;
            clearMove();
            return;
        }
        const auto position = getLocalPoint(event.eventComponent, event.position);
        std::function<void()> action;
        juce::Button* hoveredTarget = nullptr;
        const auto petalAction = [this, position] (RoutingPetal& petal) -> std::function<void()>
        {
            const auto local = petal.getLocalPoint(this, position).roundToInt();
            return petal.isEnabled() && petal.hitTest(local.x, local.y) ? petal.onClick : nullptr;
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
        stopTimer();
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
            update(*instance.name);
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
        stopTimer();
        updateControlDragHover(nullptr);
        moveSourceId = 0;
        updateMoveTargets();
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
    ava::routing::State displayedState;
    std::map<int, InstanceControls> controls;
    std::map<int, GroupControls> groupControls, retiringGroups;
    std::map<int, InstanceControls> retiringControls;
    DisclosureAnimation reflow;
    std::optional<SlideDirection> pendingDirection;
    int availableWidth = 0, availableHeight = 0;
    int reflowGeneration = 0;
    bool retiredCleanupPending = false;
    std::map<int, juce::Rectangle<int>> locations;
    std::map<int, juce::Rectangle<int>> frames;
    std::map<int, juce::Rectangle<int>> groupBodies;
    bool controlDragCancelled = false;
    int moveSourceId = 0;
    bool horizontal = false;
};

RoutingPanel::RoutingPanel(AvaAudioProcessor& processor)
    : parameters(processor.getValueTreeState()),
      content(std::make_unique<Content>(processor,
          [this] (const int id, const juce::String& label)
          {
              openInstance(id, label);
          },
          [this] (const int id, const juce::String& label, juce::Component& anchor)
          {
              if (onRenameRequest != nullptr)
                  onRenameRequest(id, label, anchor);
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
    setOpaque(true);
    viewport.setViewedComponent(content.get(), false);
    scrollBarLookAndFeel = std::make_unique<FlatScrollBarLookAndFeel>();
    viewport.getHorizontalScrollBar().setLookAndFeel(scrollBarLookAndFeel.get());
    viewport.getVerticalScrollBar().setLookAndFeel(scrollBarLookAndFeel.get());
    viewport.setScrollBarsShown(true, true);
    viewport.setScrollBarThickness(uiGap);
    viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::never);
    viewport.setWantsKeyboardFocus(false);
    addAndMakeVisible(viewport);

    backButton = std::make_unique<BoxTextButton>(uiGreyLight);
    backButton->setButtonText({});
    backButton->setTablerIcon("arrow-back-up");
    backButton->onClick = [this]
    {
        openInstanceId = 0;
        resized();
    };
    addAndMakeVisible(*backButton);

    titleButton = std::make_unique<BoxTextButton>(uiGreyLight);
    titleButton->setTextJustification(juce::Justification::centred);
    titleButton->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*titleButton);

    emptyLabel = std::make_unique<BoxTextButton>(uiGreyLight);
    emptyLabel->setButtonText("EMPTY");
    emptyLabel->setTextJustification(juce::Justification::centred);
    emptyLabel->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*emptyLabel);

    resized();
    startTimerHz(10);
}

RoutingPanel::~RoutingPanel()
{
    stopTimer();
    viewport.setViewedComponent(nullptr, false);
    viewport.getHorizontalScrollBar().setLookAndFeel(nullptr);
    viewport.getVerticalScrollBar().setLookAndFeel(nullptr);
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

void RoutingPanel::setOnRenameRequest(
    std::function<void(int, const juce::String&, juce::Component&)> action)
{
    onRenameRequest = std::move(action);
}

void RoutingPanel::openInstance(const int instanceId, const juce::String& label)
{
    if (instanceId == ava::routing::readState(parameters.state).rootInstanceId)
    {
        if (onOpenRoot != nullptr)
            onOpenRoot();
        return;
    }

    if (onOpenInstance != nullptr)
    {
        onOpenInstance(instanceId);
        return;
    }

    openInstanceId = instanceId;
    titleButton->setButtonText(label);
    resized();
}

bool RoutingPanel::cancelMoveMode()
{
    return content->cancelMoveMode();
}

void RoutingPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(uiBackground);
}

void RoutingPanel::resized()
{
    const auto detailOpen = openInstanceId != 0;
    viewport.setVisible(! detailOpen);
    backButton->setVisible(detailOpen);
    titleButton->setVisible(detailOpen);
    emptyLabel->setVisible(detailOpen);

    if (detailOpen)
    {
        viewport.setBounds({});
        auto bounds = getLocalBounds();
        auto titleRow = bounds.removeFromTop(rowHeight);
        backButton->setBounds(titleRow.removeFromLeft(rowHeight));
        titleRow.removeFromLeft(parameterGap);
        titleButton->setBounds(titleRow);
        bounds.removeFromTop(uiGap);
        emptyLabel->setBounds(bounds.removeFromTop(rowHeight));
        return;
    }

    viewport.setBounds(getLocalBounds());
    const auto thickness = viewport.getScrollBarThickness();
    auto verticalNeeded = false;
    auto horizontalNeeded = false;
    for (auto attempt = 0; attempt < 3; ++attempt)
    {
        const auto availableWidth = juce::jmax(1, viewport.getWidth()
            - (verticalNeeded ? thickness : 0));
        const auto nextHorizontalNeeded = content->getPreferredWidth() > availableWidth;
        const auto availableHeight = juce::jmax(1, viewport.getHeight()
            - (nextHorizontalNeeded ? thickness : 0));
        const auto nextVerticalNeeded = content->getPreferredHeight() > availableHeight;
        if (nextHorizontalNeeded == horizontalNeeded && nextVerticalNeeded == verticalNeeded)
            break;
        horizontalNeeded = nextHorizontalNeeded;
        verticalNeeded = nextVerticalNeeded;
    }

    const auto visibleWidth = juce::jmax(1, viewport.getWidth()
        - (verticalNeeded ? thickness : 0));
    const auto visibleHeight = juce::jmax(1, viewport.getHeight()
        - (horizontalNeeded ? thickness : 0));
    content->setAvailableSize(viewport.getWidth(), viewport.getHeight());
    content->setSize(juce::jmax(visibleWidth, content->getPreferredWidth()),
                     juce::jmax(visibleHeight, content->getPreferredHeight()));
    if (scrollReflowGeneration != content->getReflowGeneration())
    {
        scrollReflowGeneration = content->getReflowGeneration();
        scrollReflowStart = viewport.getViewPosition();
        scrollReflowActive = true;
    }
    if (scrollReflowActive)
    {
        auto targetVertical = false, targetHorizontal = false;
        for (auto attempt = 0; attempt < 3; ++attempt)
        {
            targetHorizontal = content->getTargetPreferredWidth() > viewport.getWidth() - (targetVertical ? thickness : 0);
            targetVertical = content->getTargetPreferredHeight() > viewport.getHeight() - (targetHorizontal ? thickness : 0);
        }
        const auto finalWidth = viewport.getWidth() - (targetVertical ? thickness : 0);
        const auto finalHeight = viewport.getHeight() - (targetHorizontal ? thickness : 0);
        const auto targetX = juce::jlimit(0, juce::jmax(0, content->getTargetPreferredWidth() - finalWidth), scrollReflowStart.x);
        const auto targetY = juce::jlimit(0, juce::jmax(0, content->getTargetPreferredHeight() - finalHeight), scrollReflowStart.y);
        const auto progress = content->getReflowProgress();
        viewport.setViewPosition(juce::roundToInt(scrollReflowStart.x + (targetX - scrollReflowStart.x) * progress),
                                 juce::roundToInt(scrollReflowStart.y + (targetY - scrollReflowStart.y) * progress));
        if (! content->isReflowRunning()) scrollReflowActive = false;
    }
}

void RoutingPanel::timerCallback()
{
    const auto previousHeight = content->getPreferredHeight();
    const auto previousWidth = content->getPreferredWidth();
    content->refresh();

    if (openInstanceId != 0)
    {
        const auto label = content->getDisplayNameFor(openInstanceId);
        if (label.isEmpty())
        {
            openInstanceId = 0;
            resized();
        }
        else if (titleButton->getButtonText() != label)
        {
            titleButton->setButtonText(label);
        }
    }
    else if (content->getPreferredHeight() != previousHeight
             || content->getPreferredWidth() != previousWidth)
    {
        resized();
    }
}
