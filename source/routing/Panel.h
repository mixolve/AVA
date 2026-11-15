#pragma once
#include "../shell/Style.h"
#include "../shell/Controls.h"
#include <cmath>

#include <JuceHeader.h>
#include <memory>
#include <functional>

class BoxTextButton;
class AvaAudioProcessor;

class RoutingPanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit RoutingPanel(AvaAudioProcessor& processor);
    ~RoutingPanel() override;

    void setOnOpenRoot(std::function<void()> action);
    void setOnOpenInstance(std::function<void(int)> action);
    void setOnTopologyChanged(std::function<void()> action);
    void setOnRenameRequest(std::function<void(int, const juce::String&, juce::Component&)> action);

    bool cancelMoveMode();
    void restoreUiState();
    void scrollToOrigin();
    void resized() override;
    void visibilityChanged() override;
    void placeContainersButton(juce::Component& parent, juce::Rectangle<int> routingFrame);

private:
    class Content;

    void timerCallback() override;
    void openInstance(int instanceId);

    juce::AudioProcessorValueTreeState& parameters;
    class StateViewport final : public UiScrollViewport
    {
    public:
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            const auto* viewedContent = getViewedComponent();
            if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
            {
                const auto maxX = viewedContent != nullptr ? juce::jmax(0, viewedContent->getWidth() - getViewWidth()) : 0;
                const auto next = juce::jlimit(0, maxX, getViewPositionX()
                    + juce::roundToInt(-wheel.deltaX * (wheel.isSmooth ? 140.0f : 48.0f)));
                setViewPosition(next, getViewPositionY());
                return;
            }
            const auto maxY = viewedContent != nullptr ? juce::jmax(0, viewedContent->getHeight() - getViewHeight()) : 0;
            const auto y = getViewPositionY();
            const bool canScroll = wheel.deltaY < 0 ? y < maxY : wheel.deltaY > 0 && y > 0;
            if (canScroll)
            {
                // Smooth gestures may be smaller than one pixel. Consume and
                // accumulate them here instead of leaking them to the page.
                const auto delta = -double(wheel.deltaY) * (wheel.isSmooth ? 140.0 : 48.0);
                if (delta * verticalRemainder < 0) verticalRemainder = 0;
                verticalRemainder += delta;
                const auto move = static_cast<int>(verticalRemainder);
                const auto next = juce::jlimit(0, maxY, y + move);
                verticalRemainder -= next - y;
                if (next == 0 || next == maxY) verticalRemainder = 0;
                if (next != y) setViewPosition(getViewPositionX(), next);
                return;
            }
            verticalRemainder = 0;
            if (auto* outer = findParentComponentOfClass<juce::Viewport>())
                scrollViewportWithWheel(*outer,
                    outer->getViewedComponent() != nullptr ? outer->getViewedComponent()->getHeight() : 0,
                    wheel, false);
        }
    private:
        double verticalRemainder = 0;
    };
    StateViewport viewport;
    std::unique_ptr<BoxTextButton> containersButton;
    bool updatingViewport = true;
    bool restoreScrollPending = true;
    std::unique_ptr<Content> content;
    std::function<void()> onOpenRoot;
    std::function<void(int)> onOpenInstance;
    std::function<void()> onTopologyChanged;
    std::function<void(int, const juce::String&, juce::Component&)> onRenameRequest;
    int scrollReflowGeneration = 0;
    juce::Point<int> scrollReflowStart;
    bool scrollReflowActive = false;
};
