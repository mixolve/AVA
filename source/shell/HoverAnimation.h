#pragma once

#include "DisclosureAnimation.h"

class HoverAnimation final : private juce::MouseListener
{
public:
    explicit HoverAnimation(juce::Component& targetComponent)
        : component(targetComponent), lastPointerPosition(juce::Desktop::getMousePosition())
    {
        animation.onUpdate = [&targetComponent] { targetComponent.repaint(); };
        targetComponent.addMouseListener(this, true);
    }

    ~HoverAnimation() override { component.removeMouseListener(this); }

    void inherit(const bool highlighted)
    {
        lastPointerPosition = juce::Desktop::getMousePosition();
        inheritedPosition = lastPointerPosition;
        inheritedHover = highlighted;
        animation.snap(highlighted);
        component.repaint();
    }

    float amount(const bool active)
    {
        const auto pointerPosition = juce::Desktop::getMousePosition();
        if (inheritedHover)
        {
            if (pointerPosition == inheritedPosition && component.isEnabled()
                && component.getLocalBounds().contains(component.getLocalPoint(nullptr, pointerPosition)))
                return 1.0f;
            inheritedHover = false;
        }
        if (! active && animation.targetExpanded() && component.isEnabled()
            && pointerPosition == lastPointerPosition && coveredByChoicePrompt(pointerPosition))
        {
            animation.snap(true);
            return 1.0f;
        }
        if (active != animation.targetExpanded())
        {
            // Rebuilt controls and popup/layout changes under a stationary cursor
            // inherit their hover appearance without replaying a pointer transition.
            if (pointerPosition == lastPointerPosition) animation.snap(active);
            else animation.setExpanded(active);
        }
        lastPointerPosition = pointerPosition;
        return static_cast<float>(animation.value());
    }

private:
    bool coveredByChoicePrompt(const juce::Point<int> screenPosition) const
    {
        if (! component.getLocalBounds().contains(component.getLocalPoint(nullptr, screenPosition))) return false;
        auto* top = component.getTopLevelComponent();
        auto* target = top->getComponentAt(top->getLocalPoint(nullptr, screenPosition));
        for (; target != nullptr; target = target->getParentComponent())
            if (static_cast<bool>(target->getProperties().getWithDefault("preserveUnderlyingHover", false)))
                return true;
        return false;
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        if (animation.targetExpanded())
            lastPointerPosition = event.source.getScreenPosition().roundToInt();
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        lastPointerPosition = event.source.getScreenPosition().roundToInt();
    }

    juce::Component& component;
    DisclosureAnimation animation { 20.0 };
    juce::Point<int> lastPointerPosition, inheritedPosition;
    bool inheritedHover = false;
};
