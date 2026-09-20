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

inline double dbToAmp(const double decibels)
{
    return std::pow(10.0, decibels / 20.0);
}

inline int wrapIndex(int index, const int size)
{
    jassert(size > 0);

    index %= size;

    if (index < 0)
        index += size;

    return index;
}

inline void clearOutputOnlyChannels(const juce::AudioProcessor& processor,
                                    juce::AudioBuffer<float>& buffer)
{
    for (auto channel = processor.getTotalNumInputChannels(); channel < processor.getTotalNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
}
}
