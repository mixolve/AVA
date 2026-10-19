#pragma once

#include <JuceHeader.h>

#include <cmath>

namespace ava::modules::parameters
{
inline juce::String formatFixedDecimalValue(const double value, const int decimalPlaces)
{
    const auto places = juce::jmax(0, decimalPlaces);
    const auto scale = std::pow(10.0, static_cast<double>(places));
    auto roundedValue = std::round(value * scale) / scale;

    if (std::abs(roundedValue) < (0.5 / scale))
        roundedValue = 0.0;

    return juce::String(roundedValue, places);
}
}
