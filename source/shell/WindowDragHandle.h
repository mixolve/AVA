#pragma once

#include "Controls.h"
#include "TablerIcons.h"
#include "WindowGeometry.h"

class WindowDragHandle final : public juce::Label
{
public:
    explicit WindowDragHandle(juce::Component& component)
        : owner(&component), icon(loadTablerIcon("arrows-left-right", iconGlyphSize)), hover(*this)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
    }

    ~WindowDragHandle() override { finishDrag(); }

    void setMoveOwner(juce::Component& component) { finishDrag(); owner = &component; }

    void paint(juce::Graphics& g) override
    {
        const auto amount = hover.amount(dragging || isMouseHovering(*this));
        ControlAppearance style(false,amount);
        style.paint(g,getLocalBounds());
        juce::DrawableImage drawable;
        drawable.setImage(icon);
        drawable.setOverlayColour(style.contentColour());
        drawable.drawWithin(g, getLocalBounds().toFloat().withSizeKeepingCentre(iconGlyphSize, iconGlyphSize),
            juce::RectanglePlacement::centred, 1.0f);
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }

    void mouseDown(const juce::MouseEvent& event) override
    {
        finishDrag();
        dragging = event.mods.isLeftButtonDown() && owner != nullptr;
        if (! dragging) return;
        movedDuringDrag = false;
        const auto bounds = window_geometry::getBounds(*owner);
        startWindowPosition = bounds.getPosition();
        draggedWindowSize = { bounds.getWidth(), bounds.getHeight() };
        snapTargets = window_geometry::getSnapTargets(*owner);
        startPointerPosition = juce::Desktop::getMousePosition();
        hover.inherit(true);
        setMouseCursor(juce::MouseCursor::NoCursor);
        cursorHidden = getPeer() != nullptr && event.source.isDragging();
        if (cursorHidden) window_geometry::setWindowDragActive(*owner, true);

    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (! dragging) return;
        if (owner == nullptr) { finishDrag(); return; }
        if (! event.mods.isLeftButtonDown()) { finishDrag(); return; }
        movedDuringDrag = movedDuringDrag || event.mouseWasDraggedSinceMouseDown();
        window_geometry::setPosition(*owner, dragPosition(juce::Desktop::getMousePosition()));
        if (cursorHidden) window_geometry::hideWindowDragCursor();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging && owner != nullptr)
            window_geometry::setPosition(*owner, dragPosition(juce::Desktop::getMousePosition()));
        finishDrag();
    }
    void mouseExit(const juce::MouseEvent&) override
    {
        if (dragging && cursorHidden) window_geometry::hideWindowDragCursor();
        repaint();
    }

    void visibilityChanged() override
    {
        if (!isShowing()) finishDrag();
    }

private:
    void finishDrag()
    {
        if (! dragging) return;
        dragging = false;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
        if (cursorHidden)
            window_geometry::setWindowDragActive(owner != nullptr ? *owner.getComponent()
                : static_cast<juce::Component&>(*this), false);
        cursorHidden = false;
    }

    juce::Point<int> dragPosition(const juce::Point<int> pointerPosition) const
    {
        const auto raw = startWindowPosition + pointerPosition - startPointerPosition;
        if (!movedDuringDrag) return raw;
        const auto snapped = window_geometry::snapPosition(
            { raw.x, raw.y, draggedWindowSize.x, draggedWindowSize.y }, snapTargets);
        return { snapped.x, snapped.y };
    }

    juce::Component::SafePointer<juce::Component> owner;
    juce::Point<int> startWindowPosition, startPointerPosition, draggedWindowSize;
    std::vector<window_geometry::SnapTarget> snapTargets;
    bool dragging = false, cursorHidden = false, movedDuringDrag = false;
    juce::Image icon;
    HoverAnimation hover;
};
