#pragma once

#include "Controls.h"
#include <algorithm>
#include <cmath>

class RangeIndicator
{
public:
    static constexpr double stepsForFullRange = 20.0;
    static double move(double position, double steps, const juce::ModifierKeys& mods)
    {
        const auto step = (mods.isShiftDown() ? 0.1 : 1.0) / (2.0 * stepsForFullRange);
        return juce::jlimit(0.0, 1.0, position + steps * step);
    }
    static double rangePosition(juce::Slider& slider)
    {
        const auto range = slider.getNormalisableRange();
        if (range.end <= range.start) return 0.0;
        const auto value = juce::jlimit(range.start, range.end, slider.getValue());
        if (range.start > 0.0 && range.skew < 1.0 && !range.symmetricSkew)
            return juce::jlimit(0.0, 1.0, std::log(value / range.start) / std::log(range.end / range.start));
        return juce::jlimit(0.0, 1.0, slider.valueToProportionOfLength(value));
    }

    static constexpr int compactMarkerSize = 8;
    static constexpr int edgeInset = controlEdgeInset;
    static juce::Rectangle<int> boundsInside(const juce::Component&, juce::Rectangle<int> bounds)
    {
        return bounds.reduced(edgeInset);
    }
    static juce::Rectangle<int> potentiometerBounds(const juce::Component& component, juce::Rectangle<int> bounds)
    {
        const auto size = bottomControlSize(component, bounds);
        const auto text = getControlTextBounds(component, bounds);
        return { bounds.getX() + edgeInset, text.getY() - controlInnerGap - size,
                 juce::jmax(0, bounds.getWidth() - 2 * edgeInset), size };
    }
    static juce::Rectangle<float> continuousMarker(juce::Rectangle<int> bounds, double position)
    {
        // Keep the moving marker on the same integer-pixel grid as the rail.
        // Subpixel X positions produced antialiased edges during a drag.
        const auto travel = juce::jmax(0, bounds.getWidth() - bounds.getHeight());
        const auto x = bounds.getX() + juce::roundToInt(
            double(travel) * juce::jlimit(0.0, 1.0, position));
        return { static_cast<float>(x), static_cast<float>(bounds.getY()),
                 float(bounds.getHeight()), float(bounds.getHeight()) };
    }

    template <typename Coordinate>
    static void paintHover(juce::Graphics& g, const juce::Component& source,
                           const juce::Component& canvas, juce::Rectangle<Coordinate> bounds, bool enabled,
                           bool held = false, const char* region = "indicator")
    {
        if (bool(source.getProperties().getWithDefault("embeddedControlButton", false))) return;
        const bool hovering = enabled && isMouseHovering(source,
            source.getLocalArea(&canvas, bounds.toFloat()).getSmallestIntegerContainer());
        const auto ready = isHoverInversionReady(const_cast<juce::Component&>(source), hovering, region);
        if (!enabled || (!held && !ready)) return;
        g.setColour(held ? uiWhite : juce::Colour(0xff666666));
        g.fillRect(bounds);

    }

    static void paint(juce::Graphics& g, juce::Rectangle<int> bounds,
                      juce::Slider& slider, juce::Colour colour, double visualPosition = -1.0, const juce::Component* source = nullptr)
    {
        if (bounds.getWidth() < bounds.getHeight() || slider.getMaximum() <= slider.getMinimum()) return;
        const auto thickness = ControlAppearance::pixelThickness(g, 1.0f);
        const auto radius = float(defaultUiCornerRadius);
        const juce::Rectangle<float> rail(static_cast<float>(bounds.getX()),
            static_cast<float>(bounds.getY()) + float(bounds.getHeight()) * 0.5f - thickness * 0.5f,
            static_cast<float>(bounds.getWidth()), thickness);
        const auto marker = continuousMarker(bounds, visualPosition >= 0.0 ? visualPosition : rangePosition(slider));
        const bool manuallySelected = source != nullptr && isManualControlSelected(*source);
        if (manuallySelected)
        {
            g.setColour(uiWhite);
            g.fillRect(bounds.getX(), bounds.getY() + bounds.getHeight() / 2 - 1,
                       bounds.getWidth(), 2);
        }
        else
        {
            g.setColour(uiGreyLight);
            g.fillPath(makeUiCornerPath(rail, radius));
        }
        g.setColour(manuallySelected ? uiWhite : colour);
        g.fillPath(makeUiCornerPath(marker, radius));
    }

    static double discreteValueAt(juce::Slider& slider, double position, int count)
    {
        if (count <= 1) return slider.getMinimum();
        auto index = juce::roundToInt(position * double(count - 1));
        index = count > 2 ? (index % count + count) % count : juce::jlimit(0, count - 1, index);
        return slider.proportionOfLengthToValue(double(index) / double(count - 1));
    }

    static int bottomControlSize(const juce::Component& component, juce::Rectangle<int> bounds,
                                 int fallback = compactMarkerSize)
    {
        if (bounds.getHeight() <= getCompactControlSize(component) + 2) return fallback;
        return juce::jmax(1, (getSquareControlSize(component) - standardControlTextZoneHeight) / 2 - edgeInset - controlInnerGap);
    }

    static juce::Rectangle<int> bottomMarker(juce::Rectangle<int> bounds, int index, int size = compactMarkerSize)
    {
        return { index == 0 ? bounds.getX() : bounds.getRight() - size,
                 bounds.getBottom() - size, size, size };
    }

    static double valueAt(juce::Slider& slider, double position)
    {
        position = juce::jlimit(0.0, 1.0, position);
        const auto range = slider.getNormalisableRange();
        if (range.start > 0.0 && range.skew < 1.0 && !range.symmetricSkew)
        {
            if (position <= 0.0) return range.start;
            if (position >= 1.0) return range.end;
            return range.start * std::exp(position * std::log(range.end / range.start));
        }
        return slider.proportionOfLengthToValue(position);
    }

    static bool contains(const juce::MouseEvent& event, juce::Rectangle<int> bounds, juce::Slider& slider)
    {
        if (bounds.getWidth() < bounds.getHeight() || slider.getMaximum() <= slider.getMinimum()) return false;
        return continuousMarker(bounds, rangePosition(slider)).contains(event.getPosition().toFloat());
    }

    bool begin(const juce::MouseEvent& event, juce::Rectangle<int> bounds, juce::Slider& slider, int choices = 0)
    {
        if (!contains(event, bounds, slider) || !beginFromValue(event, bounds, slider, choices)) return false;
        return true;
    }

    bool beginFromValue(const juce::MouseEvent& event, juce::Rectangle<int> bounds, juce::Slider& slider, int choices = 0)
    {
        if (!event.mods.isLeftButtonDown()) return false;
        target = &slider;
        moved = false;
        discreteChoices = choices;
        startedOnMarker = contains(event, bounds, slider);
        position = rangePosition(slider);
        // Work in the original component's drag coordinate system. JUCE can
        // extend these coordinates beyond the screen while dragging; converting
        // them back to physical screen coordinates discards that continuity.
        lastDragDistance = 0.0;
        dragAxis = DragAxis::unlocked;
        thumbSize = bounds.getHeight();
        travel = juce::jmax(1, bounds.getWidth() - thumbSize);
        return true;
    }

    bool isActive() const { return target != nullptr; }
    bool wasDragged() const { return moved; }
    bool isDragging() const { return isActive() && moved; }
    bool startedDragOnMarker() const { return isDragging() && startedOnMarker; }
    // Continuous values are applied on every drag event; paint the slider's
    // actual (possibly quantised) value. Discrete choices are committed only
    // on release, so display the snapped choice during the drag as well.
    double visualPosition() const
    {
        if (!isDragging() || discreteChoices <= 1) return -1.0;
        auto index = juce::roundToInt(position * double(discreteChoices - 1));
        if (discreteChoices > 2) index = (index % discreteChoices + discreteChoices) % discreteChoices;
        return double(juce::jlimit(0, discreteChoices - 1, index)) / double(discreteChoices - 1);
    }
    void drag(const juce::MouseEvent& event)
    {
        if (target == nullptr || (!moved && !event.mouseWasDraggedSinceMouseDown())) return;

        // The mouse-down position is transformed along with this MouseEvent,
        // so this difference stays meaningful even during JUCE's unbounded
        // virtual-cursor movement on macOS (and on other platforms).
        const auto offset = event.position - event.mouseDownPosition;
        if (dragAxis == DragAxis::unlocked)
        {
            if (std::max(std::abs(offset.x), std::abs(offset.y)) < 3.0f)
                return;
            dragAxis = std::abs(offset.x) >= std::abs(offset.y)
                ? DragAxis::horizontal : DragAxis::vertical;
            // The initial movement only selects the drag axis. Applying it as
            // a value delta makes the thumb jump on the first drag event.
            lastDragDistance = dragAxis == DragAxis::horizontal
                ? double(offset.x) : -double(offset.y);
        }

        // Only one fixed axis contributes for the whole gesture. An absolute
        // displacement also avoids accumulating errors from intermediate mouse
        // events (including those generated when the cursor is hidden).
        const auto distance = dragAxis == DragAxis::horizontal
            ? double(offset.x) : -double(offset.y);
        const auto delta = distance - lastDragDistance;
        lastDragDistance = distance;

        if (!moved)
        {
            cursor.hold(event, true);
            moved = true;
            if (gesture == nullptr)
                gesture = std::make_unique<juce::Slider::ScopedDragNotification>(*target);
        }
        if (delta == 0.0) return;

        const auto increment = delta * (event.mods.isShiftDown() ? 0.1 : 1.0)
            / (4.0 * double(travel));
        if (discreteChoices > 1)
        {
            if (discreteChoices > 2)
            {
                const auto period = double(discreteChoices) / double(discreteChoices - 1);
                position = std::fmod(position + increment, period);
                if (position < 0.0) position += period;
            }
            else position = juce::jlimit(0.0, 1.0, position + increment);
        }
        else position = juce::jlimit(0.0, 1.0, position + increment);

        if (discreteChoices == 0)
            target->setValue(valueAt(*target, position), juce::sendNotificationSync);
    }

    void end(bool commit = false)
    {
        cursor.end();
        if (commit && discreteChoices > 0 && moved && target != nullptr)
            target->setValue(discreteValueAt(*target, position, discreteChoices), juce::sendNotificationSync);
        gesture.reset();
        target = nullptr;
        dragAxis = DragAxis::unlocked;
        startedOnMarker = false;
    }

private:
    enum class DragAxis { unlocked, horizontal, vertical };
    DragAxis dragAxis = DragAxis::unlocked;
    double lastDragDistance = 0.0;
    int thumbSize = compactMarkerSize;
    DragCursor cursor;
    juce::Component::SafePointer<juce::Slider> target;
    std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
    double position = 0.0;
    int travel = 1;
    bool moved = false;
    bool startedOnMarker = false;
    int discreteChoices = 0;
};

class ValueEditor final : public CopyPasteTextEditor, private juce::Slider::Listener
{
public:
    std::function<void()> onResetRequested;
    void setFrameVisible(bool visible) { frameVisible = visible; repaint(); }
    void paintOverChildren(juce::Graphics& g) override
    {
        if (frameVisible) CopyPasteTextEditor::paintOverChildren(g);
    }
    ~ValueEditor() override
    {
        if (target != nullptr) target->removeListener(this);
    }

    void paintIndicator(juce::Graphics& g, juce::Rectangle<int> bounds)
    {
        if (target == nullptr) return;
        const auto area = RangeIndicator::potentiometerBounds(*this, bounds);
        // Editing has an ordinary rail. The control's manual-selection state
        // remains saved and is drawn again when the value editor closes.
        RangeIndicator::paint(g, area, *target, uiIndicatorActive, indicator.visualPosition());
        const auto position = indicator.visualPosition() >= 0.0 ? indicator.visualPosition() : RangeIndicator::rangePosition(*target);
        if (const auto* parent = getParentComponent())
            RangeIndicator::paintHover(g, *this, *parent, RangeIndicator::continuousMarker(area, position), isEnabled() && !indicator.isDragging());
    }

    void refreshPresentationFor(juce::Slider& control)
    {
        if (target == &control) refreshNumericTextPreservingSelection();
    }

    void setNumericControl(juce::Slider* control,
                           std::function<double(const juce::String&)> parser = {},
                           std::function<juce::String()> formatter = {})
    {
        if (target != nullptr) target->removeListener(this);
        target = control;
        if (target != nullptr) target->addListener(this);
        parseText = std::move(parser);
        formatText = std::move(formatter);
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        CopyPasteTextEditor::mouseMove(event);
        if (auto* parent = getParentComponent()) parent->repaint();
    }
    void mouseExit(const juce::MouseEvent& event) override
    {
        CopyPasteTextEditor::mouseExit(event);
        if (auto* parent = getParentComponent()) parent->repaint();
    }

    bool beginIndicatorDrag(const juce::MouseEvent& event)
    {
        return target != nullptr && indicator.begin(event, RangeIndicator::potentiometerBounds(*this, getLocalBounds()), *target);
    }
    bool isIndicatorDragging() const { return indicator.isActive(); }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (beginIndicatorDrag(event)) return;
        if (event.mods.isLeftButtonDown() && event.mods.isCtrlDown() && event.mods.isShiftDown()
            && !event.mods.isAltDown() && onResetRequested != nullptr)
        {
            juce::Component::SafePointer<ValueEditor> safeThis(this);
            onResetRequested();
            if (safeThis != nullptr && (formatText != nullptr || target != nullptr))
            {
                refreshNumericTextPreservingSelection();
            }
            return;
        }
        CopyPasteTextEditor::mouseDown(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!indicator.isActive()) { CopyPasteTextEditor::mouseDrag(event); return; }
        juce::Component::SafePointer<ValueEditor> safeThis(this);
        indicator.drag(event);
        if (safeThis != nullptr && indicator.wasDragged())
        {
            refreshNumericTextPreservingSelection();
        }
    }
    void mouseUp(const juce::MouseEvent& event) override
    {
        if (indicator.isActive())
        {
            indicator.end(true);
            repaintValueOverlay(*this);
            return;
        }
        CopyPasteTextEditor::mouseUp(event);
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        juce::Component::mouseWheelMove(event, wheel);
    }

private:
    void refreshNumericTextPreservingSelection()
    {
        const auto next = formatText ? formatText() : target->getTextFromValue(target->getValue());
        if (next == getText()) return;
        const auto selection = getHighlightedRegion();
        const auto caret = getCaretPosition();
        setText(next, false);
        const auto length = getText().length();
        const auto start = juce::jlimit(0, length, selection.getStart());
        const auto end = juce::jlimit(start, length, selection.getEnd());
        if (selection.isEmpty()) setCaretPosition(juce::jlimit(0, length, caret));
        else
        {
            // Prime the opposite end so JUCE also restores selection direction.
            setCaretPosition(caret == selection.getStart() ? end : start);
            setHighlightedRegion({ start, end });
        }
    }

    void sliderValueChanged(juce::Slider*) override
    {
        if (auto* parent = getParentComponent()) parent->repaint(getBounds().expanded(0, 4));
        else repaint();
    }

    juce::Component::SafePointer<juce::Slider> target;
    std::function<double(const juce::String&)> parseText;
    std::function<juce::String()> formatText;
    RangeIndicator indicator;
    bool frameVisible = true;
};
