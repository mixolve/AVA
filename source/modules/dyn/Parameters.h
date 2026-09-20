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
    { "peak_hold", "Peak Hold", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "ms" },
    { "lookahead", "Lookahead", ParameterType::floating, 0.0f, 24.0f, 0.01f, 0.0f, "ms" },
    { "tension_floor", "Tension Floor", ParameterType::floating, -96.0f, 0.0f, 0.01f, -96.0f, "dB" },
    { "tension_hysteresis", "Tension Hysteresis", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "release_form", "Release Form", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "release_curve", "Release Curve", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "adaptive_offset", "Adaptive Offset", ParameterType::floating, 0.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "adaptive_attack", "Adaptive Attack", ParameterType::floating, 0.0f, 200.0f, 1.0f, 30.0f, "ms" },
    { "adaptive_hold", "Adaptive Hold", ParameterType::floating, 0.0f, 2000.0f, 1.0f, 0.0f, "ms" },
    { "adaptive_release", "Adaptive Release", ParameterType::floating, 0.0f, 2000.0f, 1.0f, 300.0f, "ms" },
    { "link_up_down", "Link UP/DN (Dual-Mono)", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "link_left_right", "Link L/R (Stereo)", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" },
    { "link_opposite", "Link Opp", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" },
    { "left_up_threshold", "L.UP.THR", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "left_up_adaptive", "L.UP.ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "left_up_tension", "L.UP.TENS", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "left_up_release", "L.UP.REL", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "left_up_output", "L.UP.OUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "left_down_threshold", "L.DN.THR", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "left_down_adaptive", "L.DN.ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "left_down_tension", "L.DN.TENS", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "left_down_release", "L.DN.REL", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "left_down_output", "L.DN.OUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "right_up_threshold", "R.UP.THR", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "right_up_adaptive", "R.UP.ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "right_up_tension", "R.UP.TENS", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "right_up_release", "R.UP.REL", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "right_up_output", "R.UP.OUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "right_down_threshold", "R.DN.THR", ParameterType::floating, -96.0f, 12.0f, 0.01f, 0.0f, "dB" },
    { "right_down_adaptive", "R.DN.ADAPTIVE", ParameterType::floating, 0.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "right_down_tension", "R.DN.TENS", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "right_down_release", "R.DN.REL", ParameterType::floating, 0.0f, 1000.0f, 0.01f, 10.0f, "ms" },
    { "right_down_output", "R.DN.OUT", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB" },
    { "delta", "Delta", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
});

static_assert(parameterSpecs.size() == numParameterSlots);

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
