#pragma once

#include <algorithm>
#include <cmath>

namespace dyn::transfer
{
// Integrate the local output slope 1/R(x), rather than applying the ratio at x
// to the whole excursion. Positive amounts compound per span; negative amounts
// reduce R linearly until unity. This keeps the output monotonic, without expansion.
inline double hardReduction(double distance, double ratio, double amount, double span) noexcept
{
    const auto x = std::max(0.0, distance);
    if (std::isinf(ratio) && amount >= 0.0) return x;
    const auto r = std::isfinite(ratio) ? std::max(1.0, ratio) : 100.0;
    const auto p = std::clamp(amount, -1.0, 1.0);
    const auto s = std::max(1.0e-6, span);
    if (p == 0.0) return x * (1.0 - 1.0 / r);
    if (p > 0.0)
    {
        const auto k = std::log1p(p) / s;
        return std::max(0.0, x + std::expm1(-k * x) / (r * k));
    }
    const auto k = p / s;
    const auto active = std::min(x, (1.0 / r - 1.0) / k);
    const auto output = std::log1p(k * active) / (r * k);
    return std::max(0.0, active - output);
}

inline double kneeDistance(double distance, double knee) noexcept
{
    if (knee <= 0.0) return std::max(0.0, distance);
    if (distance <= -knee * 0.5) return 0.0;
    if (distance >= knee * 0.5) return distance;
    const auto position = distance + knee * 0.5;
    return position * position / (2.0 * knee);
}

inline double reduction(double distance, double ratio, double amount, double span, double knee) noexcept
{
    return hardReduction(kneeDistance(distance, knee), ratio, amount, span);
}

inline double reductionSlope(double distance, double ratio, double amount, double span, double knee) noexcept
{
    const auto x = kneeDistance(distance, knee);
    if (x <= 0.0) return 0.0;
    const auto kneeSlope = knee > 0.0 && distance < knee * 0.5 ? (distance + knee * 0.5) / knee : 1.0;
    if (std::isinf(ratio) && amount >= 0.0) return kneeSlope;
    const auto r = std::isfinite(ratio) ? std::max(1.0, ratio) : 100.0;
    const auto localRatio = amount >= 0.0
        ? r * std::exp(std::min(700.0, std::log1p(amount) * x / span))
        : std::max(1.0, r * (1.0 + amount * x / span));
    return (1.0 - 1.0 / localRatio) * kneeSlope;
}

// Invert a monotonic function using safeguarded Newton steps. A negative
// progression has a finite maximum reduction; callers only invert reachable gains.
inline double distanceForReduction(double target, double ratio, double amount, double span, double knee) noexcept
{
    double lower = -std::max(0.0, knee) * 0.5;
    double upper = std::max({span, knee, target * 2.0, 1.0});
    for (int i = 0; i < 20 && reduction(upper, ratio, amount, span, knee) < target; ++i) upper *= 2.0;
    auto x = (lower + upper) * 0.5;
    for (int i = 0; i < 60; ++i)
    {
        const auto error = reduction(x, ratio, amount, span, knee) - target;
        if (std::abs(error) < 1.0e-10) break;
        if (error > 0.0) upper = x; else lower = x;
        const auto slope = reductionSlope(x, ratio, amount, span, knee);
        const auto next = slope > 1.0e-12 ? x - error / slope : upper;
        x = next > lower && next < upper ? next : (lower + upper) * 0.5;
    }
    return x;
}

// Calibrate the feedback gain law against the same source-level curve as FWD:
// keyDistance = sourceDistance - feedback * reduction(sourceDistance).
inline double feedbackReduction(double keyDistance, double ratio, double amount, double span, double knee, double feedback) noexcept
{
    if (feedback <= 0.0) return reduction(keyDistance, ratio, amount, span, knee);
    double lower = keyDistance;
    double upper = std::max(keyDistance, 0.0) * std::max(1.0, ratio) + knee + span;
    for (int i = 0; i < 20 && upper - feedback * reduction(upper, ratio, amount, span, knee) < keyDistance; ++i) upper *= 2.0;
    auto x = lower;
    for (int i = 0; i < 60; ++i)
    {
        const auto value = reduction(x, ratio, amount, span, knee);
        const auto error = x - feedback * value - keyDistance;
        if (std::abs(error) < 1.0e-13) return value;
        if (error > 0.0) upper = x; else lower = x;
        const auto slope = std::max(1.0e-9, 1.0 - feedback * reductionSlope(x, ratio, amount, span, knee));
        const auto next = x - error / slope;
        x = next > lower && next < upper ? next : (lower + upper) * 0.5;
    }
    return reduction(x, ratio, amount, span, knee);
}
}
