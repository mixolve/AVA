#pragma once

#include <JuceHeader.h>

#include <cmath>

namespace ava::modules::dsp
{
inline constexpr double epsilon = 1.0e-9;

inline double roundToParameterStep(const double value)
{
    return std::floor((value * 100.0) + 0.5) * 0.01;
}

inline constexpr double gainMinimumDb = -100.0, gainMaximumDb = 96.0, gainMuteThresholdDb = -96.0;
inline bool isGainMuted(double gainDb) noexcept { return gainDb < gainMuteThresholdDb; }

inline double dbToAmp(const double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

inline double mutedGainToAmp(double gainDb, bool muted) noexcept
{
    return muted || isGainMuted(gainDb) ? 0.0 : dbToAmp(gainDb);
}

inline int wrapIndex(int index, const int size)
{
    jassert(size > 0);

    index %= size;

    if (index < 0)
        index += size;

    return index;
}

}
