#include "Controls.h"
#include "TablerIcons.h"

#include <algorithm>
#include <array>
#include <utility>

class BoxTextButton::PromptDismissListener final : public juce::MouseListener
{
public:
    explicit PromptDismissListener(BoxTextButton& ownerIn) : owner(ownerIn) {}

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (event.eventComponent != &owner)
            owner.dismissActionPrompt();
    }

private:
    BoxTextButton& owner;
};

void BoxTextButton::setCancelClickOnLeave(const bool shouldEnable) noexcept
{
    cancelClickOnLeave = shouldEnable;
}

BoxTextButton::BoxTextButton(const juce::Colour accent)
    : accentColour(accent)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    (void) getMouseCursor();
}

BoxTextButton::~BoxTextButton()
{
    dismissActionPrompt();
    stopTimer();
}

void BoxTextButton::setActiveFill(const bool shouldBeActive)
{
    if (activeFillEnabled == shouldBeActive)
        return;

    activeFillEnabled = shouldBeActive;
    repaint();
}

bool BoxTextButton::hasActiveHighlight() const noexcept
{
    return isEnabled() && (confirmationFlashActive || dragTargetHighlighted
        || (accentColour==uiWhite && (activeFillEnabled || (toggleActiveFillEnabled && getToggleState()))));
}

void BoxTextButton::paintOutline(juce::Graphics& graphics,const juce::Rectangle<int> bounds) const
{
    ControlAppearance style(hasActiveHighlight() && !actionPromptActive,hoverOutlineAmount);
    style.normalFrameVisible=borderVisible;
    style.paintFrame(graphics,bounds);
}

void BoxTextButton::setTablerIcon(const char* iconName)
{
    tablerIconImage = loadTablerIcon(iconName, iconGlyphSize);
    repaint();
}

bool BoxTextButton::usesIconOnlyContent() const noexcept
{
    return tablerIconImage.isValid();
}

void BoxTextButton::setToggleActiveFillEnabled(const bool shouldShow) noexcept
{
    if (toggleActiveFillEnabled == shouldShow)
        return;

    toggleActiveFillEnabled = shouldShow;
    repaint();
}

void BoxTextButton::setInteractionOutlineEnabled(const bool shouldEnable) noexcept
{
    interactionOutlineEnabled = shouldEnable;
}

void BoxTextButton::setFillVisible(const bool shouldShow) noexcept
{
    if (fillVisible == shouldShow)
        return;

    fillVisible = shouldShow;
    repaint();
}

void BoxTextButton::setFillColour(const juce::Colour colour) noexcept
{
    if (fillColour == colour)
        return;

    fillColour = colour;
    repaint();
}

void BoxTextButton::setDividerLineVisible(const bool shouldShow) noexcept
{
    if (dividerLineVisible == shouldShow)
        return;

    dividerLineVisible = shouldShow;
    repaint();
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

void BoxTextButton::setTextColourOverride(const juce::Colour colour)
{
    hasTextColourOverride = true;
    textColourOverride = colour;
    repaint();
}

void BoxTextButton::setLongPressPromptActions(std::function<void()> resetAction,
                                               std::function<void()> secondaryAction)
{
    longPressResetAction = std::move(resetAction);
    longPressSecondaryAction = std::move(secondaryAction);
    longPressPrimaryPromptIconImage = longPressResetAction != nullptr
        ? loadTablerIcon("refresh", iconGlyphSize) : juce::Image {};
    longPressPrimaryPromptEnabled = true;
    longPressSecondaryPromptIconImage = longPressSecondaryAction != nullptr
        ? loadTablerIcon("map-pin-share", iconGlyphSize) : juce::Image {};
    longPressSecondaryPromptActive = false;
}

void BoxTextButton::setLongPressPrimaryPromptIcon(const char* iconName)
{
    longPressPrimaryPromptIconImage = loadTablerIcon(iconName, iconGlyphSize);
    repaint();
}

void BoxTextButton::setLongPressPrimaryPromptEnabled(const bool shouldEnable)
{
    longPressPrimaryPromptEnabled = shouldEnable;
    repaint();
}

void BoxTextButton::setLongPressSecondaryPromptIcon(const char* iconName)
{
    longPressSecondaryPromptIconImage = loadTablerIcon(iconName, iconGlyphSize);
    repaint();
}

void BoxTextButton::setLongPressSecondaryPromptActive(const bool shouldBeActive)
{
    if (longPressSecondaryPromptActive == shouldBeActive)
        return;
    longPressSecondaryPromptActive = shouldBeActive;
    repaint();
}

void BoxTextButton::setLongPressTrailingPromptIconAction(std::function<void()> action,
                                                        const char* iconName)
{
    longPressTrailingAction = std::move(action);
    longPressTrailingPromptIconImage = loadTablerIcon(iconName, iconGlyphSize);
}

void BoxTextButton::flashConfirmationFill()
{
    confirmationFlashActive = true;
    repaint();
    juce::Timer::callAfterDelay(500, [safeThis = juce::Component::SafePointer<BoxTextButton>(this)]
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
    return moveTargetAvailable ? moveCursor : juce::TextButton::getMouseCursor();
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

void BoxTextButton::showActionPrompt()
{
    if (getActionPromptCount() == 0)
        return;

    stopTimer();
    pointerDown = false;
    dragActive = false;
    pressHighlight = false;
    pressCanceled = false;
    longPressEligible = false;
    actionPromptOriginalText = getButtonText();
    ++pendingClickGeneration;
    actionPromptActive = true;
    actionPromptPressedIndex = -1;
    // A long press still owns the pointer. Preserve the hovered action even
    // while the mouse button is held; general hover deliberately excludes dragging.
    actionPromptHoverIndex = getActionPromptHitIndex(getMouseXYRelative());
    consumeNextMouseUp = true;

    if (promptDismissListener == nullptr)
        promptDismissListener = std::make_unique<PromptDismissListener>(*this);

    juce::Desktop::getInstance().addGlobalMouseListener(promptDismissListener.get());
    actionPromptGlobalListenerActive = true;
    repaint();
}

int BoxTextButton::getActionPromptCount() const noexcept
{
    return (longPressResetAction != nullptr ? 1 : 0)
        + (longPressSecondaryAction != nullptr ? 1 : 0)
        + ((onMoveArmed != nullptr) ? 1 : 0)
        + (longPressTrailingAction != nullptr ? 1 : 0);
}

juce::Rectangle<int> BoxTextButton::getActionPromptBounds(const int requestedIndex) const noexcept
{
    const auto actionCount = getActionPromptCount();
    if (! juce::isPositiveAndBelow(requestedIndex, actionCount))
        return {};
    const auto bounds = getLocalBounds();
    const auto left = bounds.getX() + bounds.getWidth() * requestedIndex / actionCount;
    const auto right = bounds.getX() + bounds.getWidth() * (requestedIndex + 1) / actionCount;
    return { left, bounds.getY(), right - left, bounds.getHeight() };
}

int BoxTextButton::getActionPromptHitIndex(const juce::Point<int> position) const noexcept
{
    const auto actionCount = getActionPromptCount();
    const auto moveIndex = (longPressResetAction != nullptr ? 1 : 0) + (longPressSecondaryAction != nullptr ? 1 : 0);
    for (auto index = 0; index < actionCount; ++index)
        if (getActionPromptBounds(index).contains(position))
            return (index == 0 && longPressResetAction != nullptr && !longPressPrimaryPromptEnabled)
                || (index == moveIndex && (onMoveArmed != nullptr) && !longPressMovePromptEnabled)
                ? -1 : index;

    return -1;
}

void BoxTextButton::dismissActionPrompt()
{
    if (! actionPromptActive && ! actionPromptGlobalListenerActive)
        return;

    if (actionPromptGlobalListenerActive && promptDismissListener != nullptr)
        juce::Desktop::getInstance().removeGlobalMouseListener(promptDismissListener.get());

    actionPromptGlobalListenerActive = false;
    actionPromptActive = false;
    actionPromptPressedIndex = -1;
    actionPromptHoverIndex = -1;

    if (actionPromptOriginalText.isNotEmpty() && getButtonText() != actionPromptOriginalText)
        setButtonText(actionPromptOriginalText);

    actionPromptOriginalText.clear();
    repaint();
}

void BoxTextButton::paintButton(juce::Graphics& graphics, const bool shouldDrawButtonAsHighlighted, bool)
{
    const auto active=hasActiveHighlight();
    const auto interactionHighlight = isEnabled()
        && (interactionOutlineEnabled || active)
        && (isMouseHovering(*this) || (dragTargetHighlighted && shouldDrawButtonAsHighlighted)
            || (pressHighlight && ! dragActive));
    const auto pointerHover=hoverAnimation.amount(interactionHighlight);
    hoverOutlineAmount=actionPromptActive?0.0f:pointerHover;
    ControlAppearance style(active && !actionPromptActive,pointerHover);
    style.normalFill=actionPromptActive?uiGreyDark:fillColour;
    style.fillVisible=actionPromptActive || fillVisible;
    style.normalContent=hasTextColourOverride?textColourOverride:uiWhite;
    activeFillAmount=active?1.0f:0.0f;
    style.paintBackground(graphics,getLocalBounds());

    const auto drawOuterBorder = [&graphics, this]
    {
        paintOutline(graphics, getLocalBounds());
    };

    const auto textColour=isEnabled()?style.contentColour():uiGreyLight;
    const auto drawBottomDivider = [&graphics, this]
    {
        if (! dividerLineVisible)
            return;

        const auto bounds = getLocalBounds().toFloat();
        constexpr auto dividerThickness = static_cast<float>(frameLineThickness);
        const auto divider = bounds.withY(bounds.getBottom() - dividerThickness).withHeight(dividerThickness);

        graphics.setColour(uiGreyLight);
        graphics.fillRect(divider);
    };

    if (actionPromptActive)
    {
        juce::Array<juce::Image> promptIcons;

        if (longPressResetAction != nullptr)
        {
            promptIcons.add(longPressPrimaryPromptIconImage);
        }
        if (longPressSecondaryAction != nullptr)
        {
            promptIcons.add(longPressSecondaryPromptIconImage);
        }
        if (onMoveArmed != nullptr)
        {
            static const auto movePromptIcon = loadTablerIcon("transfer-vertical", iconGlyphSize);
            promptIcons.add(movePromptIcon);
        }
        if (longPressTrailingAction != nullptr)
        {
            promptIcons.add(longPressTrailingPromptIconImage);
        }

        const auto singlePrompt = promptIcons.size() == 1;
        for (int index = 0; index < promptIcons.size(); ++index)
        {
            const auto actionBounds = getActionPromptBounds(index);
            const auto moveIndex = (longPressResetAction != nullptr ? 1 : 0) + (longPressSecondaryAction != nullptr ? 1 : 0);
            const auto promptEnabled = (index != 0 || longPressResetAction == nullptr || longPressPrimaryPromptEnabled)
                && (index != moveIndex || (onMoveArmed == nullptr) || longPressMovePromptEnabled);
            const auto promptHighlighted = promptEnabled && (actionPromptPressedIndex == index
                || (actionPromptPressedIndex < 0 && actionPromptHoverIndex == index));
            const auto secondaryIndex = longPressResetAction != nullptr ? 1 : 0;
            const auto promptActive = promptEnabled && longPressSecondaryAction != nullptr
                && index == secondaryIndex && longPressSecondaryPromptActive;

            ControlAppearance promptStyle(promptActive,promptHighlighted?1.0f:0.0f);
            promptStyle.fillVisible=!singlePrompt || promptHighlighted || promptActive;
            promptStyle.paintBackground(graphics,actionBounds);
            if(promptIcons[index].isValid()) {
                juce::DrawableImage drawable;
                drawable.setImage(promptIcons[index]);
                drawable.setOverlayColour(!promptEnabled?uiGreyLight:promptStyle.contentColour());
                drawable.drawWithin(graphics,actionBounds.withSizeKeepingCentre(
                    static_cast<int>(iconGlyphSize),static_cast<int>(iconGlyphSize)).toFloat(),
                    juce::RectanglePlacement::centred,1.0f);
            }

            if (index > 0)
            {
                graphics.setColour(uiGreyLight);
                graphics.fillRect(actionBounds.getX(), 1, 1, juce::jmax(0, getHeight() - 2));
            }
        }

        drawBottomDivider();
        drawOuterBorder();
        const auto hoveredAction=actionPromptPressedIndex>=0?actionPromptPressedIndex:actionPromptHoverIndex;
        if(hoveredAction>=0 && getActionPromptHitIndex(getActionPromptBounds(hoveredAction).getCentre())>=0)
        {
            ControlAppearance hovered(false,1.0f);hovered.normalFrameVisible=false;
            hovered.paintFrame(graphics,getActionPromptBounds(hoveredAction));
        }
        return;
    }

    drawOuterBorder();
    const auto iconBounds = getLocalBounds()
        .withSizeKeepingCentre(static_cast<int>(iconGlyphSize), static_cast<int>(iconGlyphSize))
        .toFloat();

    if (tablerIconImage.isValid())
    {
        juce::DrawableImage drawable;
        drawable.setImage(tablerIconImage);
        drawable.setOverlayColour(textColour);
        drawable.drawWithin(graphics, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        drawBottomDivider();
        return;
    }

    graphics.setColour(textColour);
    const auto font = makeUiFont();
    graphics.setFont(font);
    const auto textBounds = getLocalBounds().reduced(textHorizontalPadding, 0);

    if (!textScrollingEnabled)
        graphics.drawText(getButtonText(), textBounds, textJustification, true);
    else if (drawLoopingText(graphics, getButtonText(), textBounds, font, textJustification))
        scheduleMarqueeRepaint(*this, marqueeRepaintPending);
    drawBottomDivider();
}
