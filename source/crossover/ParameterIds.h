#pragma once

#include <JuceHeader.h>

#include <cstddef>

namespace ava::crossover::parameters
{
inline juce::String makeRangeParameterId(const size_t rangeIndex, const char* suffix)
{
    return "range_" + juce::String(static_cast<int>(rangeIndex + 1)) + "_" + suffix;
}

inline juce::String makeRangeGroupId(const size_t rangeIndex)
{
    return "range_" + juce::String(static_cast<int>(rangeIndex + 1));
}

inline juce::String makeRangeGroupName(const size_t rangeIndex)
{
    return "Range " + juce::String(static_cast<int>(rangeIndex + 1));
}
}
