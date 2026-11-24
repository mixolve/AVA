#include "Controls.h"
#include "ValueEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

void BoxTextButton::setCancelClickOnLeave(const bool shouldEnable) noexcept
{
    cancelClickOnLeave = shouldEnable;
}

BoxTextButton::BoxTextButton()
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    (void) getMouseCursor();
}

void BoxTextButton::setSelectionHighlighted(const bool shouldBeActive)
{
    if (selectionHighlighted == shouldBeActive)
        return;

    selectionHighlighted = shouldBeActive;
    repaint();
}

bool BoxTextButton::hasActiveHighlight() const noexcept
{
    return isEnabled() && (selectionHighlighted || getToggleState());
}

void BoxTextButton::paintOutline(juce::Graphics& graphics,const juce::Rectangle<int> bounds) const
{
    ControlAppearance style;
    style.frameVisible=borderVisible;
    const juce::Graphics::ScopedSaveState saved(graphics);
    graphics.reduceClipRegion(bounds);
    style.paintFrame(graphics, bounds);
}

void BoxTextButton::setInteractionOutlineEnabled(const bool shouldEnable) noexcept
{
    interactionOutlineEnabled = shouldEnable;
}

void BoxTextButton::setFillColour(const juce::Colour colour) noexcept
{
    if (fillColour == colour)
        return;

    fillColour = colour;
    repaint();
}

void BoxTextButton::setBottomIndicator(int index, ValueBoxComponent::BottomIndicator indicator, juce::String label)
{
    jassert(juce::isPositiveAndBelow(index, int(bottomIndicators.size())));
    if (!juce::isPositiveAndBelow(index, int(bottomIndicators.size()))) return;
    if (label.isNotEmpty()) bottomIndicatorLabels[size_t(index)] = std::move(label);
    bottomIndicators[static_cast<size_t>(index)] = std::move(indicator);
    bottomIndicatorVisible[static_cast<size_t>(index)] = true;
    repaint();
}

juce::Rectangle<int> BoxTextButton::getBottomIndicatorBounds(int index) const
{
    const auto columns = juce::jmax(1, int(std::count(bottomIndicatorVisible.begin(), bottomIndicatorVisible.end(), true)));
    const auto slot = int(std::count(bottomIndicatorVisible.begin(), bottomIndicatorVisible.begin() + index, true));
    const auto area = RangeIndicator::boundsInside(*this, getLocalBounds());
    const auto height = RangeIndicator::bottomControlSize(*this, getLocalBounds());
    return controlStripCell(area.withTop(area.getBottom() - height), slot, columns);
}

int BoxTextButton::bottomIndicatorAt(juce::Point<int> point) const
{
    for (int index = 0; index < int(bottomIndicators.size()); ++index)
        if (bottomIndicatorVisible[static_cast<size_t>(index)] && getBottomIndicatorBounds(index).contains(point))
            return index;
    return -1;
}

void BoxTextButton::setTextJustification(const juce::Justification justification) noexcept
{
    textJustification = justification;
    repaint();
}

void BoxTextButton::setBorderVisible(const bool shouldShow) noexcept
{
    if (borderVisible == shouldShow)
        return;

    borderVisible = shouldShow;
    repaint();
}

void BoxTextButton::flashConfirmation()
{
    confirmationFlashActive = true;
    repaint();
    juce::Timer::callAfterDelay(120, [safeThis = juce::Component::SafePointer<BoxTextButton>(this)]
    {
        if (safeThis == nullptr)
            return;
        safeThis->confirmationFlashActive = false;
        safeThis->repaint();
    });
}

juce::MouseCursor BoxTextButton::getMouseCursor()
{
    static const juce::MouseCursor moveCursor(juce::MouseCursor::DraggingHandCursor);
    return (controlDragActive || moveTargetAvailable) ? moveCursor : juce::TextButton::getMouseCursor();
}

void BoxTextButton::setMoveTargetAvailable(const bool available)
{
    if (moveTargetAvailable == available) return;
    moveTargetAvailable = available;
    // Target activation must not force a native cursor update for every row.
    if (isMouseHovering(*this)) updateMouseCursor();
}

void BoxTextButton::setDragTargetHighlighted(const bool shouldShow) noexcept
{
    if (dragTargetHighlighted == shouldShow)
        return;

    dragTargetHighlighted = shouldShow;
    repaint();
}

void BoxTextButton::refreshBottomIndicatorStates()
{
    bool changed = false;
    for (size_t index = 0; index < bottomIndicators.size(); ++index)
    {
        const auto state = bottomIndicatorVisible[index] ? bottomIndicators[index].state() : -1;
        if (lastBottomIndicatorStates[index] != state)
        {
            lastBottomIndicatorStates[index] = state;
            changed = true;
        }
    }
    if (changed) repaint();
}

void BoxTextButton::paintBottomIndicators(juce::Graphics& graphics)
{
        for (int index = 0; index < int(bottomIndicators.size()); ++index)
            if (bottomIndicatorVisible[static_cast<size_t>(index)])
            {
                const auto& indicator = bottomIndicators[static_cast<size_t>(index)];
                const auto bounds = getBottomIndicatorBounds(index);
                indicator.paint(graphics, bounds, isEnabled(), float(defaultUiCornerRadius));
                const auto* region = index == 0 ? "box.bottom0" : index == 1 ? "box.bottom1" : "box.bottom2";
                RangeIndicator::paintHover(graphics, *this, *this, bounds,
                    isEnabled() && indicator.write != nullptr && indicator.state() >= 0,
                    pressedBottomIndicator == index, region);
                const auto canHover = isEnabled() && indicator.write != nullptr && indicator.state() >= 0;
                const auto inverted = pressedBottomIndicator != index
                    && isHoverInversionReady(*this, canHover && isMouseHovering(*this, bounds), region);
                drawEmbeddedButtonText(graphics, bottomIndicatorLabels[size_t(index)], bounds,
                    inverted ? uiWhite : juce::Colour(0xff222222));
            }
}

void BoxTextButton::paintButton(juce::Graphics& graphics, const bool shouldDrawButtonAsHighlighted, const bool shouldDrawButtonAsDown)
{
    const auto active=hasActiveHighlight();
    if (bool(getProperties().getWithDefault("routingPinnedTile", false)))
    {
        ControlAppearance tileAppearance;
        tileAppearance.frameColour = uiWhite;
        tileAppearance.fillColour = uiGreyDark;
        tileAppearance.paint(graphics, getLocalBounds());

        const auto caption = getControlTextBounds(*this, getLocalBounds());
        const auto hover = isHoverInversionReady(*this,
            isEnabled() && isMouseHovering(*this, caption), "routing.pinned.caption");
        const bool moving = bool(getProperties().getWithDefault("routing.instance.dragging", false));
        paintControlTextButton(graphics, caption, hover, isEnabled() && active,
                               isEnabled() && (moving || pressHighlight || shouldDrawButtonAsDown),
                               isEnabled() ? uiGreyLight : uiIndicatorDisabled);
        if (drawSquareControlText(graphics, getButtonText(), caption, textJustification))
            scheduleMarqueeRepaint(*this, marqueeRepaintPending);
        paintBottomIndicators(graphics);
        return;
    }
    ControlAppearance style;
    juce::ignoreUnused(shouldDrawButtonAsHighlighted);
    style.fillColour=fillColour;
    style.paintBackground(graphics,getLocalBounds());

    const auto drawOuterBorder = [&graphics, this]
    {
        paintBottomIndicators(graphics);
        paintOutline(graphics, getLocalBounds());
    };

    drawOuterBorder();

    const auto font = makeUiFont();
    const auto textBounds = getControlTextBounds(*this,getLocalBounds());
    const auto delayedHover = isHoverInversionReady(*this,
        isEnabled() && interactionOutlineEnabled && isMouseHovering(*this, textBounds), "box.body");
    paintControlTextButton(graphics, textBounds, delayedHover
        || (isEnabled() && bool(getProperties().getWithDefault("osc.move.highlighted", false))), isEnabled() && active,
        isEnabled() && (bool(getProperties().getWithDefault("routing.instance.dragging", false))
            || (!dragActive && (pressHighlight || shouldDrawButtonAsDown || confirmationFlashActive))), isEnabled() ? uiGreyLight : uiIndicatorDisabled);
    const auto textInkBounds = textBounds.reduced(int(getProperties().getWithDefault("textContentInset", 0)), 0);
    graphics.setFont(font);
    bool scrolling = false;
    if (getHeight() <= getCompactControlSize(*this) + 2
        && getWidth() <= getCompactControlSize(*this) + 2)
        scrolling = drawCompactControlText(graphics, getButtonText(), textInkBounds, textJustification);
    else if (bool(getProperties().getWithDefault("singleLineText", false)))
        scrolling = drawLoopingText(graphics, getButtonText(), textInkBounds, font, textJustification);
    else if (bool(getProperties().getWithDefault("twoLineText", false))
             || std::abs(getHeight() - getSquareControlSize(*this)) <= 1)
        scrolling = drawSquareControlText(graphics, getButtonText(), textInkBounds, textJustification);
    else
    {
        scrolling = drawLoopingText(graphics, getButtonText(), textInkBounds, font, textJustification);
    }
    if (scrolling) scheduleMarqueeRepaint(*this, marqueeRepaintPending);
}
