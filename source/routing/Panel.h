#pragma once
#include "../shell/Style.h"
#include "../shell/Controls.h"

#include <JuceHeader.h>
#include <memory>
#include <functional>

class BoxTextButton;
class AvaAudioProcessor;

class RoutingPanel final : public juce::Component,
                           private juce::Timer,
                           private juce::ScrollBar::Listener
{
public:
    explicit RoutingPanel(AvaAudioProcessor& processor);
    ~RoutingPanel() override;

    void setOnBackgroundClick(std::function<void()> action);
    void setOnOpenRoot(std::function<void()> action);
    void setOnOpenInstance(std::function<void(int)> action);
    void setOnTopologyChanged(std::function<void()> action);
    void setOnRenameRequest(std::function<void(int, const juce::String&, juce::Component&)> action);

    bool cancelMoveMode();
    void restoreUiState();
    void scrollToOrigin();
    void setLayoutBounds(juce::Rectangle<int> bounds, int scrollbarLength, int centerY);
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    class Content;

    void timerCallback() override;
    void openInstance(int instanceId);
    void syncScrollbars();
    void scrollBarMoved(juce::ScrollBar* bar, double position) override;

    juce::AudioProcessorValueTreeState& parameters;
    UiScrollViewport viewport;
    CursorDragScrollBar horizontalScrollBar {false};
    CursorDragScrollBar verticalScrollBar {true};
    int scrollbarLength = 0;
    int graphCenterY = 0;
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
