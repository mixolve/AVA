#pragma once
#include <JuceHeader.h>
#include "WindowGeometry.h"
namespace window_geometry
{
class WindowDragger
{
public:
    void begin(const juce::MouseEvent& event, juce::Component& owner)
    {
        window = owner.getTopLevelComponent();
        bounds = getBounds(*window);
        start = event.getScreenPosition();
        targets = getSnapTargets(*window);
    }
    void drag(const juce::MouseEvent& event)
    {
        if (window == nullptr || !event.mouseWasDraggedSinceMouseDown()) return;
        const auto position = bounds.getPosition() + event.getScreenPosition() - start;
        const auto snapped = snapPosition({position.x, position.y, bounds.getWidth(), bounds.getHeight()}, targets);
        setPosition(*window, {snapped.x, snapped.y});
    }
    void end() { window = nullptr; targets.clear(); }
private:
    juce::Component::SafePointer<juce::Component> window;
    juce::Rectangle<int> bounds;
    juce::Point<int> start;
    std::vector<SnapTarget> targets;
};
}
namespace ana::ui { using WindowDragger = window_geometry::WindowDragger; }
