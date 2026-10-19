#pragma once

#include "AnimationSettings.h"
#include <JuceHeader.h>
#include <optional>

struct AnimationField
{
    const char* label;
    const char* property;
    std::atomic<float> AnimationModeSettings::* value;
    double minimum, maximum, initial, interval;
    int decimals;
    bool burningFilm, topographic;

    bool supports(int mode) const noexcept { return mode >= 3 && mode <= animationModeCount ? value == &AnimationModeSettings::speed
        : mode == 1 ? burningFilm : mode == 2 && topographic; }
    double lowerBound(int mode) const noexcept { return value == &AnimationModeSettings::speed && mode >= 3 ? 0.2 : minimum; }
    double upperBound(int mode) const noexcept { return value == &AnimationModeSettings::speed ? (mode >= 3 ? 3.0 : mode == 1 ? 8.0 : maximum) : maximum; }
    double defaultValue(int mode) const noexcept { return value == &AnimationModeSettings::speed && mode == 1 ? 2.0 : initial; }
};

inline constexpr std::array<AnimationField, 5> animationFields {{
    { "SPEED", "speed", &AnimationModeSettings::speed, 0.0, 4.0, 1.0, 0.01, 2, true, true },
    { "GLOW", "glow", &AnimationModeSettings::glow, 0.0, 3.0, 1.0, 0.01, 2, true, false },
    { "SCALE", "scale-factor", &AnimationModeSettings::scaleFactor, 0.1, 2.0, 0.1, 0.01, 2, true, true },
    { "THICKNESS", "thickness", &AnimationModeSettings::thickness, 0.1, 4.0, 1.0, 0.01, 2, false, true },
    { "DENSITY", "density", &AnimationModeSettings::density, 2.0, 20.0, 14.0, 1.0, 0, false, true }
}};

juce::String animationBandProperty(size_t band);
juce::String animationModeProperty(size_t band, int mode, const char* property);
std::optional<bool> validateAnimationProperty(const juce::Identifier&, const juce::var&);
void loadAnimationSettings(AnimationSettings&, const juce::ValueTree&);
void preserveAnimationSettings(juce::XmlElement&, const juce::ValueTree&);
