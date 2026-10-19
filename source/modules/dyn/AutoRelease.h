#pragma once

#include <algorithm>

namespace dyn::detector
{
// History is driven by requested action, never by its own release tail.
// Time-domain callers tick once per sample, outside feedback solver trials.
struct ReleaseMemory
{
    double value = 0.0;
    void advance(bool active, double coefficient, double amount, double depth = 0.0, double strength = 1.0) noexcept
    {
        const auto target = active ? (depth > 0.0 ? 1.0 - depth + depth * std::clamp(strength, 0.0, 1.0) : 1.0) : 0.0;
        value = amount > 0.0 ? coefficient * value + (1.0 - coefficient) * target : 0.0;
    }
    double multiplier(double amount) const noexcept { return 1.0 + amount * value; }
};
}
