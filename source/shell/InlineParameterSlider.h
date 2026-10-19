#pragma once
#include "Style.h"
#include "DisclosureAnimation.h"
#include "HoverAnimation.h"
#include "PetalGeometry.h"
#include <optional>

class InlineParameterSlider final : public juce::Component,
                                    private juce::Timer,
                                    private juce::Slider::Listener
{
public:
    static constexpr int petalWidth = 10;
    static constexpr int gripWidth = 8 + 8 + 8;
    static constexpr int gripHeight = 6;
    static constexpr int gripBevel = 8;
    static constexpr double dragPixelsForFullRange = 100.0;
    static constexpr double collapseDelayMs = 2000.0;

    explicit InlineParameterSlider(juce::Slider& targetIn) : target(targetIn)
    {
        setName("inline-parameter-slider");
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        target.addListener(this);
        reveal.onUpdate = [this] { repaintField(); };
    }
    ~InlineParameterSlider() override
    {
        stopTimer();
        endDrag();
        target.removeListener(this);
    }

    void setAvailable(bool available)
    {
        if (!available)
        {
            endDrag();
            pointerOverPetals=false;
            openingPress = false;
            stopTimer();
            reveal.snap(false);
            outsideSince = 0.0;
        }
        setVisible(available);
    }
    bool isExpanded() const noexcept { return reveal.targetExpanded(); }
    float getRevealOpacity() const noexcept { return static_cast<float>(reveal.value()); }
    juce::Rectangle<int> getPetalBounds() const
    { return getLocalBounds().withWidth(juce::jmin(petalWidth, getWidth())); }
    juce::Rectangle<int> getRightPetalBounds() const
    { return getLocalBounds().withLeft(juce::jmax(0, getWidth() - petalWidth)); }
    juce::Rectangle<int> getRevealBounds() const { return getLocalBounds(); }
    bool hasReveal() const noexcept { return reveal.value() > 0.0; }
    juce::Path getSidePetalPath(bool right,bool closed=true) const
    {
        return makeSidePetal(right?getRightPetalBounds():getPetalBounds(),right,closed,float(gripHeight),float(gripBevel),0.0f);
    }
    juce::Rectangle<int> getTrackBounds() const
    {
        // The shared diagonal starts at the field edge: no horizontal ledge
        // before the side cut and no inset before the moving grip's base.
        return getLocalBounds().reduced(0,1);
    }
    juce::Rectangle<int> getGripBounds() const
    {
        const auto track=getTrackBounds();
        const auto width=juce::jmin(gripWidth,track.getWidth());
        const auto x=track.getX()+juce::roundToInt((track.getWidth()-width)*proportion());
        return {x,track.getY(),width,track.getHeight()};
    }
    juce::RectangleList<int> getGripAreas() const
    {
        const auto grip = getGripBounds();
        const auto height = juce::jmin(gripHeight, grip.getHeight() / 2);
        juce::RectangleList<int> areas;
        areas.add(grip.withHeight(height));
        areas.add(grip.withTop(grip.getBottom() - height));
        return areas;
    }
    juce::Path getGripPath() const
    {
        const auto grip = getGripBounds();
        const auto height = juce::jmin(gripHeight, grip.getHeight() / 2);
        auto path = makeEndPetal(grip.withHeight(height),false,float(gripBevel));
        path.addPath(makeEndPetal(grip.withTop(grip.getBottom() - height),true,float(gripBevel)));
        return path;
    }
    bool hitTest(int x, int y) override
    { return (isExpanded() || reveal.isRunning()) ? getLocalBounds().contains(x, y) : (getSidePetalPath(false).contains(float(x),float(y)) || getSidePetalPath(true).contains(float(x),float(y))); }

    // Input/time are explicit so the idle policy also works without a native
    // peer (export/offscreen UI tests); the live timer supplies the real hit test.
    void updateInactivity(bool pointerInside, double now)
    {
        if (pointerInside || gesture != nullptr || openingPress) { outsideSince = 0.0; return; }
        if (outsideSince == 0.0) outsideSince = now;
        if (now - outsideSince >= collapseDelayMs)
        {
            reveal.setExpanded(false);
            stopTimer();
        }
    }

    std::function<bool()> canOpen;

    void paint(juce::Graphics& g) override
    {
        const auto hover=petalHover.amount(pointerOverPetals);
        const auto editing=isExpanded() || hasReveal();
        const auto paintPetal = [this,&g,hover,editing](bool right)
        {
            const auto petal=getSidePetalPath(right);
            ControlAppearance style(false,hover,editing);
            style.normalFill=uiGreyLight;
            style.paint(g,petal);
        };
        if(hasReveal()) {
            g.setColour(uiGreyDark.withMultipliedAlpha(getRevealOpacity()));
            g.fillRect(getLocalBounds().reduced(1));
        }
        paintPetal(false);
        paintPetal(true);
        if(hasReveal()) {
            g.setColour(uiWhite.withMultipliedAlpha(getRevealOpacity()));
            g.fillPath(getGripPath());
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (!event.mods.isLeftButtonDown()) return;
        outsideSince = 0.0;
        const auto onGrip=isExpanded() && getGripPath().contains(event.position);
        if (!onGrip && (getSidePetalPath(true).contains(event.position)
            || getSidePetalPath(false).contains(event.position)))
        {
            if (!isExpanded() && canOpen && !canOpen()) return;
            openingPress = true;
            reveal.setExpanded(!isExpanded());
            if (isExpanded()) startTimerHz(30);
            else stopTimer();
            repaint();
            return;
        }
        if (!onGrip) return;
        openingPress = false;
        pointerOverPetals=false;
        wheelRemainder = 0.0;
        dragProportion = proportion();
        lastPosition = event.getScreenPosition();
        gesture = std::make_unique<juce::Slider::ScopedDragNotification>(target);
        startCursorTracking(event);
    }
    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (openingPress || gesture == nullptr) return;
        const auto position = event.getScreenPosition();
        const auto movement = position - lastPosition;
        lastPosition = position;
        dragProportion = juce::jlimit(0.0, 1.0, dragProportion
            + double(movement.x - movement.y) / dragPixelsForFullRange
                * (event.mods.isShiftDown() ? 0.1 : 1.0));
        outsideSince = 0.0;
        target.setValue(valueFromProportion(dragProportion), juce::sendNotificationSync);
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        openingPress = false;
        endDrag();
        outsideSince = 0.0;
    }
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        if (!isExpanded()) { juce::Component::mouseWheelMove(event, wheel); return; }
        if (wheel.isInertial) return;
        const auto delta = std::abs(wheel.deltaY) >= std::abs(wheel.deltaX) ? wheel.deltaY : -wheel.deltaX;
        if (std::abs(delta) < 1e-6f) return;
        const auto direction = wheel.isReversed ? -delta : delta;
        const auto movement = direction > 0 ? 1.0 : -1.0;
        if (gesture != nullptr) return;
        outsideSince = 0.0;
        gesture = std::make_unique<juce::Slider::ScopedDragNotification>(target);
        const auto safeThis = juce::Component::SafePointer<InlineParameterSlider>(this);
        if (!juce::approximatelyEqual(lastWheelValue, target.getValue())) wheelRemainder = 0.0;
        const auto desired = juce::jlimit(0.0, 1.0, proportion() + wheelRemainder
            + movement * (event.mods.isShiftDown() ? 0.001 : 0.01));
        target.setValue(valueFromProportion(desired), juce::sendNotificationSync);
        if (safeThis != nullptr)
        {
            // Preserve sub-step movement for integer/stepped parameters.
            safeThis->wheelRemainder = desired - safeThis->proportion();
            safeThis->lastWheelValue = safeThis->target.getValue();
            safeThis->endDrag();
        }
    }
    void mouseEnter(const juce::MouseEvent& event) override
    { outsideSince = 0.0; updatePetalHover(event.position); repaintField(); }
    void mouseMove(const juce::MouseEvent& event) override { updatePetalHover(event.position); }
    void mouseExit(const juce::MouseEvent&) override
    {
        pointerOverPetals=false;
        outsideSince = 0.0;
        repaintField();
    }
    void visibilityChanged() override
    {
        if (!isShowing())
        {
            pointerOverPetals=false;
            endDrag();
            reveal.snap(false);
            stopTimer();
        }
    }
private:
    juce::Point<float> cursorAnchor() const
    {
        const auto grip = getGripBounds();
        const auto height = juce::jmin(gripHeight, grip.getHeight() / 2);
        return juce::Point<float>(float(grip.getX()),
            float(lowerGripCursor ? grip.getBottom() - height : grip.getY())) + cursorOffset;
    }
    void startCursorTracking(const juce::MouseEvent& event)
    {
        lowerGripCursor = event.position.y >= static_cast<float>(getHeight()) * 0.5f;
        cursorOffset = {};
        cursorOffset = event.position - cursorAnchor();
        setMouseCursor(juce::MouseCursor::NoCursor);
        // Only real native drags change system mouse state. Unbounded input
        // keeps the cursor hidden even when the physical pointer leaves the field.
        if (getPeer() != nullptr && event.source.isDragging() && event.source.canDoUnboundedMovement())
        {
            cursorSource = event.source;
            cursorSource->enableUnboundedMouseMovement(true, false);
        }
    }
    void endDrag()
    {
        if (cursorSource.has_value())
        {
            const auto anchor = localPointToGlobal(cursorAnchor());
            cursorSource->enableUnboundedMouseMovement(false);
            if (isShowing() && getPeer() != nullptr) cursorSource->setScreenPosition(anchor);
            cursorSource.reset();
        }
        setMouseCursor(juce::MouseCursor::NormalCursor);
        gesture.reset();
    }
    void updatePetalHover(juce::Point<float> position)
    {
        pointerOverPetals=getSidePetalPath(false).contains(position)
            || getSidePetalPath(true).contains(position);
        repaint();
    }
    void repaintField()
    {
        if (auto* field = getParentComponent()) field->repaint();
        else repaint();
    }
    bool usesLogarithmicScale() const
    {
        const auto range = target.getNormalisableRange();
        return range.start > 0.0 && range.end > range.start && range.skew < 1.0
            && !range.symmetricSkew;
    }
    double proportion() const
    {
        // A power skew can map several pixels at the low end to less than one
        // legal parameter step (e.g. EQL FREQ 20..30000Hz, interval 0.01Hz).
        // Positive skewed controls use a log UI scale; host parameter mapping
        // and quantisation stay on the original slider/attachment.
        if (usesLogarithmicScale())
        {
            const auto value = juce::jlimit(target.getMinimum(), target.getMaximum(), target.getValue());
            const auto position = std::log(value / target.getMinimum())
                / std::log(target.getMaximum() / target.getMinimum());
            return juce::jlimit(0.0, 1.0, position);
        }
        return juce::jlimit(0.0, 1.0, target.valueToProportionOfLength(target.getValue()));
    }
    double valueFromProportion(double position) const
    {
        position = juce::jlimit(0.0, 1.0, position);
        if (usesLogarithmicScale())
        {
            if (position <= 0.0) return target.getMinimum();
            if (position >= 1.0) return target.getMaximum();
            return target.getMinimum() * std::exp(position
                * std::log(target.getMaximum() / target.getMinimum()));
        }
        return target.proportionOfLengthToValue(position);
    }
    void sliderValueChanged(juce::Slider*) override { repaint(); }
    void timerCallback() override
    {
        // Check the entire value field, including children, rather than a stale
        // mouse-exit event when the field scrolls or another control replaces it.
        if (getPeer() != nullptr && !isShowing())
        {
            endDrag();
            reveal.snap(false);
            stopTimer();
            return;
        }
        const auto inside = reallyContains(getLocalPoint(nullptr, juce::Desktop::getMousePosition()), true);
        updateInactivity(inside, juce::Time::getMillisecondCounterHiRes());
    }
    juce::Slider& target;
    DisclosureAnimation reveal;
    HoverAnimation petalHover { *this };
    bool pointerOverPetals=false;
    std::unique_ptr<juce::Slider::ScopedDragNotification> gesture;
    std::optional<juce::MouseInputSource> cursorSource;
    juce::Point<float> cursorOffset;
    bool lowerGripCursor = false;
    juce::Point<int> lastPosition;
    double dragProportion = 0.0, outsideSince = 0.0;
    double wheelRemainder = 0.0, lastWheelValue = 0.0;
    bool openingPress = false;
};
