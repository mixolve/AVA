#pragma once

#include <JuceHeader.h>

#include <array>
#include <cstddef>

namespace dyn::parameters
{
enum class ParameterType
{
    floating,
    boolean,
    choice,
};

enum class ParameterSlot : size_t
{
    morph,
    ratio,
    knee,
    peakHoldMs,
    lookahead,
    tensionFloor,
    tensionHysteresis,
    releaseForm,
    releaseCurve,
    adaptiveOffset,
    adaptiveAttack,
    adaptiveHold,
    adaptiveRelease,
    linkUpDown,
    linkLeftRight,
    linkOpposite,
    leftUpThreshold,
    leftUpAdaptive,
    leftUpTension,
    leftUpRelease,
    leftUpOutput,
    leftDownThreshold,
    leftDownAdaptive,
    leftDownTension,
    leftDownRelease,
    leftDownOutput,
    rightUpThreshold,
    rightUpAdaptive,
    rightUpTension,
    rightUpRelease,
    rightUpOutput,
    rightDownThreshold,
    rightDownAdaptive,
    rightDownTension,
    rightDownRelease,
    rightDownOutput,
    delta,
    count
};

inline constexpr size_t numParameterSlots = static_cast<size_t>(ParameterSlot::count);

struct ParameterSpec
{
    const char* suffix = "";
    const char* name = "";
    ParameterType type = ParameterType::floating;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    float defaultValue = 0.0f;
    const char* label = "";
};

inline constexpr auto parameterSpecs = std::to_array<ParameterSpec>({
    { "morph", "Morph", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "ratio", "Ratio", ParameterType::floating, 1.0f, 100.0f, 0.01f, 100.0f, ":1" },
    { "knee", "Knee", ParameterType::floating, 0.0f, 24.0f, 0.01f, 0.0f, "dB" },
    { "peak-hold", "Peak Hold", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "ms" },
    { "lookahead", "Lookahead", ParameterType::floating, 0.0f, 24.0f, 0.01f, 0.0f, "ms" },
    { "tension-floor", "Tension Floor", ParameterType::floating, -96.0f, 0.0f, 0.01f, -96.0f, "dB" },
    { "tension-hysteresis", "Tension Hysteresis", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "release-form", "Release Form", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "release-curve", "Release Curve", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "offset", "Adaptive Offset", ParameterType::floating, 0.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "attack", "Adaptive Attack", ParameterType::floating, 0.0f, 200.0f, 1.0f, 30.0f, "ms" },
    { "hold", "Adaptive Hold", ParameterType::floating, 0.0f, 2000.0f, 1.0f, 0.0f, "ms" },
    { "release", "Adaptive Release", ParameterType::floating, 0.0f, 2000.0f, 1.0f, 300.0f, "ms" },
    { "up-dn", "Link UP-DN (Dual-Mono)", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "l-r", "Link L-R (Stereo)", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" },
    { "opposite", "Link Opposite", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" },
    { "l-up-threshold", "L-UP-THRESHOLD", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "l-up-adaptive", "L-UP-ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "l-up-tension", "L-UP-TENSION", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "l-up-release", "L-UP-RELEASE", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "l-up-output", "L-UP-OUTPUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "l-dn-threshold", "L-DN-THRESHOLD", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "l-dn-adaptive", "L-DN-ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "l-dn-tension", "L-DN-TENSION", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "l-dn-release", "L-DN-RELEASE", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "l-dn-output", "L-DN-OUTPUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "r-up-threshold", "R-UP-THRESHOLD", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "r-up-adaptive", "R-UP-ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "r-up-tension", "R-UP-TENSION", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "r-up-release", "R-UP-RELEASE", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "r-up-output", "R-UP-OUTPUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "r-dn-threshold", "R-DN-THRESHOLD", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "r-dn-adaptive", "R-DN-ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "r-dn-tension", "R-DN-TENSION", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "r-dn-release", "R-DN-RELEASE", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "r-dn-output", "R-DN-OUTPUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "delta", "Delta", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
});

static_assert(parameterSpecs.size() == numParameterSlots);

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
