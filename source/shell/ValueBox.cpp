#include "Controls.h"
#include "ValueEditor.h"
#include "Prompt.h"
#include "Editor.h"
#include "ChoiceControl.h"
#include "ParameterControl.h"

namespace
{
// MODULES embeds value controls inside a scrolling page. Drawing its moving
// thumb in the outer editor overlay introduces an extra buffered repaint.
bool paintsRangeLocally(const juce::Component& component)
{
    for (auto* parent = component.getParentComponent(); parent != nullptr; parent = parent->getParentComponent())
        if (bool(parent->getProperties().getWithDefault("paintRangeLocally", false)))
            return true;
    return false;
}
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
        pointerDown = false;
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
        pointerDown = false;
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
        && (pointerOnValue() || momentaryPressed || directIndicator->isDragging() || textInputActive);
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
    const bool moved = directIndicator->wasDragged();
    directIndicator->end(true);
    if (valueVisible && !pointerOnValue()) valueTransitionAt = juce::Time::getMillisecondCounterHiRes() + valueTransitionDelayMs;
    if (moved) repaintValueOverlay(*this);
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
    if (paintsRangeLocally(*this) && areRangeIndicatorsVisible()
        && (numericControlEnabled || choiceCount() > 0) && !textInputActive)
    {
        const auto rail = RangeIndicator::potentiometerBounds(*this, getValueBounds());
        const auto visual = directIndicator->visualPosition();
        const auto position = visual >= 0.0 ? visual : RangeIndicator::rangePosition(slider);
        const auto marker = RangeIndicator::continuousMarker(rail, position);
        const bool enabled = interactionEnabled && isEnabled();
        const bool selected = isManualControlSelected(*this);
        RangeIndicator::paint(g, rail, slider, enabled ? uiIndicatorActive : uiIndicatorDisabled, visual, this);
        const bool interacting = directIndicator->isDragging();
        const bool hovered = enabled && (interacting ? directIndicator->startedDragOnMarker()
            : isMouseHovering(*this, marker.getSmallestIntegerContainer()));
        if (selected)
        {
            if (isHoverInversionReady(*this, hovered, "value.top"))
            {
                g.setColour(juce::Colour(0xff666666));
                g.fillRect(marker);
                g.setColour(uiWhite);
                g.drawRect(marker, 1.0f);
            }
        }
        else if (interacting)
        {
            if (isHoverInversionReady(*this, hovered, "value.top"))
            {
                g.setColour(juce::Colour(0xff666666));
                g.fillRect(marker);
            }
        }
        else
            RangeIndicator::paintHover(g, *this, *this, marker, enabled, false, "value.top");
    }
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
    paintControlTextButton(g, textBounds, interactionEnabled && isEnabled() && (valueVisible || dragTargetHighlighted), textInputActive || (interactionEnabled && isEnabled() && compactOn), arrangementDragging || (interactionEnabled && isEnabled() && momentaryPressed && momentaryPressFeedbackEnabled), interactionEnabled && isEnabled() ? juce::Colour(juce::uint32(juce::int64(base))) : uiIndicatorDisabled);
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

void ValueBoxComponent::paintRangeIndicators(juce::Graphics& g, juce::Component& root,
    const std::function<bool(const juce::Component&)>& include)
{
    const juce::Graphics::ScopedSaveState restoreClip(g);
    // Resolve the control under the pointer once for the whole overlay. Testing
    // every marker separately repeats the component-tree hit test during scroll.
    ValueBoxComponent* hoveredControl = nullptr;
    juce::Point<float> pointerPosition;
    const auto& desktop = juce::Desktop::getInstance();
    for (const auto& source : desktop.getMouseSources())
    {
        if (!source.isMouse()) continue;
        const auto point = root.getLocalPoint(nullptr, source.getScreenPosition());
        // Preserve the pressed control's hover across a gesture, rather than
        // dropping it while JUCE reports a dragging mouse source.
        // JUCE reports isDragging() as soon as a mouse button goes down,
        // even if the pointer has not moved. Keep normal hit-testing on a click.
        auto* target = source.isDragging() && source.hasMovedSignificantlySincePressed()
            ? source.getComponentUnderMouse() : root.getComponentAt(point.roundToInt());
        if (target == nullptr || (target != &root && !root.isParentOf(target))) continue;
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
            const auto visual = directVisual;
            const auto position = visual >= 0.0 ? visual : RangeIndicator::rangePosition(value->slider);
            // The marker is pixel-aligned. Reusing its cached image until that
            // pixel changes avoids reallocating an image for every tiny drag step.
            const auto markerPixel = RangeIndicator::continuousMarker(
                RangeIndicator::potentiometerBounds(*value, value->getValueBounds()), position).getX();
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
            const auto key = juce::String(value->getWidth()) + ":" + juce::String(value->getHeight())
                + ":" + juce::String(scale, 6) + ":" + juce::String(defaultUiCornerRadius)
                + ":" + juce::String(enabled ? 1 : 0) + ":" + juce::String(count)
                + ":" + juce::String(markerPixel)
                + ":" + juce::String((value->numericControlEnabled || count > 0) && !value->textInputActive ? 1 : 0)
                + ":" + juce::String(bottomStates[0]) + ":" + juce::String(bottomStates[1])
                + ":" + dockNumber
                + ":" + juce::String(rangeVisible ? 1 : 0)
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
                if (!paintsRangeLocally(*value) && rangeVisible
                    && (value->numericControlEnabled || count > 0) && !value->textInputActive)
                    RangeIndicator::paint(ink, RangeIndicator::potentiometerBounds(*value, value->getValueBounds()), value->slider,
                        enabled ? uiIndicatorActive : disabledColour, visual, value);
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
            if (!paintsRangeLocally(*value) && rangeVisible
                && (value->numericControlEnabled || count > 0) && !value->textInputActive)
            {
                const auto marker = RangeIndicator::continuousMarker(
                    RangeIndicator::potentiometerBounds(*value, bounds),
                    visual >= 0.0 ? visual : RangeIndicator::rangePosition(value->slider));
                const bool interacting = value->directIndicator->isDragging();
                const bool enabledHover = value->interactionEnabled && value->isEnabled();
                // The pointer is hidden while dragging. Keep the thumb's
                // existing hover instead of clearing it and restarting the
                // delayed hover after mouseUp.
                const bool markerHovered = enabledHover && (interacting
                    ? value->directIndicator->startedDragOnMarker()
                    : value == hoveredControl && marker.contains(pointerPosition));
                if (manuallySelected)
                {
                    // Selection is white at rest, but must still respond to
                    // hovering the thumb.  Use a dark centre with a WHITE
                    // outline, so the control remains visibly Ctrl-marked.
                    if (isHoverInversionReady(*value, markerHovered, "value.top"))
                    {
                        g.setColour(juce::Colour(0xff666666));
                        g.fillRect(marker);
                        g.setColour(uiWhite);
                        g.drawRect(marker, 1.0f);
                    }
                }
                else if (interacting)
                {
                    if (isHoverInversionReady(*value, markerHovered, "value.top"))
                    {
                        g.setColour(juce::Colour(0xff666666));
                        g.fillRect(marker);
                    }
                }
                else
                {
                    paintHover(*value, "value.top", marker, enabledHover);
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
