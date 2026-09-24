#pragma once

#include <JuceHeader.h>

class EdgeResizeHandle final : public juce::Component
{
public:
    enum class Axis
    {
        horizontal,
        vertical
    };

    EdgeResizeHandle(juce::Component& ownerIn, const Axis axisIn,
                     const int minimumSizeIn, const int maximumSizeIn,
                     const juce::Colour borderColourIn)
        : owner(&ownerIn),
          axis(axisIn), minimumSize(minimumSizeIn), maximumSize(maximumSizeIn),
          borderColour(borderColourIn)
    {
        setMouseCursor(axis == Axis::horizontal ? juce::MouseCursor::LeftRightResizeCursor
                                                : juce::MouseCursor::UpDownResizeCursor);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    void setResizeOwner(juce::Component& newOwner) noexcept { owner = &newOwner; }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds();
        graphics.setColour(borderColour);

        if (axis == Axis::horizontal)
            graphics.fillRect(bounds.removeFromRight(1));
        else
            graphics.fillRect(bounds.removeFromBottom(1));
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragStartSize = { owner->getWidth(), owner->getHeight() };
        dragStartScreenPosition = event.getScreenPosition();
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        auto width = dragStartSize.x;
        auto height = dragStartSize.y;
        const auto dragDistance = event.getScreenPosition() - dragStartScreenPosition;

        if (axis == Axis::horizontal)
            width = juce::jlimit(minimumSize,
                                 maximumSize,
                                 dragStartSize.x + dragDistance.x);
        else
            height = juce::jlimit(minimumSize,
                                  maximumSize,
                                  dragStartSize.y + dragDistance.y);

        owner->setSize(width, height);
    }

private:
    juce::Component* owner;
    Axis axis;
    const int minimumSize;
    const int maximumSize;
    const juce::Colour borderColour;
    juce::Point<int> dragStartSize;
    juce::Point<int> dragStartScreenPosition;
};
