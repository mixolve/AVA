#pragma once

#include <algorithm>
#include <cmath>

namespace dyn::ballistics
{
// Preserve the existing +/-100 shape; beyond it, extend the exponent
// continuously instead of clipping ATTACK CURVE or passing atanh a value > 1.
inline double curveExponent(const double amount)
{
    constexpr double maximumExponent = 4.605170185988091; // log(100).
    const auto magnitude = std::abs(amount);
    return magnitude <= 1.0
        ? 4.0 * std::atanh(magnitude * std::tanh(maximumExponent * 0.25))
        : maximumExponent * magnitude;
}

inline double shapedProgress(double progress, double curve)
{
    const auto amount = std::clamp(curve, -10.0, 10.0);
    if (amount == 0.0) return progress;
    // Equal midpoint deviation from LIN across the full curve range.
    const auto exponent = curveExponent(amount);
    const auto normalisation = std::expm1(exponent);
    return amount > 0.0
        ? 1.0 - std::expm1(exponent * (1.0 - progress)) / normalisation
        : std::expm1(exponent * progress) / normalisation;
}
// Preserve the chosen Attack Curve over the first 85% of the interval and
// finish its tail with a monotone cubic Hermite segment whose final tangent
// is horizontal. Unlike a whole-interval smoothstep, this changes only the
// end of ATTACK so the curve meets steady gain without a visible corner.
inline double shapedAttackToSustainProgress(const double progress, const double curve)
{
    const auto p = std::clamp(progress, 0.0, 1.0);
    constexpr double tailStart = 0.85;
    if (p <= tailStart)
        return shapedProgress(p, curve);

    const auto amount = std::clamp(curve, -10.0, 10.0);
    const auto valueAtStart = shapedProgress(tailStart, amount);
    double slopeAtStart = 1.0;
    if (amount != 0.0)
    {
        const auto exponent = curveExponent(amount);
        const auto norm = std::expm1(exponent);
        slopeAtStart = exponent * std::exp(exponent * (amount > 0.0
            ? 1.0 - tailStart : tailStart)) / norm;
    }

    constexpr double tailLength = 1.0 - tailStart;
    const auto u = (p - tailStart) / tailLength;
    const auto u2 = u * u;
    const auto u3 = u2 * u;
    const auto h00 = 2.0 * u3 - 3.0 * u2 + 1.0;
    const auto h10 = u3 - 2.0 * u2 + u;
    const auto h01 = -2.0 * u3 + 3.0 * u2;
    return std::clamp(h00 * valueAtStart + h10 * tailLength * slopeAtStart + h01,
        valueAtStart, 1.0);
}

}
