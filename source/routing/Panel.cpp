#include "Panel.h"
#include "State.h"
#include "../shell/Controls.h"
#include "../shell/Editor.h"
#include "../shell/Style.h"

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

class RoutingPanel::ScrollBarLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawScrollbar(juce::Graphics& graphics, juce::ScrollBar& scrollbar,
                       int x, int y, int width, int height, bool vertical,
                       int thumbStart, int thumbSize, bool hovered, bool pressed) override
    {
        juce::ignoreUnused(scrollbar, hovered, pressed);
        graphics.setColour(juce::Colours::white);
        if (vertical)
            graphics.fillRect(x, thumbStart, width, thumbSize);
        else
            graphics.fillRect(thumbStart, y, thumbSize, height);
    }
};

class RoutingPanel::Content final : public juce::Component
{
    struct InstanceControls
    {
        std::unique_ptr<BoxTextButton> name;
    };

public:
    Content(juce::AudioProcessorValueTreeState& stateIn,
            std::function<void(int, const juce::String&)> openActionIn,
            std::function<void(int, const juce::String&, juce::Component&)> renameActionIn,
            std::function<void()> topologyChangedIn,
            std::function<void()> layoutChangedIn)
        : state(stateIn), openAction(std::move(openActionIn)),
          renameAction(std::move(renameActionIn)),
          topologyChanged(std::move(topologyChangedIn)),
          layoutChanged(std::move(layoutChangedIn))
    {
        setOpaque(true);
        refresh();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(uiBlack);
        graphics.setColour(uiGreyLight);
        for (const auto& [id, frame] : frames)
            graphics.drawRect(frame, 1);

        for (const auto& node : displayedState.nodes)
        {
            const auto found = locations.find(node.id);
            if (found == locations.end())
                continue;
            const auto& bounds = found->second;
            if (node.serialNext != 0)
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

        for (const auto& [id, exit] : groupExits)
        {
            const auto body = groupBodies.find(id);
            if (body == groupBodies.end())
                continue;
            if (horizontal)
                graphics.drawLine(static_cast<float>(body->second.getRight()),
                                  static_cast<float>(exit.getCentreY()),
                                  static_cast<float>(exit.getX()),
                                  static_cast<float>(exit.getCentreY()), 1.0f);
            else
                graphics.drawLine(static_cast<float>(exit.getCentreX()),
                                  static_cast<float>(body->second.getBottom()),
                                  static_cast<float>(exit.getCentreX()),
                                  static_cast<float>(exit.getY()), 1.0f);
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
        groupExits.clear();
        const auto width = laneWidth();
        const auto group = measureGroup(displayedState.entryInstanceId, width);
        if (horizontal)
            layoutGroup(displayedState.entryInstanceId,
                        uiGap,
                        juce::jmax(uiGap, (getHeight() - group.height) / 2),
                        width);
        else
            layoutGroup(displayedState.entryInstanceId,
                        juce::jmax(uiGap, (getWidth() - group.width) / 2),
                        uiGap,
                        width);
        repaint();
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
        for (auto& [id, instance] : controls)
            configureAddActions(*instance.name, id);
        resized();
        if (layoutChanged != nullptr)
            layoutChanged();
    }

    int getPreferredWidth() const
    {
        return measureGroup(displayedState.entryInstanceId, laneWidth()).width + 2 * uiGap;
    }

    int getPreferredHeight() const
    {
        return measureGroup(displayedState.entryInstanceId, laneWidth()).height + 2 * uiGap;
    }

    juce::String getDisplayNameFor(const int instanceId) const
    {
        return ava::routing::getDisplayName(displayedState, instanceId);
    }

    void refresh()
    {
        const auto newState = ava::routing::readState(state.state);
        if (newState == displayedState && ! controls.empty())
            return;

        displayedState = newState;
        if (moveSourceId != 0 && findNode(moveSourceId) == nullptr)
            moveSourceId = 0;
        controls.clear();
        groupExitButtons.clear();

        for (const auto& node : displayedState.nodes)
        {
            const auto id = node.id;
            const auto label = getDisplayNameFor(id);
            InstanceControls instance;
            instance.name = makeButton(label);
            instance.name->onDoubleClick = [this, id, label, button = instance.name.get()]
            {
                if (renameAction != nullptr)
                    renameAction(id, label, *button);
            };
            instance.name->setLongPressPromptActions([this, id]
            {
                if (ava::routing::removeInstance(state.state, id))
                {
                    if (moveSourceId == id)
                        clearMove();
                    notifyTopologyChanged();
                }
            }, {}, "D?");
            instance.name->onMoveArmed = [this, id]
            {
                moveSourceId = id;
                updateMoveTargets();
            };
            configureAddActions(*instance.name, id);
            instance.name->onClick = [this, id, label]
            {
                if (moveSourceId != 0)
                {
                    if (moveSourceId != id)
                        if (ava::routing::moveInstanceSerialAfter(state.state, moveSourceId, id))
                            notifyTopologyChanged();
                    clearMove();
                }
                else if (openAction != nullptr)
                    openAction(id, label);
            };

            controls.emplace(id, std::move(instance));
        }
        for (const auto& node : displayedState.nodes)
        {
            if (node.parallelNext == 0 || node.groupNext != 0)
                continue;
            const auto isParallelChild = std::any_of(displayedState.nodes.begin(), displayedState.nodes.end(),
                                                     [id = node.id] (const auto& candidate)
                                                     {
                                                         return candidate.parallelNext == id;
                                                     });
            if (isParallelChild)
                continue;
            auto button = makeButton("+");
            button->setEnabled(displayedState.nodes.size() < ava::routing::maximumInstanceCount);
            button->onClick = [this, id = node.id]
            {
                if (moveSourceId != 0)
                {
                    clearMove();
                    return;
                }
                if (ava::routing::insertInstanceAfterGroup(state.state, id))
                    notifyTopologyChanged();
            };
            groupExitButtons.emplace(node.id, std::move(button));
        }
        updateMoveTargets();
        resized();
    }

private:
    void configureAddActions(BoxTextButton& button, const int id)
    {
        button.setLongPressTrailingPromptIconAction([this, id]
        {
            if (moveSourceId != 0)
            {
                if (moveSourceId != id)
                    if (ava::routing::moveInstanceSerialAfter(state.state, moveSourceId, id))
                        notifyTopologyChanged();
                clearMove();
            }
            else if (ava::routing::insertSerialInstance(state.state, id))
                notifyTopologyChanged();
        }, horizontal ? "arrows-right" : "arrows-down");
        button.setLongPressAdditionalPromptIconAction([this, id]
        {
            if (moveSourceId != 0)
            {
                if (moveSourceId != id)
                    if (ava::routing::moveInstanceParallelTo(state.state, moveSourceId, id))
                        notifyTopologyChanged();
                clearMove();
            }
            else if (ava::routing::insertParallelInstance(state.state, id))
                notifyTopologyChanged();
        }, horizontal ? "arrows-down" : "arrows-right");
    }

    const ava::routing::State::Node* findNode(const int id) const
    {
        const auto found = std::find_if(displayedState.nodes.begin(), displayedState.nodes.end(),
                                        [id] (const auto& node) { return node.id == id; });
        return found == displayedState.nodes.end() ? nullptr : &*found;
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
        if (horizontal)
            return { width + (child.width == 0 ? 0 : horizontalGroupGap(node->serialNext) + child.width),
                     juce::jmax(rowHeight, child.height) };

        return { juce::jmax(width, child.width),
                 rowHeight + (child.height == 0 ? 0 : uiGap + child.height) };
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
        if (first->groupNext != 0)
        {
            const auto child = measureGroup(first->groupNext, width);
            if (horizontal)
                return { body.width + uiGapDouble + child.width,
                         juce::jmax(body.height, child.height) };
            return { juce::jmax(body.width, child.width),
                     body.height + uiGapDouble + child.height };
        }
        if (first->parallelNext == 0)
            return body;

        if (horizontal)
            return { body.width + uiGap + rowHeight,
                     juce::jmax(body.height, rowHeight) };

        return { juce::jmax(body.width, rowHeight),
                 body.height + uiGap + rowHeight };
    }

    int laneWidth() const
    {
        return 120;
    }

    void layoutBranch(const int id, const int x, const int y, const int width)
    {
        const auto* node = findNode(id);
        const auto found = controls.find(id);
        if (node == nullptr || found == controls.end())
            return;

        const auto branch = measureBranch(id, width);
        const auto nameX = horizontal ? x : x + (branch.width - width) / 2;
        const auto nameY = horizontal ? y + (branch.height - rowHeight) / 2 : y;
        found->second.name->setBounds(nameX, nameY, width, rowHeight);
        locations[id] = { nameX, nameY, width, rowHeight };

        if (node->serialNext != 0)
        {
            const auto child = measureGroup(node->serialNext, width);
            if (horizontal)
                layoutGroup(node->serialNext, x + width + horizontalGroupGap(node->serialNext),
                            y + (branch.height - child.height) / 2, width);
            else
                layoutGroup(node->serialNext, x + (branch.width - child.width) / 2,
                            y + rowHeight + uiGap, width);
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
        const auto bodyX = horizontal ? x : x + (group.width - body.width) / 2;
        const auto bodyY = horizontal ? y + (group.height - body.height) / 2 : y;
        groupBodies[firstId] = { bodyX, bodyY, body.width, body.height };
        if (framed)
            frames[firstId] = groupBodies[firstId];

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
                layoutGroup(first->groupNext, bodyX + body.width + uiGapDouble,
                            y + (group.height - child.height) / 2, width);
            else
                layoutGroup(first->groupNext, x + (group.width - child.width) / 2,
                            bodyY + body.height + uiGapDouble, width);
            return;
        }
        if (! framed)
            return;

        const auto exitX = horizontal ? bodyX + body.width + uiGap
                                      : bodyX + (body.width - rowHeight) / 2;
        const auto exitY = horizontal ? bodyY + (body.height - rowHeight) / 2
                                      : bodyY + body.height + uiGap;
        const auto exit = juce::Rectangle<int>(exitX, exitY, rowHeight, rowHeight);
        groupExits[firstId] = exit;
        if (const auto button = groupExitButtons.find(firstId); button != groupExitButtons.end())
            button->second->setBounds(exit);
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
            instance.name->setDragTargetOutlineVisible(isSource || isTarget);
        }
    }

    void clearMove()
    {
        moveSourceId = 0;
        updateMoveTargets();
    }

    void notifyTopologyChanged()
    {
        if (topologyChanged != nullptr)
            topologyChanged();
    }

    juce::AudioProcessorValueTreeState& state;
    std::function<void(int, const juce::String&)> openAction;
    std::function<void(int, const juce::String&, juce::Component&)> renameAction;
    std::function<void()> topologyChanged;
    std::function<void()> layoutChanged;
    ava::routing::State displayedState;
    std::map<int, InstanceControls> controls;
    std::map<int, std::unique_ptr<BoxTextButton>> groupExitButtons;
    std::map<int, juce::Rectangle<int>> locations;
    std::map<int, juce::Rectangle<int>> frames;
    std::map<int, juce::Rectangle<int>> groupBodies;
    std::map<int, juce::Rectangle<int>> groupExits;
    int moveSourceId = 0;
    bool horizontal = false;
};

RoutingPanel::RoutingPanel(juce::AudioProcessorValueTreeState& state)
    : parameters(state),
      content(std::make_unique<Content>(state,
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
    scrollBarLookAndFeel = std::make_unique<ScrollBarLookAndFeel>();
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

void RoutingPanel::paint(juce::Graphics& graphics)
{
    graphics.fillAll(uiBlack);
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
    content->setSize(juce::jmax(visibleWidth, content->getPreferredWidth()),
                     juce::jmax(visibleHeight, content->getPreferredHeight()));
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
