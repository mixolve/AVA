#pragma once

#include <JuceHeader.h>
#include <memory>
#include <functional>

class BoxTextButton;

class RoutingPanel final : public juce::Component,
                           private juce::Timer
{
public:
    explicit RoutingPanel(juce::AudioProcessorValueTreeState& state);
    ~RoutingPanel() override;

    void setOnOpenRoot(std::function<void()> action);
    void setOnOpenInstance(std::function<void(int)> action);
    void setOnTopologyChanged(std::function<void()> action);
    void setOnRenameRequest(std::function<void(int, const juce::String&, juce::Component&)> action);

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    class Content;
    class ScrollBarLookAndFeel;

    void timerCallback() override;
    void openInstance(int instanceId, const juce::String& label);

    juce::AudioProcessorValueTreeState& parameters;
    std::unique_ptr<ScrollBarLookAndFeel> scrollBarLookAndFeel;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    std::unique_ptr<BoxTextButton> backButton;
    std::unique_ptr<BoxTextButton> titleButton;
    std::unique_ptr<BoxTextButton> emptyLabel;
    std::function<void()> onOpenRoot;
    std::function<void(int)> onOpenInstance;
    std::function<void()> onTopologyChanged;
    std::function<void(int, const juce::String&, juce::Component&)> onRenameRequest;
    int openInstanceId = 0;
};
