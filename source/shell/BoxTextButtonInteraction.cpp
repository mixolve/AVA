#include "Controls.h"

#include <utility>

void BoxTextButton::enablementChanged()
{
    if (! isEnabled())
    {
        pointerDown = false;
        dragActive = false;
        pressHighlight = false;
        pressCanceled = false;
        pressedBottomIndicator = -1;
    }

    repaint();
}

void BoxTextButton::mouseDown(const juce::MouseEvent& event)
{
    if(bool(getProperties().getWithDefault("passiveIndicator",false)))return;
    if (!bool(getProperties().getWithDefault("preserveCtrlClick", false)) && selectionGesture.begin(*this, event))
    {
        pointerDown = pressHighlight = false;
        return;
    }
    if (!isEnabled()) return;
    const auto bottom = bottomIndicatorAt(event.getPosition());
    if (bottom >= 0 && event.mods.isLeftButtonDown())
    {
        pressedBottomIndicator = bottom;
        repaint();
        return;
    }

    const auto modifiedClick = (onShiftClick != nullptr && event.mods.isShiftDown())
        || (onModifiedClick != nullptr
        && (event.mods.isAltDown() || event.mods.isCtrlDown()))
        || (onControlDrag != nullptr && event.mods.isCtrlDown() && ! event.mods.isAltDown());
    if (! event.mods.isLeftButtonDown() || (event.mods.isPopupMenu() && ! modifiedClick))
        return;

    clickModifiers = event.mods;
    controlDragActive = false;
    pointerDown = true;
    dragActive = false;
    pressCanceled = false;
    pressHighlight = true;
    repaint();
}

void BoxTextButton::mouseDrag(const juce::MouseEvent& event)
{
    if(bool(getProperties().getWithDefault("passiveIndicator",false)))return;
    if (selectionGesture.isActive()) return;
    if (! isEnabled())
        return;

    if (pressedBottomIndicator >= 0)
        return;

    if (! pointerDown)
        return;

    const auto dragGesture = dragRequiresControl
        ? clickModifiers.isCtrlDown() && !clickModifiers.isAltDown()
        : !clickModifiers.isAnyModifierKeyDown();
    if (onControlDrag != nullptr && dragGesture
        && (controlDragActive || event.getDistanceFromDragStart() >= 4))
    {
        const auto starting = ! controlDragActive;
        controlDragActive = true;
        dragActive = true;
        pressHighlight = false;
        repaint();
        if (starting && onControlDragStarted != nullptr)
            onControlDragStarted();
        if(starting){auto input=event.source;input.forceMouseCursorUpdate();}
        const auto action = onControlDrag;
        action(event, false);
        return;
    }

    if (cancelClickOnLeave && ! pressCanceled && ! contains(event.getPosition()))
    {
        pressCanceled = true;
        pressHighlight = false;
        repaint();
    }

    if (! dragActive && event.getDistanceFromDragStart() >= 4)
        dragActive = true;

    const auto shouldHighlight = ! dragActive && contains(event.getPosition());

    if (pressHighlight != shouldHighlight)
    {
        pressHighlight = shouldHighlight;
        repaint();
    }

}

void BoxTextButton::mouseUp(const juce::MouseEvent& event)
{
    if(bool(getProperties().getWithDefault("passiveIndicator",false)))return;
    if (selectionGesture.finish(event)) return;
    if (pressedBottomIndicator >= 0)
    {
        const auto index = std::exchange(pressedBottomIndicator, -1);
        repaint();
        if (isEnabled() && bottomIndicatorAt(event.getPosition()) == index
            && !event.mouseWasDraggedSinceMouseDown())
        {
            repaint();
            const auto indicator = bottomIndicators[static_cast<size_t>(index)];
            indicator.toggle();
        }
        return;
    }

    if (! isEnabled())
        return;

    if (controlDragActive)
    {
        controlDragActive = false;
        pointerDown = false;
        dragActive = false;
        pressHighlight = false;
        pressCanceled = false;
        repaint();
        const auto action = onControlDrag;
        if (action != nullptr)
            action(event, true);
        auto input=event.source;input.forceMouseCursorUpdate();
        return;
    }

    const auto shouldClick = pointerDown && ! pressCanceled && ! dragActive
        && ! event.mouseWasDraggedSinceMouseDown() && contains(event.getPosition());
    pointerDown = false;
    dragActive = false;
    pressHighlight = false;
    pressCanceled = false;
    repaint();

    if (! shouldClick)
        return;

    if (onShiftClick && clickModifiers.isShiftDown() && !clickModifiers.isCtrlDown() && !clickModifiers.isAltDown())
    {
        const auto action = onShiftClick;
        action();
        return;
    }

    if (onModifiedClick != nullptr && (clickModifiers.isAltDown() || clickModifiers.isCtrlDown()))
    {
        const auto action = onModifiedClick;
        action(clickModifiers);
        return;
    }

    triggerClick();
}

void BoxTextButton::mouseExit(const juce::MouseEvent&)
{
    if (! isEnabled())
        return;

    if (! pointerDown || ! pressHighlight)
    {
        repaint();
        return;
    }

    if (cancelClickOnLeave) pressCanceled = true;

    pressHighlight = false;
    repaint();
}

void BoxTextButton::mouseEnter(const juce::MouseEvent&)
{
    if (isEnabled()) repaint();
}
