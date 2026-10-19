#include "Controls.h"

#include <utility>

void BoxTextButton::enablementChanged()
{
    if (! isEnabled())
    {
        dismissActionPrompt();
        stopTimer();
        pointerDown = false;
        dragActive = false;
        pressHighlight = false;
        pressCanceled = false;
        longPressEligible = false;
    }

    repaint();
}

void BoxTextButton::mouseDown(const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;

    if(event.mods.isPopupMenu() && onPopupClick) { onPopupClick(); return; }

    if (actionPromptActive)
    {
        if (! event.mods.isLeftButtonDown() || ! contains(event.getPosition()))
        {
            dismissActionPrompt();
            return;
        }

        const auto actionIndex = getActionPromptHitIndex(event.getPosition());

        if (actionIndex < 0)
        {
            dismissActionPrompt();
            return;
        }

        actionPromptPressedIndex = actionIndex;
        actionPromptHoverIndex = actionIndex;
        repaint();
        return;
    }

    const auto modifiedClick = (onModifiedClick != nullptr
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
    longPressEligible = ! modifiedClick && getActionPromptCount() > 0;
    if (longPressEligible)
        startTimer(longPressDelayMs);
    else
        stopTimer();
    repaint();
}

void BoxTextButton::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (onModifiedClick != nullptr && (event.mods.isAltDown() || event.mods.isCtrlDown()))
        return;

    if (isEnabled() && ! actionPromptActive && onDoubleClick != nullptr
        && event.mods.isLeftButtonDown())
    {
        ++pendingClickGeneration;
        onDoubleClick();
        return;
    }

    juce::TextButton::mouseDoubleClick(event);
}

void BoxTextButton::mouseDrag(const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;

    if (actionPromptActive)
    {
        if (event.mouseWasDraggedSinceMouseDown())
        {
            actionPromptPressedIndex = -1;
            actionPromptHoverIndex = -1;
            repaint();
        }
        return;
    }

    if (! pointerDown)
        return;

    if (onControlDrag != nullptr && clickModifiers.isCtrlDown() && ! clickModifiers.isAltDown()
        && (controlDragActive || event.getDistanceFromDragStart() >= 4))
    {
        const auto starting = ! controlDragActive;
        controlDragActive = true;
        dragActive = true;
        pressHighlight = false;
        longPressEligible = false;
        stopTimer();
        repaint();
        if (starting && onControlDragStarted != nullptr)
            onControlDragStarted();
        const auto action = onControlDrag;
        action(event, false);
        return;
    }

    if (cancelClickOnLeave && ! pressCanceled && ! contains(event.getPosition()))
    {
        pressCanceled = true;
        pressHighlight = false;
        longPressEligible = false;
        stopTimer();
        repaint();
    }

    if (! dragActive && event.getDistanceFromDragStart() >= 4)
    {
        dragActive = true;
        longPressEligible = false;
        stopTimer();
    }

    const auto shouldHighlight = ! dragActive && contains(event.getPosition());

    if (pressHighlight != shouldHighlight)
    {
        pressHighlight = shouldHighlight;
        repaint();
    }

    if (! shouldHighlight)
    {
        longPressEligible = false;
        stopTimer();
    }
}

void BoxTextButton::mouseUp(const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;

    if (consumeNextMouseUp)
    {
        consumeNextMouseUp = false;
        return;
    }

    if (actionPromptActive)
    {
        const auto actionIndex = getActionPromptHitIndex(event.getPosition());
        const auto selectedIndex = actionPromptPressedIndex;
        actionPromptPressedIndex = -1;

        if (selectedIndex < 0 || selectedIndex != actionIndex)
        {
            dismissActionPrompt();
            return;
        }

        std::function<void()> action;
        auto resolvedIndex = 0;

        if (longPressResetAction != nullptr)
        {
            if (selectedIndex == resolvedIndex)
                action = longPressResetAction;
            ++resolvedIndex;
        }

        if (action == nullptr && longPressSecondaryAction != nullptr)
        {
            if (selectedIndex == resolvedIndex)
                action = longPressSecondaryAction;
            ++resolvedIndex;
        }

        const auto hasMoveAction = onMoveArmed != nullptr;
        const auto selectMove = action == nullptr
            && hasMoveAction
            && selectedIndex == resolvedIndex;

        if (hasMoveAction)
            ++resolvedIndex;

        if (action == nullptr
            && ! selectMove
            && longPressTrailingAction != nullptr
            && selectedIndex == resolvedIndex)
        {
            action = longPressTrailingAction;
        }

        dismissActionPrompt();

        if (selectMove)
        {
            onMoveArmed();
        }
        else if (action != nullptr)
        {
            action();
        }

        return;
    }

    stopTimer();

    if (controlDragActive)
    {
        controlDragActive = false;
        pointerDown = false;
        dragActive = false;
        pressHighlight = false;
        longPressEligible = false;
        pressCanceled = false;
        repaint();
        const auto action = onControlDrag;
        if (action != nullptr)
            action(event, true);
        return;
    }

    const auto shouldClick = pointerDown && ! pressCanceled && ! dragActive
        && ! event.mouseWasDraggedSinceMouseDown() && contains(event.getPosition());
    pointerDown = false;
    dragActive = false;
    pressHighlight = false;
    longPressEligible = false;
    pressCanceled = false;
    repaint();

    if (! shouldClick)
        return;

    if (onModifiedClick != nullptr && (clickModifiers.isAltDown() || clickModifiers.isCtrlDown()))
    {
        ++pendingClickGeneration;
        const auto action = onModifiedClick;
        action(clickModifiers);
        return;
    }

    if (onDoubleClick != nullptr && onClick != nullptr)
    {
        if (event.getNumberOfClicks() > 1)
            return;

        const auto generation = ++pendingClickGeneration;
        juce::Timer::callAfterDelay(juce::MouseEvent::getDoubleClickTimeout(),
                                   [safeThis = juce::Component::SafePointer<BoxTextButton>(this), generation]
        {
            if (safeThis != nullptr && safeThis->isEnabled()
                && safeThis->pendingClickGeneration == generation)
                safeThis->triggerClick();
        });
    }
    else
        triggerClick();
}

void BoxTextButton::mouseExit(const juce::MouseEvent&)
{
    if (! isEnabled())
        return;

    if (actionPromptActive)
    {
        if (actionPromptPressedIndex >= 0 || actionPromptHoverIndex >= 0)
        {
            actionPromptPressedIndex = -1;
            actionPromptHoverIndex = -1;
            repaint();
        }

        return;
    }

    if (! pointerDown || ! pressHighlight)
    {
        repaint();
        return;
    }

    if (cancelClickOnLeave)
    {
        pressCanceled = true;
        longPressEligible = false;
        stopTimer();
    }

    pressHighlight = false;
    longPressEligible = false;
    stopTimer();
    repaint();
}

void BoxTextButton::mouseMove(const juce::MouseEvent& event)
{
    if (! isEnabled() || ! actionPromptActive || actionPromptPressedIndex >= 0
        || ! event.source.isMouse() || event.source.isDragging())
        return;

    const auto hoverIndex = getActionPromptHitIndex(event.getPosition());

    if (actionPromptHoverIndex == hoverIndex)
        return;

    actionPromptHoverIndex = hoverIndex;
    repaint();
}

void BoxTextButton::mouseEnter(const juce::MouseEvent& event)
{
    if (actionPromptActive && actionPromptPressedIndex < 0)
        actionPromptHoverIndex = getActionPromptHitIndex(event.getPosition());
    if (isEnabled())
        repaint();
}

void BoxTextButton::timerCallback()
{
    stopTimer();

    if (! isEnabled())
        return;

    if (! pointerDown || ! pressHighlight || dragActive || !longPressEligible)
        return;

    longPressEligible = false;
    showActionPrompt();
}
