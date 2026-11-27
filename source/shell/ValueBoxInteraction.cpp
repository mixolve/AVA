#include "Controls.h"
#include "ValueEditor.h"
#include "Prompt.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"

void ValueBoxComponent::mouseDown(const juce::MouseEvent& event)
{
    const auto point = event.getEventRelativeTo(this).getPosition();
    if (hasTopInternalControls() && RangeIndicator::potentiometerBounds(*this, getValueBounds()).contains(point))
        return;

    if (selectionGesture.begin(*this, event))
    {
        pointerDown = false;
        setPressPreviewPosition(-1.0);
        return;
    }
    if (const auto* field = findParentComponentOfClass<ParameterField>(); field != nullptr)
    {
        if (field->getProperties()["dockInstanceNumber"].toString().isNotEmpty()
            && getBottomIndicatorBounds(RangeIndicator::boundsInside(*this, getValueBounds()), 0)
                   .contains(event.getPosition()))
            return;
    }

    const auto bottom = bottomIndicatorAt(event.getEventRelativeTo(this).getPosition());
    if (bottom == 0 && canUseBottomIndicator(0) && event.mods.isLeftButtonDown())
    {
        pressedBottomIndicator = bottom;
        repaintValueOverlay(*this);
        return;
    }
    if (const auto* field = findParentComponentOfClass<ParameterField>(); field != nullptr
        && bool(field->getProperties().getWithDefault("readOnlyControl", false))) return;
    if (bottom >= 0 && canUseBottomIndicator(bottom) && event.mods.isLeftButtonDown())
    {
        pressedBottomIndicator = bottom;
        repaintValueOverlay(*this);
        return;
    }
    auto* clickedComponent = event.originalComponent;
    const auto clickIsInsideThisValueBox = clickedComponent != nullptr
        && (clickedComponent == this || isParentOf(clickedComponent));

    if (clickIsInsideThisValueBox && event.mods.isLeftButtonDown() && event.mods.isCtrlDown()
        && event.mods.isShiftDown() && !event.mods.isAltDown() && onResetRequested != nullptr
        && !textInputActive)
    {
        onResetRequested();
        return;
    }

    if (! event.mods.isLeftButtonDown())
        return;

    if (clickedComponent != this) return;

    if (momentaryAction != nullptr || isBinaryToggle())
    {
        if (!canAdjustValue() || event.mods.isPopupMenu() || !getValueBounds().contains(point))
            return;
        pointerDown = true;
        momentaryPressed = momentaryAction != nullptr;
        const auto next = momentaryPressed || RangeIndicator::rangePosition(slider) < 0.5 ? 1 : 0;
        if (momentaryPressed ? momentaryPressFeedbackEnabled
            : isDiscreteChoiceEnabled == nullptr || isDiscreteChoiceEnabled(next))
            setPressPreviewPosition(double(next));
        return;
    }

    pointerDown = true;
    if (canAdjustValue() && getValueBounds().contains(point))
        directIndicator->beginFromValue(event.getEventRelativeTo(this),
            RangeIndicator::potentiometerBounds(*this, getValueBounds()),
            slider, choiceCount(), liveDiscreteDrag, hideCursorOnDrag);
}

void ValueBoxComponent::setPressPreviewPosition(double position)
{
    if (juce::exactlyEqual(pressPreviewPosition, position)) return;
    pressPreviewPosition = position;
    if (position >= 0.0)
        revealValue();
    else if (!pointerOnValue())
        valueTransitionAt = juce::Time::getMillisecondCounterHiRes() + valueTransitionDelayMs;
    repaintValueOverlay(*this);
}

void ValueBoxComponent::setMomentaryRestingState(bool active)
{
    momentaryRestingState = active;
    if (!momentaryPressed)
    {
        auto& source = slider;
        source.setValue(active ? source.getMaximum() : source.getMinimum(), juce::sendNotificationSync);
    }
    repaintValueOverlay(*this);
}

void ValueBoxComponent::cancelMomentaryPress()
{
    if (std::exchange(momentaryDragging, false)) {
        juce::Component::beginDragAutoRepeat(0);
        setMouseCursor(momentaryDragCursor);
        const auto cancelled = onMomentaryDragCancelled;
        if (cancelled) cancelled();
    }
    if (!momentaryPressed) return;
    momentaryPressed = pointerDown = false;
    slider.setValue(momentaryRestingState ? slider.getMaximum() : slider.getMinimum(), juce::sendNotificationSync);
    setPressPreviewPosition(-1.0);
}

void ValueBoxComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (selectionGesture.isActive()) return;
    if (onMomentaryDrag && momentaryPressed && (momentaryDragging || event.getDistanceFromDragStart() >= 4)) {
        if (!momentaryDragging) {
            momentaryDragging = true;
            momentaryDragCursor = getMouseCursor();
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            juce::Component::beginDragAutoRepeat(40);
        }
        const auto action = onMomentaryDrag;
        action(event, false);
        return;
    }
    if (pressPreviewPosition >= 0.0 && event.mouseWasDraggedSinceMouseDown())
        setPressPreviewPosition(-1.0);

    if (directIndicator->isActive())
    {
        directIndicator->drag(event);
        if (directIndicator->wasDragged()) revealValue();
        repaintValueOverlay(*this);
        return;
    }

}

void ValueBoxComponent::mouseUp(const juce::MouseEvent& event)
{
    if (selectionGesture.finish(event)) return;
    if (std::exchange(momentaryDragging, false)) {
        juce::Component::beginDragAutoRepeat(0);
        setMouseCursor(momentaryDragCursor);
        const auto action = onMomentaryDrag;
        cancelMomentaryPress();
        if (action) action(event, true);
        return;
    }
    if (momentaryPressed)
    {
        const auto clicked = getValueBounds().contains(event.getEventRelativeTo(this).getPosition())
            && !event.mouseWasDraggedSinceMouseDown() && !event.mods.isPopupMenu()
            && interactionEnabled && isEnabled();
        const auto action = momentaryAction;
        cancelMomentaryPress();
        if (clicked && action != nullptr) action();
        return;
    }
    if (pressedBottomIndicator >= 0)
    {
        const auto index = std::exchange(pressedBottomIndicator, -1);
        repaint();
        if (bottomIndicatorAt(event.getEventRelativeTo(this).getPosition()) == index
            && !event.mouseWasDraggedSinceMouseDown() && canUseBottomIndicator(index))
        {
            auto& action = bottomIndicators[static_cast<size_t>(index)];
            action.toggle();
            repaintValueOverlay(*this);
        }
        return;
    }

    if (directIndicator->isActive())
    {
        const auto dragged = directIndicator->wasDragged();
        finishIndicatorDrag();
        if (dragged || !pointerDown)
        {
            pointerDown = false;
            // In the fixed dock the rail covers much of the small tile.
            // A click without dragging must still open the native numeric
            // editor; dragging keeps the ordinary potentiometer behaviour.
            if (!dragged && !event.mouseWasDraggedSinceMouseDown()
                && !event.mods.isPopupMenu() && !event.mods.isCtrlDown()
                && !event.mods.isAltDown() && canAdjustValue()
                && choiceCount() == 0 && textInputEnabled)
                if (const auto* field = findParentComponentOfClass<ParameterField>();
                    field != nullptr && field->getProperties().contains("quickAccessSelectionKey"))
                    showEditor();
            return;
        }
    }
    if (! pointerDown)
        return;

    const auto clicked = getValueBounds().contains(event.getEventRelativeTo(this).getPosition())
        && ! event.mouseWasDraggedSinceMouseDown()
        && ! event.mods.isPopupMenu();

    pointerDown = false;
    setPressPreviewPosition(-1.0);

    if (clicked)
    {
        if (canAdjustValue() && choiceCount() > 1)
        {
            auto& source = slider;
            const auto count = choiceCount();
            const auto selected = juce::roundToInt(RangeIndicator::rangePosition(source) * (count - 1));
            const auto direction = event.mods.isShiftDown() && !event.mods.isCtrlDown()
                && !event.mods.isAltDown() ? -1 : 1;
            auto next = selected;
            for (int attempt = 0; attempt < count; ++attempt)
            {
                next = (next + direction + count) % count;
                if (isDiscreteChoiceEnabled == nullptr || isDiscreteChoiceEnabled(next))
                    break;
            }
            if (next != selected && (isDiscreteChoiceEnabled == nullptr || isDiscreteChoiceEnabled(next)))
            {
                juce::Slider::ScopedDragNotification gesture(source);
                source.setValue(source.proportionOfLengthToValue(double(next) / double(count - 1)), juce::sendNotificationSync);
            }
        }
        else if (interactionEnabled && choiceCount() == 0)
            showEditor();
    }
}

void ValueBoxComponent::updateDockInstanceHover(juce::Point<int> position)
{
    // The routing INSTANCE name replaces the badge and BP in place, but only
    // after 50 ms over the number. Moving away cancels the pending reveal.
    const auto* field = findParentComponentOfClass<ParameterField>();
    const bool isDockControl = field != nullptr
        && field->getProperties()["dockInstanceNumber"].toString().isNotEmpty();
    const auto badge = getBottomIndicatorBounds(
        RangeIndicator::boundsInside(*this, getValueBounds()), 0);
    const bool hovered = isDockControl && badge.contains(position);
    if (dockInstanceBadgeUnderPointer == hovered) return;
    dockInstanceBadgeUnderPointer = hovered;
    const auto generation = ++dockInstanceHoverGeneration;
    if (!hovered)
    {
        if (dockInstanceNameHovered)
        {
            dockInstanceNameHovered = false;
            repaintValueOverlay(*this);
        }
        return;
    }
    juce::Timer::callAfterDelay(50,
        [safe = juce::Component::SafePointer<ValueBoxComponent>(this), generation]
        {
            if (safe == nullptr || !safe->dockInstanceBadgeUnderPointer
                || safe->dockInstanceHoverGeneration != generation) return;
            safe->dockInstanceNameHovered = true;
            repaintValueOverlay(*safe);
        });
}

void ValueBoxComponent::mouseExit(const juce::MouseEvent&)
{
    updateDockInstanceHover({ -1, -1 });
    if (valueVisible && !directIndicator->isActive())
        valueTransitionAt = juce::Time::getMillisecondCounterHiRes() + valueTransitionDelayMs;
    else if (!valueVisible && !directIndicator->isActive() && !textInputActive)
    {
        valueTransitionAt = 0.0;
        stopTimer();
    }
    if (isManualControlSelected(*this)) repaintValueOverlay(*this);
    else repaint();

    if (auto* parent = getParentComponent()) parent->repaint();
}

void ValueBoxComponent::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    repaint();
    juce::Component::modifierKeysChanged(modifiers);
}

void ValueBoxComponent::mouseMove(const juce::MouseEvent& event)
{
    updateDockInstanceHover(event.getPosition());
    if (interactionEnabled && isEnabled() && !isTimerRunning()) startTimer(10);
    // The thumb is drawn in the editor's overlay, not in this component.
    // Refresh that overlay while hovering a manually marked control, even
    // when the cursor moves within the same ValueBox.
    if (isManualControlSelected(*this)) repaintValueOverlay(*this);
    else repaint();
}

void ValueBoxComponent::mouseEnter(const juce::MouseEvent& event)
{
    updateDockInstanceHover(event.getPosition());
    if (interactionEnabled && isEnabled()
        && (numericControlEnabled || choiceCount() > 0 || momentaryAction != nullptr || textInputEnabled))
    {
        valueTransitionAt = valueVisible ? 0.0 : juce::Time::getMillisecondCounterHiRes() + valueTransitionDelayMs;
        if (!isTimerRunning()) startTimer(10);
    }
    if (isManualControlSelected(*this)) repaintValueOverlay(*this);
    else repaint();
    if (auto* parent = getParentComponent()) parent->repaint();
}

void ValueBoxComponent::showEditor()
{
    if (!textInputEnabled || textInputActive || choiceCount() > 0) return;
    auto* owner = findParentComponentOfClass<AvaAudioProcessorEditor>();
    if (owner == nullptr) return;
    const auto editorText = editorTextProvider != nullptr ? editorTextProvider()
                                                          : slider.getTextFromValue(slider.getValue());
    auto safeThis = juce::Component::SafePointer<ValueBoxComponent>(this);
    const auto anchorBounds = owner->getLocalArea(this, getValueBounds());

    setTextInputActive(true);
    repaint();

    owner->showTextPrompt(editorText,
                          [safeThis] (const juce::String& enteredText)
                          {
                              if (safeThis == nullptr)
                                  return false;

                              return safeThis->applyEnteredText(enteredText);
                          },
                          anchorBounds,
                          [safeThis]
                          {
                              if (safeThis != nullptr)
                                  safeThis->setTextInputActive(false);
                          },
                          [safeThis]
                          {
                              if (safeThis != nullptr)
                                  safeThis->setTextInputActive(false);
                          }, true, numericControlEnabled ? &slider : nullptr, textToValueParser, editorTextProvider,
                          [safeThis]
                          {
                              if (safeThis != nullptr && safeThis->onResetRequested != nullptr) safeThis->onResetRequested();
                          }, this);
}

bool ValueBoxComponent::applyEnteredText(const juce::String& enteredText)
{
    if (textCommit != nullptr)
    {
        if (!textCommit(enteredText)) return false;
        revealValue();
        repaint();
        return true;
    }
    const auto enteredValue = textToValueParser != nullptr
        ? textToValueParser(enteredText)
        : slider.getValueFromText(enteredText);
    if (!std::isfinite(enteredValue)) return false;
    const auto clampedValue = juce::jlimit(static_cast<double>(slider.getMinimum()),
                                           static_cast<double>(slider.getMaximum()),
                                           enteredValue);

    applyValue(clampedValue);

    if (auto* parent = getParentComponent())
        parent->repaint();

    repaint();
    return true;
}

void ValueBoxComponent::applyValue(const double value)
{
    slider.setValue(value, juce::sendNotificationSync);
}

void ValueBoxComponent::sliderValueChanged(juce::Slider*)
{
    if (pointerOnValue() || directIndicator->isActive() || textInputActive) revealValue();
    repaintValueOverlay(*this);
}

