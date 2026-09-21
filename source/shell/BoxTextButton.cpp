#include "Controls.h"
#include "TablerIcons.h"

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
}

BoxTextButton::~BoxTextButton()
{
    dismissActionPrompt();
    stopTimer();
}

void BoxTextButton::setAlwaysAccentOutline(const bool shouldAlwaysAccent)
{
    if (alwaysAccentOutline == shouldAlwaysAccent)
        return;

    alwaysAccentOutline = shouldAlwaysAccent;
    repaint();
}

void BoxTextButton::setHorizontalBidirectionalArrowVisible(const bool shouldShow) noexcept
{
    if (horizontalBidirectionalArrowVisible == shouldShow)
        return;

    horizontalBidirectionalArrowVisible = shouldShow;

    if (shouldShow && horizontalBidirectionalArrowImage.isNull())
        horizontalBidirectionalArrowImage = loadTablerIcon("layers-difference", iconGlyphSize);

    repaint();
}

void BoxTextButton::setTablerIcon(const char* iconName)
{
    iconOnlyText = false;
    tablerIconImage = loadTablerIcon(iconName, iconGlyphSize);
    repaint();
}

void BoxTextButton::setIconOnlyText(const juce::String& text)
{
    tablerIconImage = {};
    iconOnlyText = true;
    setButtonText(text);
}

bool BoxTextButton::usesIconOnlyContent() const noexcept
{
    return iconOnlyText
        || tablerIconImage.isValid()
        || (horizontalBidirectionalArrowVisible && getButtonText().isEmpty());
}

void BoxTextButton::setToggleAccentVisible(const bool shouldShow) noexcept
{
    if (toggleAccentVisible == shouldShow)
        return;

    toggleAccentVisible = shouldShow;
    repaint();
}

void BoxTextButton::setPressFillEnabled(const bool shouldEnable) noexcept
{
    pressFillEnabled = shouldEnable;
}

void BoxTextButton::setClearsParameterFocusOnMouseDown(const bool shouldClear) noexcept
{
    clearsParameterFocusOnMouseDown = shouldClear;
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

void BoxTextButton::setInteractionFillColour(const juce::Colour colour) noexcept
{
    if (interactionFillColour == colour)
        return;

    interactionFillColour = colour;
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

void BoxTextButton::clearTextColourOverride()
{
    if (! hasTextColourOverride)
        return;

    hasTextColourOverride = false;
    repaint();
}

void BoxTextButton::setLongPressAction(std::function<void()> action, const int delayMs, juce::String promptText)
{
    longPressAction = std::move(action);
    longPressDelayMs = juce::jmax(1, delayMs);
    longPressPromptText = std::move(promptText);
}

void BoxTextButton::setLongPressPromptActions(std::function<void()> resetAction,
                                               std::function<void()> hostAction,
                                               juce::String primaryPromptText)
{
    longPressResetAction = std::move(resetAction);
    longPressHostAction = std::move(hostAction);
    longPressPrimaryPromptText = std::move(primaryPromptText);
}

void BoxTextButton::setLongPressTrailingPromptAction(std::function<void()> action, juce::String promptText)
{
    longPressTrailingAction = std::move(action);
    longPressTrailingPromptText = std::move(promptText);
}

void BoxTextButton::setDragTargetOutlineVisible(const bool shouldShow) noexcept
{
    if (dragTargetOutlineVisible == shouldShow)
        return;

    dragTargetOutlineVisible = shouldShow;
    repaint();
}

void BoxTextButton::flashConfirmationOutline()
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
    longPressArmed = false;
    dragHoldEligible = false;
    dragHoldArmed = false;
    setViewportIgnoreDragFlag(false);
    actionPromptOriginalText = getButtonText();
    actionPromptActive = true;
    actionPromptPressedIndex = -1;
    actionPromptHoverIndex = isMouseHovering(*this) ? getActionPromptHitIndex(getMouseXYRelative()) : -1;
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
        + (longPressHostAction != nullptr ? 1 : 0)
        + ((onMoveArmed != nullptr || onDragDrop != nullptr) ? 1 : 0)
        + (longPressTrailingAction != nullptr ? 1 : 0);
}

int BoxTextButton::getActionPromptHitIndex(const juce::Point<int> position) const noexcept
{
    const auto actionCount = getActionPromptCount();

    if (actionCount == 0)
        return -1;

    auto bounds = getLocalBounds().reduced(1);
    const auto actionWidth = juce::jmax(0, (bounds.getWidth() - (actionCount - 1)) / actionCount);

    for (auto index = 0; index < actionCount; ++index)
    {
        const auto isLastAction = index + 1 == actionCount;
        const auto actionBounds = bounds.removeFromLeft(isLastAction ? bounds.getWidth() : actionWidth);

        if (actionBounds.contains(position))
            return index;

        if (! isLastAction)
            bounds.removeFromLeft(1);
    }

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

void BoxTextButton::paintButton(juce::Graphics& graphics, bool, bool)
{
    const auto interactionHighlight = isEnabled()
        && pressFillEnabled
        && (isMouseHovering(*this) || (pressHighlight && ! dragActive));
    const auto accentActive = isEnabled() && (alwaysAccentOutline || (toggleAccentVisible && getToggleState()));
    const auto whiteOutlineActive = confirmationFlashActive
        || dragTargetOutlineVisible
        || (accentActive && accentColour == uiWhite);
    const auto fill = actionPromptActive
        ? uiGreyDark
        : (interactionHighlight ? interactionFillColour : fillColour);
    const auto outline = (confirmationFlashActive || dragTargetOutlineVisible)
        ? uiWhite
        : (accentActive ? accentColour : uiGrey500);

    if (actionPromptActive || fillVisible || interactionHighlight)
    {
        graphics.setColour(fill);
        graphics.fillRect(getLocalBounds());
    }

    if (borderVisible)
    {
        graphics.setColour(outline);
        graphics.drawRect(getLocalBounds(), whiteOutlineActive ? 2 : 1);
    }

    const auto textColour = isEnabled()
        ? (interactionHighlight ? uiBlack : (hasTextColourOverride ? textColourOverride : uiWhite))
        : uiGrey500;
    const auto drawBottomDivider = [&graphics, this]
    {
        if (! dividerLineVisible)
            return;

        const auto bounds = getLocalBounds().toFloat();
        constexpr auto dividerThickness = static_cast<float>(frameLineThickness);
        graphics.setColour(uiGrey500);
        graphics.fillRect(bounds.withY(bounds.getBottom() - dividerThickness).withHeight(dividerThickness));
    };

    if (actionPromptActive)
    {
        auto promptBounds = getLocalBounds().reduced(1);
        const auto font = makeUiFont();
        graphics.setFont(font);

        const auto actionCount = getActionPromptCount();
        const auto actionWidth = actionCount > 0
            ? juce::jmax(0, (promptBounds.getWidth() - (actionCount - 1)) / actionCount)
            : 0;
        juce::StringArray promptLabels;

        if (longPressResetAction != nullptr)
            promptLabels.add(longPressPrimaryPromptText);
        if (longPressHostAction != nullptr)
            promptLabels.add("H?");
        if (onMoveArmed != nullptr || onDragDrop != nullptr)
            promptLabels.add("M?");
        if (longPressTrailingAction != nullptr)
            promptLabels.add(longPressTrailingPromptText);

        for (int index = 0; index < promptLabels.size(); ++index)
        {
            const auto isLastAction = index + 1 == promptLabels.size();
            const auto actionBounds = promptBounds.removeFromLeft(isLastAction ? promptBounds.getWidth() : actionWidth);
            const auto promptHighlighted = actionPromptPressedIndex == index
                || (actionPromptPressedIndex < 0 && isMouseHovering(*this) && actionPromptHoverIndex == index);

            if (promptHighlighted)
            {
                graphics.setColour(uiGreyLight);
                graphics.fillRect(actionBounds);
            }

            graphics.setColour(promptHighlighted ? uiBlack : uiWhite);
            if (drawLoopingText(graphics,
                                promptLabels[index],
                                actionBounds.reduced(uiGap, 0),
                                font,
                                juce::Justification::centred))
                scheduleMarqueeRepaint();

            if (! isLastAction)
            {
                auto dividerBounds = promptBounds.removeFromLeft(1);
                graphics.setColour(uiGrey500);
                graphics.fillRect(dividerBounds);
            }
        }

        drawBottomDivider();
        return;
    }

    const auto iconBounds = getLocalBounds()
        .withSizeKeepingCentre(static_cast<int>(iconGlyphSize), static_cast<int>(iconGlyphSize))
        .toFloat();

    if (tablerIconImage.isValid())
    {
        juce::DrawableImage drawable;
        drawable.setImage(tablerIconImage);
        drawable.setOverlayColour(interactionHighlight ? uiBlack : textColour);
        drawable.drawWithin(graphics, iconBounds, juce::RectanglePlacement::centred, 1.0f);
        drawBottomDivider();
        return;
    }

    if (horizontalBidirectionalArrowVisible && getButtonText().isEmpty())
    {
        if (horizontalBidirectionalArrowImage.isValid())
        {
            juce::DrawableImage drawable;
            drawable.setImage(horizontalBidirectionalArrowImage);
            drawable.setOverlayColour(interactionHighlight ? uiBlack : textColour);
            drawable.drawWithin(graphics, iconBounds, juce::RectanglePlacement::centred, 1.0f);
            drawBottomDivider();
            return;
        }

        const auto centreY = static_cast<float>(getHeight()) * 0.5f;
        const auto leftX = 6.0f;
        const auto rightX = juce::jmax(leftX + 8.0f, static_cast<float>(getWidth()) - 7.0f);
        constexpr auto arrowHeadWidth = 4.0f;
        constexpr auto arrowHeadHeight = 4.0f;

        graphics.setColour(textColour);
        graphics.drawLine(leftX, centreY, rightX, centreY, 2.0f);
        graphics.drawLine(leftX, centreY, leftX + arrowHeadWidth, centreY - arrowHeadHeight, 2.0f);
        graphics.drawLine(leftX, centreY, leftX + arrowHeadWidth, centreY + arrowHeadHeight, 2.0f);
        graphics.drawLine(rightX, centreY, rightX - arrowHeadWidth, centreY - arrowHeadHeight, 2.0f);
        graphics.drawLine(rightX, centreY, rightX - arrowHeadWidth, centreY + arrowHeadHeight, 2.0f);
        drawBottomDivider();
        return;
    }

    graphics.setColour(textColour);
    const auto font = makeUiFont();
    graphics.setFont(font);
    const auto textBounds = getLocalBounds().reduced(uiGap, 0);

    if (getTextPixelWidth(font, getButtonText()) > textBounds.getWidth())
    {
        drawLoopingText(graphics, getButtonText(), textBounds, font, textJustification);
        scheduleMarqueeRepaint();
        drawBottomDivider();
        return;
    }

    if (drawLoopingText(graphics, getButtonText(), textBounds, font, textJustification))
        scheduleMarqueeRepaint();
    drawBottomDivider();
}

void BoxTextButton::scheduleMarqueeRepaint()
{
    if (marqueeRepaintPending || ! isShowing())
        return;

    marqueeRepaintPending = true;
    juce::Timer::callAfterDelay(16, [safeThis = juce::Component::SafePointer<BoxTextButton>(this)]
    {
        if (safeThis == nullptr)
            return;

        safeThis->marqueeRepaintPending = false;
        safeThis->repaint();
    });
}
