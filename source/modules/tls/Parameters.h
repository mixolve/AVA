#pragma once

#include <JuceHeader.h>

#include <array>
#include <cstddef>

namespace tls::parameters
{
enum class ParameterType
{
    floating,
    boolean,
    choice,
};

enum class ParameterSlot : size_t
{
    gainMid,
    gainMidMute,
    gainSide,
    gainSideMute,
    gainL,
    gainLMute,
    gainR,
    gainRMute,
    gainLr,
    gainLrMute,
    gainLOrder,
    gainROrder,
    gainMidOrder,
    gainSideOrder,
    halfPositive,
    halfNegative,
    fullPositive,
    fullNegative,
    left,
    right,
    law,
    impact,
    impactDirection,
    mid,
    side,
    degree,
    flipRight,
    listenLc,
    listenRc,
    listenMc,
    listenSc,
    listenLl,
    listenRr,
    listenSs,
    stereoDelay,
    leftDelay,
    rightDelay,
    stereoPhase,
    leftPhase,
    rightPhase,
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
    int displayDecimals = 2;
    bool muteAtMinimum = false;
};

inline constexpr auto parameterSpecs = std::to_array<ParameterSpec>({
    { "gain_mid", "MID", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "gain_mid_mute", "MID MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "gain_side", "SIDE", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "gain_side_mute", "SIDE MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "gain_left", "LEFT", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "gain_left_mute", "LEFT MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "gain_right", "RIGHT", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "gain_right_mute", "RIGHT MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "gain_stereo", "STEREO", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "gain_stereo_mute", "STEREO MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "gain_left_order", "LEFT ORDER", ParameterType::floating, 0.0f, 3.0f, 1.0f, 0.0f, "", 0 },
    { "gain_right_order", "RIGHT ORDER", ParameterType::floating, 0.0f, 3.0f, 1.0f, 1.0f, "", 0 },
    { "gain_mid_order", "MID ORDER", ParameterType::floating, 0.0f, 3.0f, 1.0f, 2.0f, "", 0 },
    { "gain_side_order", "SIDE ORDER", ParameterType::floating, 0.0f, 3.0f, 1.0f, 3.0f, "", 0 },
    { "half_positive", "HPOS", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "half_negative", "HNEG", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "full_positive", "FPOS", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "full_negative", "FNEG", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "left", "LEFT", ParameterType::floating, -100.0f, 100.0f, 0.01f, -100.0f, "%" },
    { "right", "RIGHT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 100.0f, "%" },
    { "law", "LAW", ParameterType::floating, 0.0f, 6.0f, 0.01f, 0.0f, "dB", 2 },
    { "impact", "IMPACT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "impact_direction", "DIRECTION", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mid", "MID", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "side", "SIDE", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "degree", "DEGREE", ParameterType::floating, 0.0f, 359.99f, 0.01f, 0.0f, "deg" },
    { "flip_right", "FLIP RIGHT", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen_lc", "LC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen_rc", "RC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen_mc", "MC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen_sc", "SC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen_ll", "LL", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen_rr", "RR", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen_ss", "SS", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "stereo_delay", "STEREO", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "left_delay", "LEFT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "right_delay", "RIGHT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "stereo_phase", "STEREO", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
    { "left_phase", "LEFT", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
    { "right_phase", "RIGHT", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
});

static_assert(parameterSpecs.size() == numParameterSlots);

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
