#pragma once

#include "DragCursor.h"
#include "WindowGeometry.h"

class WindowMoveGesture final : public juce::MouseListener
{
public:
    WindowMoveGesture(juce::Component& handleIn, juce::Component& editorIn)
        : handle(&handleIn), editor(&editorIn)
    {
        for (auto* parent = editorIn.getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
            if (dynamic_cast<juce::AudioProcessorEditor*>(parent) != nullptr) editor = parent;
        handleIn.addMouseListener(this, true);
    }
    ~WindowMoveGesture() override
    {
        if (handle != nullptr)
        {
            handle->removeMouseListener(this);
        }
    }
    void mouseDown(const juce::MouseEvent& event) override
    {
        cursor.end();
        moving = false;
        if(event.originalComponent!=nullptr && bool(event.originalComponent->getProperties().getWithDefault("passiveIndicator",false))) {armed=false;return;}
        armed = editor != nullptr && event.mods.isLeftButtonDown()
            && !event.mods.isCtrlDown() && !event.mods.isShiftDown()
            && !event.mods.isAltDown() && !event.mods.isCommandDown();
        if (!armed || !window_geometry::canMoveNativeWindow(*editor))
        {
            armed = false;
            return;
        }
        start = event.source.getScreenPosition();
        windowStart = window_geometry::getBounds(*editor).getPosition();
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!armed || editor == nullptr || (!moving && event.getDistanceFromDragStart() < 4)) return;
        moving = true;
        cursor.hold(event);
        window_geometry::setPosition(*editor, windowStart + (event.source.getScreenPosition() - start).roundToInt());
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        armed = moving = false;
        cursor.end();
    }
private:
    juce::Component::SafePointer<juce::Component> handle, editor;
    juce::Point<float> start;
    juce::Point<int> windowStart;
    bool armed = false, moving = false;
    DragCursor cursor;
};
