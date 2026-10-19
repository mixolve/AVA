#pragma once

namespace dyn::detector
{
// Positive distance is on the triggering side of the threshold. The action is
// retained across the return band until distance reaches -width.
struct ThresholdHysteresis
{
    bool active = false;
    double retainedTarget = 0.0;

    bool holds(double distance, double width) noexcept
    {
        if (width <= 0.0)
        {
            active = false;
            return false;
        }
        if (distance >= 0.0)
        {
            active = true;
            return false;
        }
        if (distance <= -width) active = false;
        return active;
    }

    template <typename T>
    T process(T requested, double distance, double width) noexcept
    {
        if (holds(distance, width)) return static_cast<T>(retainedTarget);
        retainedTarget = static_cast<double>(requested);
        return requested;
    }
};
}
