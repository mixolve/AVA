#pragma once

#include <JuceHeader.h>
#include <array>

namespace ava::channels
{
inline constexpr int count = 8;
inline constexpr int choiceCount = count + count / 2;

inline juce::StringArray choices()
{
    juce::StringArray result;
    for (int channel = 0; channel < count; channel += 2)
        result.add(juce::String(channel + 1) + "-" + juce::String(channel + 2));
    for (int channel = 0; channel < count; ++channel)
        result.add(juce::String(channel + 1));
    return result;
}

inline std::array<int, 2> decode(int choice) noexcept
{
    choice = juce::jlimit(0, choiceCount - 1, choice);
    if (choice < count / 2) return { choice * 2, choice * 2 + 1 };
    return { choice - count / 2, -1 };
}

inline bool isAvailable(const juce::AudioProcessor& processor, const int choice)
{
    if (!juce::isPositiveAndBelow(choice, choiceCount)) return false;
    const auto first = decode(choice)[0];
    return first / 2 < processor.getBusCount(true)
        && processor.getChannelCountOfBus(true, first / 2) > first % 2;
}
}
