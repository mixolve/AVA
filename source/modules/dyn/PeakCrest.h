#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace dyn::detector
{
inline constexpr std::size_t peak = 0, rms = 1, count = 2;
// A threshold difference is meaningful only while both detectors contribute.
template <typename T>
inline T effectiveCrest(T crest, T rmsAmount) noexcept
{
    return rmsAmount > T(0) && rmsAmount < T(1) ? crest : T(0);
}
inline double peakScale(double crestDb) noexcept { return std::pow(10.0, -crestDb * 0.05); }
template <typename T>
inline T mix(T peakValue, T rmsValue, T rmsAmount) noexcept
{
    if (rmsAmount <= T(0)) return peakValue;
    if (rmsAmount >= T(1)) return rmsValue;
    return peakValue + (rmsValue - peakValue) * rmsAmount;
}
// A positive crest makes the instantaneous path less sensitive in either scale.
inline float peakCorrelationThreshold(float correlation, float crest) noexcept
{
    return std::clamp(correlation - crest, -1.0f, 1.0f);
}
}
