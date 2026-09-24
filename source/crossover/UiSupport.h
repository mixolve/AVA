#pragma once

#include "Splitter.h"
#include "UiState.h"
#include "../shell/Controls.h"
#include "../shell/Style.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace crossover_ui
{
inline constexpr size_t splitControlCount = ava::crossover::Splitter::numSplits;

inline constexpr std::array<const char*, splitControlCount> splitParameterSuffixes {
    "split-1", "split-2", "split-3", "split-4", "split-5"
};

inline constexpr std::array<const char*, splitControlCount> splitLabels {
    "SPLIT-1", "SPLIT-2", "SPLIT-3", "SPLIT-4", "SPLIT-5"
};

inline constexpr std::array<const char*, 7> globalListenSuffixes {
    "lc", "rc", "mc", "sc", "ll", "rr", "ss"
};

inline constexpr std::array<const char*, 7> globalListenLabels {
    "LC", "RC", "MC", "SC", "LL", "RR", "SS"
};

inline constexpr float minSplitFrequencyGapHz = 1.0f;

inline std::unique_ptr<BoxTextButton> makeTextButton(const juce::String& text,
                                                     const juce::Colour accent = uiAccent)
{
    auto button = std::make_unique<BoxTextButton>(accent);
    button->setButtonText(text);
    button->setTextJustification(juce::Justification::centred);
    button->setCancelClickOnLeave(true);
    return button;
}

inline std::unique_ptr<BoxTextButton> makeTimeModeButton()
{
    return makeTextButton("M", uiGrey500);
}

inline bool getBool(const juce::ValueTree& state,
                    const juce::String& moduleKey,
                    const juce::String& property,
                    const bool defaultValue)
{
    const auto id = makeStatePropertyId(moduleKey, property);
    return state.hasProperty(id) ? static_cast<bool>(state.getProperty(id)) : defaultValue;
}

inline int getInt(const juce::ValueTree& state,
                  const juce::String& moduleKey,
                  const juce::String& property,
                  const int defaultValue)
{
    const auto id = makeStatePropertyId(moduleKey, property);
    return state.hasProperty(id) ? static_cast<int>(state.getProperty(id)) : defaultValue;
}

inline void setBool(juce::ValueTree& state,
                    const juce::String& moduleKey,
                    const juce::String& property,
                    const bool value)
{
    state.setProperty(makeStatePropertyId(moduleKey, property), value, nullptr);
}

inline void setInt(juce::ValueTree& state,
                   const juce::String& moduleKey,
                   const juce::String& property,
                   const int value)
{
    state.setProperty(makeStatePropertyId(moduleKey, property), value, nullptr);
}

inline float readRawParameter(juce::AudioProcessorValueTreeState& state,
                              const juce::String& parameterId,
                              const float fallback) noexcept
{
    if (auto* value = state.getRawParameterValue(parameterId))
        return value->load(std::memory_order_relaxed);

    return fallback;
}

inline juce::String getOrthogonalPositionDescription(const float degreeValue, const bool flipRight)
{
    auto degree = std::fmod(static_cast<double>(degreeValue), 360.0);

    if (degree < 0.0)
        degree += 360.0;

    const auto nearestKey = static_cast<int>(std::round(degree / 45.0)) % 8;
    const auto nearestDegree = static_cast<double>(nearestKey) * 45.0;
    auto distance = std::abs(degree - nearestDegree);
    distance = juce::jmin(distance, 360.0 - distance);

    if (distance > 0.05)
        return "CUSTOM";

    static constexpr std::array<const char*, 8> rotationLabels {
        "NORMAL",
        "L > SIDE & R > MID",
        "L > -R & R > L",
        "L > -MID & R > SIDE",
        "BOTH FLIPPED",
        "L > -SIDE & R > -MID",
        "L > R & R > -L",
        "L > MID & R > -SIDE"
    };

    static constexpr std::array<const char*, 8> flipLabels {
        "RIGHT FLIPPED",
        "L > MID & R > SIDE",
        "L & R SWAPPED",
        "L > -SIDE & R > MID",
        "LEFT FLIPPED",
        "L > -MID & R > -SIDE",
        "SWAPPED FLIPPED",
        "L > SIDE & R > -MID"
    };

    return flipRight ? flipLabels[static_cast<size_t>(nearestKey)]
                     : rotationLabels[static_cast<size_t>(nearestKey)];
}
}
