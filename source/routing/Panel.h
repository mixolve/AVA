#pragma once

#include <JuceHeader.h>
#include <memory>
#include <functional>

class BoxTextButton;
class AvaAudioProcessor;
class FlatScrollBarLookAndFeel;

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
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    class Content;

    void timerCallback() override;
    void openInstance(int instanceId, const juce::String& label);

    juce::AudioProcessorValueTreeState& parameters;
    std::unique_ptr<FlatScrollBarLookAndFeel> scrollBarLookAndFeel;
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
    int scrollReflowGeneration = 0;
    juce::Point<int> scrollReflowStart;
    bool scrollReflowActive = false;
};
