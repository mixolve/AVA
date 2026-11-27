#pragma once

#include <JuceHeader.h>
#include <optional>

#if JUCE_MAC
namespace drag_cursor_detail
{
void hideSystemCursor();
void showSystemCursor();
}
#endif

// One lifetime for cursor hiding across value, indicator and window drags.
class DragCursor final : private juce::Timer
{
public:
    ~DragCursor() override { end(); finishReveal(); }
    void hold(const juce::MouseEvent& event, bool returnToStart = false, bool hideSystemCursor = false)
    {
        if (revealPending) finishReveal();
        if (!source.has_value())
        {
            source = event.source;
           #if JUCE_MAC
            if (hideSystemCursor && source->isMouse())
            {
                drag_cursor_detail::hideSystemCursor();
                systemCursorHidden = true;
            }
           #else
            juce::ignoreUnused(hideSystemCursor);
           #endif
            restorePosition = returnToStart;
            returnPosition = event.getMouseDownScreenPosition().toFloat();
            // JUCE supplies continuous virtual drag coordinates when unbounded
            // movement is enabled. Without it, a physical screen-edge or a
            // macOS cursor warp can cause a discontinuity in slider values.
            // Window dragging (returnToStart == false) remains bounded.
            unboundedMovementEnabledByUs = returnToStart
                && source->canDoUnboundedMovement()
                && !source->isUnboundedMouseMovementEnabled();
            if (unboundedMovementEnabledByUs)
                source->enableUnboundedMouseMovement(true);
            component = event.eventComponent;
            if (component != nullptr)
            {
                original = component->getMouseCursor();
                component->setMouseCursor(juce::MouseCursor::NoCursor);
            }
        }
        source->hideCursor();
        if (!isTimerRunning()) startTimer(50);
    }
    bool isHeld() const noexcept { return source.has_value() && !revealPending; }

    void setReturnPosition(juce::Point<float> position)
    {
        returnPosition = position;
        deferReveal = true;
    }

    void end()
    {
        if (!source.has_value() || revealPending) return;
        stopTimer();
        if (unboundedMovementEnabledByUs)
            source->enableUnboundedMouseMovement(false);
        unboundedMovementEnabledByUs = false;
        if (restorePosition) source->setScreenPosition(returnPosition);
        restorePosition = false;
        if (deferReveal)
        {
            // JUCE can still update the peer cursor and the pointer position in
            // the remainder of mouseUp. Keep NoCursor until that stack unwinds.
            source->hideCursor();
            revealPending = true;
            revealDeadline = juce::Time::getMillisecondCounterHiRes() + 40.0;
            startTimer(10);
            return;
        }
        finishReveal();
    }
private:
    void finishReveal()
    {
        stopTimer();
        if (!source.has_value()) return;
        revealPending = deferReveal = false;
        if (component != nullptr) component->setMouseCursor(original);
        component = nullptr;
        source->revealCursor();
        source->forceMouseCursorUpdate();
       #if JUCE_MAC
        if (systemCursorHidden)
        {
            drag_cursor_detail::showSystemCursor();
            systemCursorHidden = false;
        }
       #endif
        source.reset();
    }

    void timerCallback() override
    {
        if (revealPending)
        {
            if (!source->getCurrentModifiers().isAnyMouseButtonDown()
                && source->getScreenPosition().getDistanceFrom(returnPosition) > 1.5f
                && juce::Time::getMillisecondCounterHiRes() < revealDeadline)
            {
                source->setScreenPosition(returnPosition);
                return;
            }
            finishReveal();
            return;
        }
        // A brief pause or loss of the OS drag flag must not warp the cursor
        // back to its mouse-down position while the button is still held.
        // The value gesture ends explicitly on mouseUp; this timer only
        // recovers from a missed mouseUp after all buttons are released.
        if (!source.has_value() || !source->getCurrentModifiers().isAnyMouseButtonDown()) end();
    }
    std::optional<juce::MouseInputSource> source;
    juce::Component::SafePointer<juce::Component> component;
    juce::MouseCursor original;
    juce::Point<float> returnPosition;
    bool restorePosition = false;
    bool deferReveal = false;
    bool revealPending = false;
    double revealDeadline = 0.0;
   #if JUCE_MAC
    bool systemCursorHidden = false;
   #endif
    bool unboundedMovementEnabledByUs = false;
};
