#include "Panel.h"
#include "State.h"
#include "../shell/Controls.h"
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
            std::function<void()> topologyChangedIn)
        : state(stateIn), openAction(std::move(openActionIn)),
          renameAction(std::move(renameActionIn)),
          topologyChanged(std::move(topologyChangedIn))
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
            const auto centreX = static_cast<float>(bounds.getCentreX());
            if (node.serialNext != 0)
            {
                const auto child = locations.find(node.serialNext);
                if (child != locations.end())
                {
                    const auto childFrame = frames.find(node.serialNext);
                    const auto target = childFrame == frames.end() ? child->second : childFrame->second;
                    graphics.drawLine(centreX, static_cast<float>(bounds.getBottom()),
                                      static_cast<float>(target.getCentreX()),
                                      static_cast<float>(target.getY()), 1.0f);
                }
            }
            if (node.parallelNext != 0)
            {
                const auto sibling = locations.find(node.parallelNext);
                if (sibling != locations.end())
                    graphics.drawLine(static_cast<float>(bounds.getRight()),
                                      static_cast<float>(bounds.getCentreY()),
                                      static_cast<float>(sibling->second.getX()),
                                      static_cast<float>(sibling->second.getCentreY()), 1.0f);
            }
        }
    }

    void resized() override
    {
        locations.clear();
        frames.clear();
        const auto width = laneWidth();
        const auto group = measureGroup(displayedState.entryInstanceId, width);
        const auto startX = juce::jmax(uiGap, (getWidth() - group.width) / 2);
        layoutGroup(displayedState.entryInstanceId, startX, uiGap, width);
        repaint();
    }

    void setAvailableWidth(const int width)
    {
        availableWidth = juce::jmax(1, width);
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
            instance.name->setLongPressTrailingPromptIconAction([this, id]
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
            }, "arrows-down");
            instance.name->setLongPressAdditionalPromptIconAction([this, id]
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
            }, "arrows-right");
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
        updateMoveTargets();
        resized();
    }

private:
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

    Size measureBranch(const int id, const int width) const
    {
        const auto* node = findNode(id);
        if (node == nullptr)
            return {};

        const auto child = measureGroup(node->serialNext, width);
        return { juce::jmax(width, child.width),
                 rowHeight + (child.height == 0 ? 0 : uiGap + child.height) };
    }

    Size measureGroup(const int firstId, const int width) const
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
            result.width += branch.width;
            result.height = juce::jmax(result.height, branch.height);
            ++count;
        }
        result.width += uiGap * (count - 1);
        if (framed)
        {
            result.width += 2 * uiGap;
            result.height += 2 * uiGap;
        }
        return result;
    }

    int laneWidth() const
    {
        auto low = 120;
        auto high = 560;
        while (low < high)
        {
            const auto candidate = (low + high + 1) / 2;
            if (measureGroup(displayedState.entryInstanceId, candidate).width + 2 * uiGap <= availableWidth)
                low = candidate;
            else
                high = candidate - 1;
        }
        return low;
    }

    void layoutBranch(const int id, const int x, const int y, const int width)
    {
        const auto* node = findNode(id);
        const auto found = controls.find(id);
        if (node == nullptr || found == controls.end())
            return;

        const auto branch = measureBranch(id, width);
        const auto nameX = x + (branch.width - width) / 2;
        found->second.name->setBounds(nameX, y, width, rowHeight);
        locations[id] = { nameX, y, width, rowHeight };

        if (node->serialNext != 0)
        {
            const auto child = measureGroup(node->serialNext, width);
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
        if (framed)
            frames[firstId] = { x, y, group.width, group.height };

        auto branchX = x + (framed ? uiGap : 0);
        const auto branchY = y + (framed ? uiGap : 0);
        for (auto* node = first; node != nullptr; node = findNode(node->parallelNext))
        {
            layoutBranch(node->id, branchX, branchY, width);
            branchX += measureBranch(node->id, width).width + uiGap;
        }
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
    ava::routing::State displayedState;
    std::map<int, InstanceControls> controls;
    std::map<int, juce::Rectangle<int>> locations;
    std::map<int, juce::Rectangle<int>> frames;
    int availableWidth = 420;
    int moveSourceId = 0;
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
        content->setAvailableWidth(availableWidth);
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
    content->setAvailableWidth(visibleWidth);
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
