#include "Controls.h"
#include "ValueEditor.h"
#include "Prompt.h"
#include "Editor.h"

bool scrollViewportWithWheel(juce::Viewport& viewport,
                             const int contentHeight,
                             const juce::MouseWheelDetails& wheel,
                             const bool fineControl)
{
    // This helper scrolls vertical pages only. A horizontal trackpad gesture
    // must never be converted into a Y movement, even when the nested page
    // cannot scroll: consume it rather than forwarding it to a parent.
    if (std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) return true;

    if (const auto* editor = viewport.findParentComponentOfClass<AvaAudioProcessorEditor>();
        editor != nullptr && !editor->allowsViewportScroll(viewport)) return true;
    auto* content = viewport.getViewedComponent();
    if (content == nullptr) return false;
    const auto maxScrollY = juce::jmax(0, contentHeight - viewport.getHeight());
    if (maxScrollY <= 0)
    {
        if (auto* parent = viewport.findParentComponentOfClass<juce::Viewport>())
            return scrollViewportWithWheel(*parent, parent->getViewedComponent() != nullptr ? parent->getViewedComponent()->getHeight() : 0, wheel, fineControl);
        return false;
    }
    const auto delta = wheel.deltaY;
    if (std::abs(delta) < 1.0e-6f) return false;

    struct WheelPosition final : juce::DynamicObject
    {
        juce::Component::SafePointer<juce::Component> content;
        double y = 0.0;
        juce::Point<int> applied;
    };
    auto& properties = viewport.getProperties();
    auto* position = dynamic_cast<WheelPosition*>(properties["wheelPosition"].getDynamicObject());
    if (position == nullptr)
    {
        position = new WheelPosition();
        properties.set("wheelPosition", juce::var(position));
    }
    const auto current = viewport.getViewPosition();
    // Reconcile navigation, page replacement and external scroll restoration.
    // Fractional wheel movement survives only while we own the current position.
    if (position->content.getComponent() != content || position->applied != current || !wheel.isSmooth)
    {
        position->content = content;
        position->y = current.y;
    }
    const auto sensitivity = (wheel.isSmooth ? 140.0 : 48.0) * (fineControl ? 0.1 : 1.0);
    auto movement = -double(delta) * sensitivity;
    if (!wheel.isSmooth && juce::roundToInt(movement) == 0)
        movement = movement < 0.0 ? -48.0 : 48.0;
    position->y = juce::jlimit(0.0, double(maxScrollY), position->y + movement);
    const auto next = juce::Point<int>(0, juce::roundToInt(position->y));
    if (next != current)
    {
        viewport.setViewPosition(next);
        if (auto* editor = viewport.findParentComponentOfClass<AvaAudioProcessorEditor>())
            editor->repaint(editor->getLocalArea(&viewport, viewport.getLocalBounds()));
        else
            viewport.repaint();
    }
    position->applied = viewport.getViewPosition();
    return true;
}

