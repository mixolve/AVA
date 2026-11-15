#pragma once
#include <JuceHeader.h>
#include <array>
namespace ava::modules::parameters {
inline constexpr std::array<double, 12> timeNoteWholeFractions {
    1.0 / 512, 1.0 / 256, 1.0 / 128, 1.0 / 64, 1.0 / 32, 1.0 / 16,
    1.0 / 8, 1.0 / 4, 1.0 / 2, 1.0, 2.0, 4.0
};
inline double noteDurationMilliseconds(int noteIndex, int mode, double tempoBpm) noexcept
{
    const auto index = size_t(juce::jlimit(0, int(timeNoteWholeFractions.size()) - 1, noteIndex));
    const auto factor = mode == 2 ? 2.0 / 3.0 : mode == 3 ? 1.5 : 1.0;
    return 240000.0 / juce::jlimit(1.0, 1000.0, tempoBpm) * timeNoteWholeFractions[index] * factor;
}
inline juce::StringArray getTimeNoteChoices()
{
    return { "1/512", "1/256", "1/128", "1/64", "1/32", "1/16",
             "1/8", "1/4", "1/2", "1/1", "2/1", "4/1" };
}
}
