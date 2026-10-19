#pragma once

#include <algorithm>
#include <cmath>

namespace dyn_spectral_tension
{
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
    const auto exponent = 1.0f + 42.0f * amount * amount;
    const auto curved = tension < 0.0f ? 1.0f - std::pow(1.0f - position, exponent) : std::pow(position, exponent);
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
