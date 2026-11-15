#include "Controls.h"
#include "ValueEditor.h"
#include "Prompt.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"

namespace
{
constexpr double valueTransitionDelayMs = 50.0;

// ParameterField owns the value box and its embedded bottom-row buttons.
// Each embedded button owns its hover; overlays are repainted on the editor.
}

void repaintValueOverlay(juce::Component& component)
{
    // The editor owns a buffered image. Repainting its outer zoom/host window
    // reuses that image, so invalidate the editor that paints the indicators.
    component.repaint();
    auto* field = component.findParentComponentOfClass<ParameterField>();
    // Manual Ctrl+selection is stored on ParameterField, but both its value
    // frame and its range marker live in ValueBoxComponent. Repaint both
    // even when the click was not on an embedded button.
    if (auto* owner = dynamic_cast<ParameterField*>(&component); owner != nullptr)
    {
        for (auto* child : owner->getChildren())
            if (auto* value = dynamic_cast<ValueBoxComponent*>(child))
                value->repaint();
    }
    else if (field != nullptr && bool(component.getProperties().getWithDefault("embeddedControlButton", false)))
    {
        for (auto* child : field->getChildren())
            if (auto* value = dynamic_cast<ValueBoxComponent*>(child))
                value->repaint();
    }
    if (auto* editor = component.findParentComponentOfClass<AvaAudioProcessorEditor>())
    {
        auto* repaintSource = field != nullptr && bool(component.getProperties().getWithDefault("embeddedControlButton", false))
            ? static_cast<juce::Component*>(field) : &component;
        editor->repaint(editor->getLocalArea(repaintSource, repaintSource->getLocalBounds().expanded(4)));
    }
}

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
        // Indicators are painted on the editor surface above scrolling children.
        if (auto* editor = viewport.findParentComponentOfClass<AvaAudioProcessorEditor>())
            editor->repaint(editor->getLocalArea(&viewport, viewport.getLocalBounds()));
        else
            viewport.repaint();
    }
    position->applied = viewport.getViewPosition();
    return true;
}

ValueBoxComponent::ValueBoxComponent(juce::Slider& sliderToControl)
    : directIndicator(std::make_unique<RangeIndicator>()), slider(sliderToControl)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::NormalCursor);
    slider.addListener(this);

}

ValueBoxComponent::~ValueBoxComponent()
{
    if (momentaryDragging) juce::Component::beginDragAutoRepeat(0);
    directIndicator->end();
    slider.removeListener(this);
}

bool ValueBoxComponent::isBottomIndicatorVisible(int index) const
{
    if (!juce::isPositiveAndBelow(index, 2)) return false;
    if (index == 0)
        if (const auto* field = findParentComponentOfClass<ParameterField>())
            if (field->getProperties()["dockInstanceNumber"].toString().isNotEmpty()) return true;
    // Visibility follows the actual bypass binding, independently of the
    // value representation (continuous, discrete, note or momentary).
    if (index == 1 && bottomIndicators[1].state() < 0) return false;
    const auto* field = findParentComponentOfClass<ParameterField>();
    return field == nullptr || field->isBottomIndicatorVisible(index);
}

bool ValueBoxComponent::hasTopInternalControls() const noexcept
{
    return areRangeIndicatorsVisible() && (numericControlEnabled || choiceCount() > 0);
}

bool ValueBoxComponent::hasBottomInternalControls() const noexcept
{
    return isBottomIndicatorVisible(0) || isBottomIndicatorVisible(1);
}

bool ValueBoxComponent::areRangeIndicatorsVisible() const
{
    const auto* field = findParentComponentOfClass<ParameterField>();
    return field == nullptr || field->areRangeIndicatorsVisible();
}

juce::Rectangle<int> ValueBoxComponent::getBottomIndicatorBounds(juce::Rectangle<int> bounds, int index) const
{
    const auto* field = findParentComponentOfClass<ParameterField>();
    if (field != nullptr) return field->getBottomSideZone(bounds.expanded(RangeIndicator::edgeInset), index);
    const auto size = RangeIndicator::bottomControlSize(*this, getValueBounds(),
        field != nullptr ? field->getBottomIndicatorSize() : RangeIndicator::compactMarkerSize);
    return RangeIndicator::bottomMarker(bounds, index, size);
}

void ValueBoxComponent::enablementChanged()
{
    if (!isEnabled())
    {
        directIndicator->end();
        pointerDown = pressHighlight = false;
        togglePreviewActive = false;
        cancelMomentaryPress();
        setTextInputActive(false);
        valueVisible = false;
        valueTransitionAt = 0.0;
        stopTimer();
    }
    repaint();
}

void ValueBoxComponent::setNumericControlEnabled(bool enabled)
{
    numericControlEnabled = enabled;
    if (!enabled) directIndicator->end();
    repaint();
}

juce::Rectangle<int> ValueBoxComponent::getValueBounds() const
{
    return getLocalBounds();
}

bool ValueBoxComponent::hitTest(int x, int y)
{
    return getLocalBounds().contains(x, y);
}

void ValueBoxComponent::setInteractionEnabled(const bool shouldEnable)
{
    if (interactionEnabled == shouldEnable)
        return;

    interactionEnabled = shouldEnable;
    if (!interactionEnabled)
    {
        cancelMomentaryPress();
        togglePreviewActive = false;
        directIndicator->end();
        valueVisible = false;
        valueTransitionAt = 0.0;
        stopTimer();
    }

    repaintValueOverlay(*this);
}

void ValueBoxComponent::setTextInputActive(const bool shouldBeActive)
{
    if (textInputActive == shouldBeActive)
        return;

    textInputActive = shouldBeActive;
    // Keep the active dock tile alive while its floating value editor owns
    // the attachment. A layout refresh may otherwise recreate it mid-edit.
    if (auto* field = findParentComponentOfClass<ParameterField>())
        if (field->getProperties().contains("quickAccessSelectionKey"))
            field->getProperties().set("dockValueEditing", shouldBeActive);

    if (! shouldBeActive)
    {
        pressHighlight = false;
        pointerDown = false;
        togglePreviewActive = false;
        valueVisible = interactionEnabled && isEnabled() && pointerOnValue();
        valueTransitionAt = 0.0;
        if (valueVisible) startTimer(10);
        else stopTimer();
    }

    repaintValueOverlay(*this);
}

void ValueBoxComponent::setDiscreteChoiceIndicator(int count, int selected)
{
    count = juce::jmax(0, count);
    if (discreteChoiceCount == count && discreteChoiceIndex == selected) return;
    if (discreteChoiceCount != count) directIndicator->end();
    discreteChoiceCount = count;
    discreteChoiceIndex = selected;
    repaintValueOverlay(*this);
}

void ValueBoxComponent::setIdleTextProvider(std::function<juce::String()> provider)
{
    idleTextProvider = std::move(provider);
    valueVisible = false;
    valueTransitionAt = 0.0;
    stopTimer();
    repaint();
}

double ValueBoxComponent::getDisplayedValue() const
{
    auto& source = slider;
    const auto position = directIndicator->visualPosition();
    if (position >= 0.0 && choiceCount() > 0)
        return RangeIndicator::discreteValueAt(source, position, choiceCount());
    return position >= 0.0 ? source.getNormalisableRange().snapToLegalValue(RangeIndicator::valueAt(source, position))
                           : source.getValue();
}

bool ValueBoxComponent::pointerOnValue() const
{
    const auto bounds = getValueBounds();
    const auto text = getControlTextBounds(*this, bounds);
    return isMouseHovering(*this, text);
}

void ValueBoxComponent::revealValue()
{
    if (!interactionEnabled || !isEnabled()) return;
    valueVisible = true;
    valueTransitionAt = 0.0;
    if (!isTimerRunning()) startTimer(10);
}

void ValueBoxComponent::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto showValue = interactionEnabled && isEnabled()
        && (pointerOnValue() || momentaryPressed || directIndicator->isActive() || textInputActive);
    if (showValue)
    {
        if (valueVisible) { valueTransitionAt = 0.0; return; }
        if (valueTransitionAt == 0.0) valueTransitionAt = now + valueTransitionDelayMs;
        if (now < valueTransitionAt) return;
        revealValue();
        repaint();
        return;
    }
    if (!valueVisible) { valueTransitionAt = 0.0; stopTimer(); return; }
    if (valueTransitionAt == 0.0) valueTransitionAt = now + valueTransitionDelayMs;
    if (now < valueTransitionAt) return;
    stopTimer();
    valueVisible = false;
    valueTransitionAt = 0.0;
    repaint();
}

bool ValueBoxComponent::canUseBottomIndicator(const int index) const
{
    if (!juce::isPositiveAndBelow(index, 2) || !isShowing() || !isEnabled()
        || !isBottomIndicatorVisible(index)) return false;
    if (index == 0)
        if (const auto* field = findParentComponentOfClass<ParameterField>(); field != nullptr
            && field->getProperties()["dockInstanceNumber"].toString().isNotEmpty()) return false;

    // Host-slot assignment (SL) is independent of parameter availability.
    // An inactive parameter must still be assignable to an automation slot.
    // BP and the main parameter retain their normal interaction policy.
    if (index != 0 && !interactionEnabled) return false;
    const auto& indicator = bottomIndicators[static_cast<size_t>(index)];
    return indicator.write != nullptr && indicator.state() >= 0;
}

bool ValueBoxComponent::canAdjustValue() const noexcept
{
    // Read-only metering controls (CLIP) can still receive a Ctrl+click
    // selection gesture, but are not writable from the viewport or dock.
    if (const auto* field = findParentComponentOfClass<ParameterField>();
        field != nullptr && bool(field->getProperties().getWithDefault("readOnlyControl", false)))
        return false;
    return interactionEnabled && isEnabled() && (numericControlEnabled || choiceCount() > 1 || momentaryAction != nullptr)
        && !textInputActive && isShowing();
}

void ValueBoxComponent::finishIndicatorDrag()
{
    directIndicator->end(true);
    if (valueVisible && !pointerOnValue()) valueTransitionAt = juce::Time::getMillisecondCounterHiRes() + valueTransitionDelayMs;
    repaintValueOverlay(*this);
}

void ValueBoxComponent::paint(juce::Graphics& g)
{
    ControlAppearance style;
    style.paintBackground(g,getValueBounds());

}

void ValueBoxComponent::paintValueFrame(juce::Graphics& g, juce::Rectangle<int> bounds, bool overlayOnly)
{
    ControlAppearance style;
    style.frameColour = isManualControlSelected(*this) ? uiWhite : uiGreyLight;
    style.frameVisible = !overlayOnly;
    style.paintFrame(g, bounds);
}

void ValueBoxComponent::paintOverChildren(juce::Graphics& g)
{
    paintValueFrame(g,getValueBounds());
    // Some text fields fit their content instead of scrolling it.
    const auto fittedText = bool(getProperties().getWithDefault("fittedText", false));
    const auto textBounds = getControlTextBounds(*this, getValueBounds());
    const auto base = getParentComponent() != nullptr
        ? getParentComponent()->getProperties().getWithDefault("textButtonBaseColour", juce::int64(0xffbbbbbb))
        : juce::var(juce::int64(0xffbbbbbb));
    const auto compactButton = getHeight() <= getCompactControlSize(*this) + 2
        && getWidth() <= getCompactControlSize(*this) + 2;
    const auto compactOn = compactButton && momentaryAction == nullptr
        && (selectionHighlighted || (choiceCount() == 2 && getDisplayedValue() >= 0.5));
    const auto arrangementDragging = getParentComponent() != nullptr
        && bool(getParentComponent()->getProperties().getWithDefault("arrangementDragging", false));
    paintControlTextButton(g, textBounds, interactionEnabled && isEnabled() && (valueVisible || dragTargetHighlighted), textInputActive || (interactionEnabled && isEnabled() && compactOn), arrangementDragging || (interactionEnabled && isEnabled() && (momentaryPressed || pressHighlight || directIndicator->hasInteractionHighlight())), interactionEnabled && isEnabled() ? juce::Colour(juce::uint32(juce::int64(base))) : uiIndicatorDisabled);
    if (textInputActive && choiceCount() == 0) return;
    const auto valueText = valueVisible || idleTextProvider == nullptr
        ? displayTextProvider ? displayTextProvider() : slider.getTextFromValue(slider.getValue()) : juce::String();
    const auto binaryStateText = (choiceCount() == 2 || momentaryAction != nullptr)
        && (valueText.equalsIgnoreCase("OFF") || valueText.equalsIgnoreCase("ON"));
    const auto showIdleContent = (!valueVisible || binaryStateText) && !textInputActive;
    const auto text = idleTextProvider != nullptr && showIdleContent ? idleTextProvider() : valueText;
    const bool compact = getHeight() <= getCompactControlSize(*this) + 2
        && getWidth() <= getCompactControlSize(*this) + 2;
    if (fittedText || compact)
    {
        // Use the existing glyph-fit renderer so normal HOST addresses don't
        // auto-scroll or get cut off by the standard narrow text window.
        drawCompactControlText(g, text, fittedText ? textBounds.reduced(getUiGap(*this), 0) : textBounds);
        return;
    }
    if (std::abs(getHeight() - getSquareControlSize(*this)) <= 1)
    {
        if (drawSquareControlText(g, text, textBounds))
            scheduleMarqueeRepaint(*this, marqueeRepaintPending);
        return;
    }
    const auto elapsed = juce::Time::getMillisecondCounterHiRes() * 0.001;
    bool scrolling = false;
    {
        const juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(getValueBounds());
        g.setFont(makeUiFont());
        scrolling = drawLoopingText(g, text, textBounds, makeUiFont(), juce::Justification::centred, elapsed);
    }
    if (scrolling) scheduleMarqueeRepaint(*this, marqueeRepaintPending);
}

int ValueBoxComponent::bottomIndicatorAt(juce::Point<int> point) const
{
    // A dock source number is a read-only badge, never a host-slot action.
    const auto* field = findParentComponentOfClass<ParameterField>();
    const auto numbered = field != nullptr
        && field->getProperties()["dockInstanceNumber"].toString().isNotEmpty();
    for (int index = 0; index < 2; ++index)
    {
        if (index == 0 && numbered) continue;
        if (isBottomIndicatorVisible(index) && bottomIndicators[static_cast<size_t>(index)].state() >= 0
            && getBottomIndicatorBounds(RangeIndicator::boundsInside(*this, getValueBounds()), index).contains(point)) return index;
    }
    return -1;
}

void ValueBoxComponent::mouseDown(const juce::MouseEvent& event)
{
    if (selectionGesture.begin(*this, event))
    {
        pointerDown = pressHighlight = false;
        return;
    }
    if (const auto* field = findParentComponentOfClass<ParameterField>(); field != nullptr)
    {
        if (field->getProperties()["dockInstanceNumber"].toString().isNotEmpty()
            && getBottomIndicatorBounds(RangeIndicator::boundsInside(*this, getValueBounds()), 0)
                   .contains(event.getPosition()))
            return; // Number badge: neither slot assignment nor value editing.
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

    // A Shift-click on a discrete control always means "previous", even
    // when the pointer happens to land on the range indicator itself.
    const auto reverseChoiceClick = choiceCount() > 1 && event.mods.isShiftDown()
        && !event.mods.isCtrlDown() && !event.mods.isAltDown();
    if (!reverseChoiceClick && areRangeIndicatorsVisible() && clickIsInsideThisValueBox
        && canAdjustValue() && momentaryAction == nullptr && choiceCount() != 2)
        if (directIndicator->begin(event.getEventRelativeTo(this), RangeIndicator::potentiometerBounds(*this, getValueBounds()), slider,
                                   choiceCount())) { repaint(); return; }

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

    if (momentaryAction != nullptr)
    {
        const auto point = event.getEventRelativeTo(this).getPosition();
        const auto onControl = getValueBounds().contains(point);
        if (canAdjustValue() && !event.mods.isPopupMenu() && onControl)
        {
            momentaryPressed = pointerDown = pressHighlight = true;
            auto& source = slider;
            source.setValue(source.getMaximum(), juce::sendNotificationSync);
            revealValue();
            repaintValueOverlay(*this);
        }
        return;
    }

    const auto valueCanOpenPrompt = interactionEnabled && isEnabled();

    pointerDown = true;
    pressHighlight = valueCanOpenPrompt;
    if (choiceCount() == 2 && canAdjustValue()
        && getValueBounds().contains(event.getEventRelativeTo(this).getPosition())
        && momentaryAction == nullptr && !event.mods.isPopupMenu())
    {
        // Treat every binary choice as the same push-feedback toggle: show
        // the *next* thumb position immediately, but do not touch the slider
        // or APVTS until the normal mouseUp commit path runs.
        togglePreviewActive = true;
        togglePreviewPosition = RangeIndicator::rangePosition(slider) < 0.5 ? 1.0 : 0.0;
        repaintValueOverlay(*this);
    }
    if (canAdjustValue() && choiceCount() != 2
        && getValueBounds().contains(event.getEventRelativeTo(this).getPosition()))
        directIndicator->beginFromValue(event.getEventRelativeTo(this), RangeIndicator::potentiometerBounds(*this, getValueBounds()), slider, choiceCount());
    repaint();
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
    momentaryPressed = pointerDown = pressHighlight = false;
    auto& source = slider;
    source.setValue(momentaryRestingState ? source.getMaximum() : source.getMinimum(), juce::sendNotificationSync);
    if (!pointerOnValue()) valueTransitionAt = juce::Time::getMillisecondCounterHiRes() + valueTransitionDelayMs;
    repaintValueOverlay(*this);
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
            pressHighlight = false;
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
    pressHighlight = false;
    togglePreviewActive = false;
    repaintValueOverlay(*this);

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
    if (pointerDown)
        pressHighlight = false;
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

void ValueBoxComponent::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (auto* viewport = findParentComponentOfClass<juce::Viewport>())
    {
        // The dock is a horizontal (or rotated vertical) strip, not a page.
        // Route its wheel gestures through its own axis-aware handler.
        if (auto* dock = viewport->findParentComponentOfClass<QuickAccessStrip>())
        {
            dock->mouseWheelMove(event.getEventRelativeTo(dock), wheel);
            return;
        }
        const auto viewedHeight = viewport->getViewedComponent() != nullptr
            ? viewport->getViewedComponent()->getHeight()
            : 0;
        scrollViewportWithWheel(*viewport, viewedHeight, wheel, event.mods.isShiftDown());
    }
    else
        juce::Component::mouseWheelMove(event, wheel);
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
    pressHighlight = false;
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

void ValueBoxComponent::paintRangeIndicators(juce::Graphics& g, juce::Component& root,
    const std::function<bool(const juce::Component&)>& include)
{
    const juce::Graphics::ScopedSaveState restoreClip(g);
    // Resolve the control under the pointer once for the whole overlay. Testing
    // every marker separately repeats the component-tree hit test during scroll.
    ValueBoxComponent* hoveredControl = nullptr;
    juce::Point<float> pointerPosition;
    const auto& desktop = juce::Desktop::getInstance();
    if (desktop.getNumDraggingMouseSources() == 0)
        for (const auto& source : desktop.getMouseSources())
        {
            if (!source.isMouse()) continue;
            const auto point = root.getLocalPoint(nullptr, source.getScreenPosition());
            auto* target = root.getComponentAt(point.roundToInt());
            for (; target != nullptr && target != &root; target = target->getParentComponent())
                if (auto* value = dynamic_cast<ValueBoxComponent*>(target))
                {
                    hoveredControl = value;
                    pointerPosition = point;
                    break;
                }
            if (hoveredControl != nullptr) break;
        }
    const auto paintHover = [&](ValueBoxComponent& value, const char* region,
                                const auto marker, bool enabled, bool held = false)
    {
        const bool hovered = enabled && (&value == hoveredControl)
            && marker.toFloat().contains(pointerPosition);
        if (!held && !isHoverInversionReady(value, hovered, region)) return false;
        if (!enabled) return false;
        g.setColour(held ? uiWhite : juce::Colour(0xff666666));
        g.fillRect(marker);
        return true;
    };
    std::vector<juce::Rectangle<int>> popupPanels;
    const auto foregroundOccluders = getForegroundOccluders(root);
    const auto excludePopups = [&](const auto& self, juce::Component& component, bool inPrompt) -> void
    {
        inPrompt = inPrompt || dynamic_cast<PromptComponent*>(&component) != nullptr;
        if (inPrompt && (component.isOpaque() || dynamic_cast<ValueBoxComponent*>(&component) != nullptr
            || dynamic_cast<BoxTextButton*>(&component) != nullptr))
        {
            auto panel = root.getLocalArea(&component, component.getLocalBounds());
            for (auto* parent = component.getParentComponent(); parent != nullptr && parent != &root;
                 parent = parent->getParentComponent())
                panel = panel.getIntersection(root.getLocalArea(parent, parent->getLocalBounds()));
            popupPanels.push_back(panel);
            return;
        }
        for (auto* child : component.getChildren())
            if (child->isVisible()) self(self, *child, inPrompt);
    };
    excludePopups(excludePopups, root, false);
    const auto visit = [&](const auto& self, juce::Component& component, bool inPrompt) -> void
    {
        if (include && !include(component)) return;
        const juce::Graphics::ScopedSaveState saved(g);
        inPrompt = inPrompt || dynamic_cast<PromptComponent*>(&component) != nullptr;
        if (&component != &root)
        {
            const auto bounds = root.getLocalArea(&component, component.getLocalBounds());
            if (bounds.isEmpty() || !g.reduceClipRegion(bounds)) return;
        }
        if (auto* value = dynamic_cast<ValueBoxComponent*>(&component))
        {
            const auto bounds = root.getLocalArea(value, value->getValueBounds());
            if (bounds.isEmpty()) return;
            if (!inPrompt)
            {
                for (const auto& panel : popupPanels) g.excludeClipRegion(panel);
                // Indicators are an overlay pass; respect the same fixed-button
                // occlusion as the control foregrounds painted underneath them.
                for (auto* cover : foregroundOccluders)
                    if (cover != value && !cover->isParentOf(value))
                        g.excludeClipRegion(root.getLocalArea(cover, cover->getLocalBounds()));
            }
            const auto indicatorBounds = RangeIndicator::boundsInside(*value, bounds);
            const auto inactiveColour = uiIndicatorInactive;
            const auto enabled = value->interactionEnabled && value->isEnabled();
            const auto disabledColour = uiIndicatorDisabled;
            const auto rangeVisible = value->areRangeIndicatorsVisible();
            const auto manuallySelected = isManualControlSelected(*value);
            const auto count = value->choiceCount();
            const auto directVisual = value->directIndicator->visualPosition();
            const auto visual = value->togglePreviewActive
                ? value->togglePreviewPosition : directVisual;
            const auto position = visual >= 0.0 ? visual : RangeIndicator::rangePosition(value->slider);
            const auto scale = juce::jmax(0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
            const auto* field = value->findParentComponentOfClass<ParameterField>();
            const auto dockNumber = field != nullptr
                ? field->getProperties()["dockInstanceNumber"].toString() : juce::String();
            std::array<int, 2> bottomStates;
            for (int index = 0; index < 2; ++index)
                bottomStates[static_cast<size_t>(index)] = value->isBottomIndicatorVisible(index)
                    ? (index == 0 && dockNumber.isNotEmpty() ? 0
                        : value->bottomIndicators[static_cast<size_t>(index)].state()) : -2;
            struct IndicatorLayer final : juce::DynamicObject
            {
                juce::String key;
                juce::Image image;
            };
            auto& properties = value->getProperties();
            auto* layer = dynamic_cast<IndicatorLayer*>(properties["indicatorLayer"].getDynamicObject());
            if (layer == nullptr)
            {
                layer = new IndicatorLayer();
                properties.set("indicatorLayer", juce::var(layer));
            }
            // Cache the indicator geometry in local control coordinates.
            const auto key = juce::String(value->getWidth()) + ":" + juce::String(value->getHeight())
                + ":" + juce::String(scale, 6) + ":" + juce::String(defaultUiCornerRadius)
                + ":" + juce::String(enabled ? 1 : 0) + ":" + juce::String(count)
                + ":" + juce::String(position, 17)
                + ":" + juce::String((value->numericControlEnabled || count > 0) && !value->textInputActive ? 1 : 0)
                + ":" + juce::String(bottomStates[0]) + ":" + juce::String(bottomStates[1])
                + ":" + dockNumber
                + ":" + juce::String(rangeVisible ? 1 : 0)
                + ":" + juce::String(value->directIndicator->hasInteractionHighlight() || value->pressHighlight ? 1 : 0)
                + ":" + juce::String(manuallySelected ? 1 : 0)
                + ":" + juce::String(value->getBottomIndicatorBounds(indicatorBounds, 0).getWidth())
                + ":" + juce::String(value->getBottomIndicatorBounds(indicatorBounds, 0).getHeight());
            if (layer->key != key || !layer->image.isValid())
            {
                layer->image = juce::Image(juce::Image::ARGB,
                    juce::jmax(1, int(std::ceil(double(value->getWidth()) * double(scale)))),
                    juce::jmax(1, int(std::ceil(double(value->getHeight()) * double(scale)))), true);
                juce::Graphics ink(layer->image);
                ink.addTransform(juce::AffineTransform::scale(scale));
                const auto localBounds = RangeIndicator::boundsInside(*value, value->getValueBounds());
                for (int index = 0; index < 2; ++index)
                    if (value->isBottomIndicatorVisible(index))
                    {
                        const auto state = bottomStates[static_cast<size_t>(index)];
                        // SL remains visually active even when the value control
                        // is disabled by a domain, mode or module policy.
                        const bool indicatorEnabled = index == 0 && dockNumber.isEmpty()
                            ? value->isEnabled() : enabled;
                        const auto colour = !indicatorEnabled || state < 0 ? disabledColour
                                          : state == 1 ? uiWhite : inactiveColour;
                        const auto marker = value->getBottomIndicatorBounds(localBounds, index);
                        paintIndicator(ink, marker.toFloat(), colour, float(defaultUiCornerRadius));
                        drawEmbeddedButtonText(ink,
                            index == 0 && dockNumber.isNotEmpty() ? dockNumber
                                : juce::String(index == 0 ? "SL" : "BP"), marker);
                    }
                // All top control indicators use the same potentiometer rail,
                // including BAND/SETT, module selectors, choices and toggles.
                if (rangeVisible && (value->numericControlEnabled || count > 0) && !value->textInputActive)
                    RangeIndicator::paint(ink, RangeIndicator::potentiometerBounds(*value, value->getValueBounds()), value->slider,
                        enabled ? (value->directIndicator->hasInteractionHighlight() || value->pressHighlight ? uiWhite : uiIndicatorActive) : disabledColour, visual, value);
                layer->key = key;
            }
            g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
            g.drawImageTransformed(layer->image, juce::AffineTransform::scale(1.0f / scale)
                .translated(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY())));
            for (int index = 0; index < 2; ++index)
            {
                if (!value->isBottomIndicatorVisible(index)) continue;
                const auto marker = value->getBottomIndicatorBounds(indicatorBounds, index);
                if (paintHover(*value, index == 0 ? "value.bottom0" : "value.bottom1", marker,
                    value->canUseBottomIndicator(index)
                        && (value == hoveredControl || value->pressedBottomIndicator == index),
                    value->pressedBottomIndicator == index))
                    drawEmbeddedButtonText(g, index == 0 ? "SL" : "BP", marker, value->pressedBottomIndicator == index ? juce::Colour(0xff222222) : uiWhite);
            }
            if (rangeVisible && (value->numericControlEnabled || count > 0) && !value->textInputActive)
            {
                const auto marker = RangeIndicator::continuousMarker(
                    RangeIndicator::potentiometerBounds(*value, bounds),
                    visual >= 0.0 ? visual : RangeIndicator::rangePosition(value->slider));
                const bool interacting = value->directIndicator->hasInteractionHighlight();
                const bool enabledHover = value->interactionEnabled && value->isEnabled();
                if (manuallySelected)
                {
                    // Selection is white at rest, but must still respond to
                    // hovering the thumb.  Use a dark centre with a WHITE
                    // outline, so the control remains visibly Ctrl-marked.
                    if (isHoverInversionReady(*value,
                        enabledHover && !interacting && value == hoveredControl
                            && marker.contains(pointerPosition), "value.top"))
                    {
                        g.setColour(juce::Colour(0xff666666));
                        g.fillRect(marker);
                        g.setColour(uiWhite);
                        g.drawRect(marker, 1.0f);
                    }
                }
                else
                {
                    paintHover(*value, "value.top", marker, enabledHover, interacting);
                }
            }
            // The badge hover occupies the *existing* bottom indicator row.
            // Cover both labels (number + BP), without a tooltip or popup.
            // In particular, hovering BP itself leaves its click behaviour and
            // its normal active-state indicator completely untouched.
            if (value->dockInstanceNameHovered && dockNumber.isNotEmpty())
            {
                const auto name = field->getProperties()["dockInstanceName"].toString();
                if (name.isNotEmpty())
                {
                    auto labelBounds = value->getBottomIndicatorBounds(indicatorBounds, 0);
                    const auto right = value->isBottomIndicatorVisible(1)
                        ? value->getBottomIndicatorBounds(indicatorBounds, 1).getRight()
                        : indicatorBounds.getRight() - RangeIndicator::edgeInset;
                    labelBounds.setRight(juce::jmax(labelBounds.getRight(), right));
                    g.setColour(enabled ? uiIndicatorInactive : disabledColour);
                    g.fillRect(labelBounds);
                    if (drawEmbeddedButtonText(g, name, labelBounds))
                        scheduleMarqueeRepaint(*value, value->marqueeRepaintPending);
                }
            }
            value->paintValueFrame(g, bounds, true);
            return;
        }
        for (auto* child : component.getChildren())
            if (child->isVisible()) self(self, *child, inPrompt);
    };
    visit(visit, root, false);
}
