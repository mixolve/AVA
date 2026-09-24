#include "Controls.h"
#include "ParameterControlSupport.h"

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
        dragHoldEligible = false;
        confirmationFlashActive = false;
        setViewportIgnoreDragFlag(false);

        if ((longPressArmed || dragHoldArmed) && getButtonText() != longPressOriginalText)
            setButtonText(longPressOriginalText);

        longPressArmed = false;
        dragHoldArmed = false;
    }

    repaint();
}

void BoxTextButton::mouseDown(const juce::MouseEvent& event)
{
    if (! isEnabled())
        return;

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

    if (clearsParameterFocusOnMouseDown)
        shell_parameter_focus::clearFocus(*this);

    if (event.mods.isPopupMenu() || ! event.mods.isLeftButtonDown())
        return;

    pointerDown = true;
    dragActive = false;
    pressCanceled = false;
    pressHighlight = true;
    dragHoldEligible = moveOnNextDrag && onDragDrop != nullptr;
    moveOnNextDrag = false;
    longPressEligible = ! dragHoldEligible && (getActionPromptCount() > 0 || longPressAction != nullptr);
    longPressArmed = false;
    dragHoldArmed = false;
    setViewportIgnoreDragFlag(false);
    longPressOriginalText = dragHoldEligible && moveArmedOriginalText.isNotEmpty()
        ? moveArmedOriginalText
        : getButtonText();
    moveArmedOriginalText.clear();
    if (dragHoldEligible && getButtonText() != longPressOriginalText)
        setButtonText(longPressOriginalText);
    if (dragHoldEligible)
    {
        dragHoldArmed = true;
        setViewportIgnoreDragFlag(true);
    }
    else if (longPressEligible)
        startTimer(longPressDelayMs);
    else
        stopTimer();
    repaint();
}

void BoxTextButton::mouseDoubleClick(const juce::MouseEvent& event)
{
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

    if (! pointerDown || longPressArmed)
        return;

    if (dragHoldArmed)
    {
        if (! dragActive && event.getDistanceFromDragStart() >= 4)
            dragActive = true;

        if (dragActive && onDragMove != nullptr)
            onDragMove(event.getScreenPosition().toInt());

        const auto shouldHighlight = ! dragActive && contains(event.getPosition());

        if (pressHighlight != shouldHighlight)
        {
            pressHighlight = shouldHighlight;
            repaint();
        }

        return;
    }

    if (cancelClickOnLeave && ! dragHoldArmed && ! pressCanceled && ! contains(event.getPosition()))
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

        if (action == nullptr && longPressHostAction != nullptr)
        {
            if (selectedIndex == resolvedIndex)
                action = longPressHostAction;
            ++resolvedIndex;
        }

        const auto hasMoveAction = onMoveArmed != nullptr || onDragDrop != nullptr;
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
        if (longPressTrailingAction != nullptr)
            ++resolvedIndex;

        if (action == nullptr
            && ! selectMove
            && longPressAdditionalPromptAction != nullptr
            && selectedIndex == resolvedIndex)
        {
            action = longPressAdditionalPromptAction;
        }

        dismissActionPrompt();

        if (selectMove)
        {
            if (onMoveArmed != nullptr)
                onMoveArmed();
            else
            {
                moveOnNextDrag = true;
                moveArmedOriginalText = getButtonText();
                setButtonText("MOVE?");
                flashConfirmationOutline();
            }
        }
        else if (action != nullptr)
        {
            flashConfirmationOutline();
            action();
        }

        return;
    }

    stopTimer();

    const auto wasLongPressArmed = longPressArmed;
    const auto wasDragHoldArmed = dragHoldArmed;
    const auto wasDragActive = dragActive;
    const auto wasPressCanceled = pressCanceled;
    pointerDown = false;
    dragActive = false;
    pressHighlight = false;
    longPressEligible = false;
    longPressArmed = false;
    dragHoldEligible = false;
    dragHoldArmed = false;
    pressCanceled = false;
    setViewportIgnoreDragFlag(false);
    repaint();

    if (wasLongPressArmed)
    {
        if (getButtonText() != longPressOriginalText)
            setButtonText(longPressOriginalText);

        repaint();

        if (! wasPressCanceled && contains(event.getPosition()) && longPressAction != nullptr)
        {
            flashConfirmationOutline();
            longPressAction();
        }

        return;
    }

    if (wasDragHoldArmed)
    {
        if (getButtonText() != longPressOriginalText)
            setButtonText(longPressOriginalText);

        repaint();

        if (! wasPressCanceled && wasDragActive && onDragDrop != nullptr)
            onDragDrop(event.getScreenPosition().toInt());

        if (onDragEnd != nullptr)
            onDragEnd();

        return;
    }

    if (wasPressCanceled)
        return;

    if (contains(event.getPosition()))
    {
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

    if (cancelClickOnLeave && ! dragHoldArmed)
    {
        pressCanceled = true;
        longPressEligible = false;
        if (! longPressArmed)
            setViewportIgnoreDragFlag(false);
        stopTimer();
    }

    pressHighlight = false;
    if (! longPressArmed && ! dragHoldArmed)
    {
        longPressEligible = false;
        setViewportIgnoreDragFlag(false);
        stopTimer();
    }
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

void BoxTextButton::mouseEnter(const juce::MouseEvent&)
{
    if (isEnabled())
        repaint();
}

void BoxTextButton::timerCallback()
{
    stopTimer();

    if (! isEnabled())
        return;

    if (! pointerDown || ! pressHighlight || dragActive || (! longPressEligible && ! dragHoldEligible))
        return;

    longPressEligible = false;
    longPressArmed = true;

    if (getActionPromptCount() > 0)
        showActionPrompt();
    else
    {
        setViewportIgnoreDragFlag(true);
        longPressOriginalText = getButtonText();
        setButtonText(longPressPromptText);
        repaint();
    }
}
