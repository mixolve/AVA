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
    longPressTrailingPromptIconImage = {};
    longPressTrailingPromptIsHostIcon = false;
}

void BoxTextButton::setLongPressTrailingPromptIconAction(std::function<void()> action,
                                                        const char* iconName)
{
    longPressTrailingAction = std::move(action);
    longPressTrailingPromptText.clear();
    longPressTrailingPromptIconImage = loadTablerIcon(iconName, iconGlyphSize);
    longPressTrailingPromptIsHostIcon = juce::String(iconName) == "map-pin-share";
}

void BoxTextButton::setLongPressAdditionalPromptIconAction(std::function<void()> action,
                                                          const char* iconName)
{
    longPressAdditionalPromptAction = std::move(action);
    longPressAdditionalPromptIconImage = loadTablerIcon(iconName, iconGlyphSize);
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
    ++pendingClickGeneration;
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
        + (longPressTrailingAction != nullptr ? 1 : 0)
        + (longPressAdditionalPromptAction != nullptr ? 1 : 0);
}

juce::Rectangle<int> BoxTextButton::getActionPromptBounds(const int requestedIndex) const noexcept
{
    const auto actionCount = getActionPromptCount();
    if (! juce::isPositiveAndBelow(requestedIndex, actionCount))
        return {};

    std::array<bool, 5> hostZones {};
    auto index = 0;
    if (longPressResetAction != nullptr) ++index;
    if (longPressHostAction != nullptr) hostZones[static_cast<size_t>(index++)] = true;
    if (onMoveArmed != nullptr || onDragDrop != nullptr) ++index;
    if (longPressTrailingAction != nullptr)
        hostZones[static_cast<size_t>(index++)] = longPressTrailingPromptIsHostIcon;

    auto bounds = getLocalBounds().reduced(1);
    const auto contentWidth = juce::jmax(0, bounds.getWidth() - (actionCount - 1));
    const auto hostCount = static_cast<int>(std::count(hostZones.begin(), hostZones.begin() + actionCount, true));
    const auto flexibleCount = actionCount - hostCount;
    const auto hostWidth = hostCount > 0
        ? juce::jmin(iconControlSize, juce::jmax(0, contentWidth - flexibleCount) / hostCount)
        : 0;
    const auto flexibleWidth = flexibleCount > 0
        ? (contentWidth - hostWidth * hostCount) / flexibleCount
        : 0;
    auto flexibleRemainder = flexibleCount > 0
        ? (contentWidth - hostWidth * hostCount) % flexibleCount
        : 0;

    if (flexibleCount == 0)
    {
        const auto groupWidth = hostWidth * hostCount + actionCount - 1;
        bounds = bounds.withSizeKeepingCentre(groupWidth, bounds.getHeight());
    }

    for (int actionIndex = 0; actionIndex < actionCount; ++actionIndex)
    {
        const auto width = hostZones[static_cast<size_t>(actionIndex)]
            ? hostWidth
            : flexibleWidth + (flexibleRemainder-- > 0 ? 1 : 0);
        auto actionBounds = bounds.removeFromLeft(width);
        if (actionIndex == requestedIndex)
            return actionBounds;
        if (actionIndex + 1 < actionCount)
            bounds.removeFromLeft(1);
    }

    return {};
}

int BoxTextButton::getActionPromptHitIndex(const juce::Point<int> position) const noexcept
{
    const auto actionCount = getActionPromptCount();
    for (auto index = 0; index < actionCount; ++index)
        if (getActionPromptBounds(index).contains(position))
            return index;

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
        if (whiteOutlineActive)
        {
            constexpr auto accentThickness = 1.5f;
            const auto bounds = getLocalBounds().toFloat();
            graphics.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(), bounds.getWidth(), accentThickness));
            graphics.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getBottom() - accentThickness,
                                                     bounds.getWidth(), accentThickness));
            graphics.fillRect(juce::Rectangle<float>(bounds.getX(), bounds.getY(), accentThickness, bounds.getHeight()));
            graphics.fillRect(juce::Rectangle<float>(bounds.getRight() - accentThickness, bounds.getY(),
                                                     accentThickness, bounds.getHeight()));
        }
        else
        {
            graphics.drawRect(getLocalBounds(), 1);
        }
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
        const auto font = makeUiFont();
        graphics.setFont(font);

        juce::StringArray promptLabels;
        juce::Array<juce::Image> promptIcons;

        if (longPressResetAction != nullptr)
        {
            promptLabels.add(longPressPrimaryPromptText);
            promptIcons.add(juce::Image());
        }
        if (longPressHostAction != nullptr)
        {
            static const auto hostPromptIcon = loadTablerIcon("map-pin-share", iconGlyphSize);
            promptLabels.add({});
            promptIcons.add(hostPromptIcon);
        }
        if (onMoveArmed != nullptr || onDragDrop != nullptr)
        {
            promptLabels.add("M?");
            promptIcons.add(juce::Image());
        }
        if (longPressTrailingAction != nullptr)
        {
            promptLabels.add(longPressTrailingPromptText);
            promptIcons.add(longPressTrailingPromptIconImage);
        }
        if (longPressAdditionalPromptAction != nullptr)
        {
            promptLabels.add({});
            promptIcons.add(longPressAdditionalPromptIconImage);
        }

        for (int index = 0; index < promptLabels.size(); ++index)
        {
            const auto isLastAction = index + 1 == promptLabels.size();
            const auto actionBounds = getActionPromptBounds(index);
            const auto promptHighlighted = actionPromptPressedIndex == index
                || (actionPromptPressedIndex < 0 && isMouseHovering(*this) && actionPromptHoverIndex == index);

            if (promptHighlighted)
            {
                graphics.setColour(uiGreyLight);
                graphics.fillRect(actionBounds);
            }

            graphics.setColour(promptHighlighted ? uiBlack : uiWhite);
            if (promptIcons[index].isValid())
            {
                juce::DrawableImage drawable;
                drawable.setImage(promptIcons[index]);
                drawable.setOverlayColour(promptHighlighted ? uiBlack : uiWhite);
                drawable.drawWithin(graphics,
                                    actionBounds.withSizeKeepingCentre(
                                        static_cast<int>(iconGlyphSize),
                                        static_cast<int>(iconGlyphSize)).toFloat(),
                                    juce::RectanglePlacement::centred, 1.0f);
            }
            else if (drawLoopingText(graphics,
                                     promptLabels[index],
                                     actionBounds.reduced(2, 0),
                                     font,
                                     juce::Justification::centred))
                scheduleMarqueeRepaint();

            if (! isLastAction)
            {
                graphics.setColour(uiGrey500);
                graphics.fillRect(actionBounds.getRight(), 1, 1, juce::jmax(0, getHeight() - 2));
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
