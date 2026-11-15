#pragma once

#include <JuceHeader.h>
#include <optional>

// One lifetime for cursor hiding across value, indicator and window drags.
class DragCursor final : private juce::Timer
{
public:
    ~DragCursor() override { end(); }
    void hold(const juce::MouseEvent& event, bool returnToStart = false)
    {
        if (!source.has_value())
        {
            source = event.source;
            restorePosition = returnToStart;
            startPosition = event.getMouseDownScreenPosition().toFloat();
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
    void end()
    {
        stopTimer();
        if (!source.has_value()) return;
        if (unboundedMovementEnabledByUs)
            source->enableUnboundedMouseMovement(false);
        unboundedMovementEnabledByUs = false;
        if (restorePosition) source->setScreenPosition(startPosition);
        restorePosition = false;
        if (component != nullptr) component->setMouseCursor(original);
        component = nullptr;
        source->revealCursor();
        source->forceMouseCursorUpdate();
        source.reset();
    }
private:
    void timerCallback() override
    {
        // A brief pause or loss of the OS drag flag must not warp the cursor
        // back to its mouse-down position while the button is still held.
        // The value gesture ends explicitly on mouseUp; this timer only
        // recovers from a missed mouseUp after all buttons are released.
        if (!source.has_value() || !source->getCurrentModifiers().isAnyMouseButtonDown()) end();
    }
    std::optional<juce::MouseInputSource> source;
    juce::Component::SafePointer<juce::Component> component;
    juce::MouseCursor original;
    juce::Point<float> startPosition;
    bool restorePosition = false;
    bool unboundedMovementEnabledByUs = false;
};
