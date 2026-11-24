#pragma once

#include <algorithm>
#include <cmath>

namespace dyn_tension
{
// TENSION-HYST opens the FLOOR gate. A linear amplitude fade front-loads the
// perceived change (25% = only -12 dB below the full level). Remap the control
// onto a smooth logarithmic-like gain curve: approximately equal dB increments
// over equal control increments away from the exact mute point.
//
// This is a parameter mapping, not a detector, release, hold or delay. It keeps
// 0% = closed gate and 100% = fully open, as in the original implementation.
// Compute it on parameter changes, not inside the per-sample DSP loop.
template <typename T>
inline T scaleHysteresis(T position) noexcept
{
    const auto x = std::clamp(static_cast<double>(position), 0.0, 1.0);
    if (x <= 0.0) return T(0);
    if (x >= 1.0) return T(1);
    // 100:1 (~40 dB) normalized exponential, continuous at both endpoints.
    return static_cast<T>(std::expm1(std::log(100.0) * x) / 99.0);
}
// Parameter scaling only: preserve the original (exponent 43) curve at +/-100.
// At every fixed detector level, the dB gain/reduction is proportional to the
// magnitude of TENSION. This avoids using almost the entire effect by 25%.
template <typename T>
inline T shapePosition(T position, T amount, bool mirrored) noexcept
{
    const auto p = std::clamp(position, T(0), T(1));
    const auto blend = std::clamp(amount, T(0), T(1));
    const auto full = mirrored ? T(1) - std::pow(T(1) - p, T(43))
                               : std::pow(p, T(43));
    return p + blend * (full - p);
}

// The same transfer in either dB coordinates or signed correlation coordinates.
inline float shape(float value, float threshold, float floor, float tension) noexcept
{
    if (std::abs(tension) < 0.005f || value >= threshold)
        return value;
    floor = std::min(floor, threshold);
    const auto span = threshold - floor;
    if (span <= 1.0e-7f)
        return threshold;
    const auto position = std::clamp((value - floor) / span, 0.0f, 1.0f);
    const auto amount = std::abs(tension) * 0.01f;
    const auto curved = shapePosition(position, amount, tension < 0.0f);
    return floor + span * curved;
}

inline float correlation(float value, float threshold, float floor, float tension, float hysteresis) noexcept
{
    if (std::abs(tension) < 0.005f || value >= threshold)
        return value;
    floor = std::clamp(floor, -1.0f, threshold);
    const auto shaped = shape(value, threshold, floor, tension);
    return value <= floor ? -1.0f + (shaped + 1.0f) * std::clamp(hysteresis, 0.0f, 1.0f) : shaped;
}
}
