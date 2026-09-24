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
    { "mid.gain", "MID", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "mid.mute.icon", "MID MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "side.gain", "SIDE", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "side.mute.icon", "SIDE MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "left.gain", "LEFT", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "left.mute.icon", "LEFT MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "right.gain", "RIGHT", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "right.mute.icon", "RIGHT MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "stereo.gain", "STEREO", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "stereo.mute.icon", "STEREO MUTE", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "left-order", "LEFT ORDER", ParameterType::floating, 2.0f, 5.0f, 1.0f, 2.0f, "", 0 },
    { "right-order", "RIGHT ORDER", ParameterType::floating, 2.0f, 5.0f, 1.0f, 3.0f, "", 0 },
    { "mid-order", "MID ORDER", ParameterType::floating, 2.0f, 5.0f, 1.0f, 4.0f, "", 0 },
    { "side-order", "SIDE ORDER", ParameterType::floating, 2.0f, 5.0f, 1.0f, 5.0f, "", 0 },
    { "hpos", "HPOS", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "hneg", "HNEG", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "fpos", "FPOS", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "fneg", "FNEG", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "left.pan", "LEFT", ParameterType::floating, -100.0f, 100.0f, 0.01f, -100.0f, "%" },
    { "right.pan", "RIGHT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 100.0f, "%" },
    { "law", "LAW", ParameterType::floating, 0.0f, 6.0f, 0.01f, 0.0f, "dB", 2 },
    { "impact", "IMPACT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "direction", "DIRECTION", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mid.balance", "MID", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "side.balance", "SIDE", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "degree", "DEGREE", ParameterType::floating, 0.0f, 359.99f, 0.01f, 0.0f, "deg" },
    { "flip-right", "FLIP RIGHT", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "lc", "LC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "rc", "RC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mc", "MC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "sc", "SC", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "ll", "LL", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "rr", "RR", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "ss", "SS", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "stereo.delay", "STEREO", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "left.delay", "LEFT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "right.delay", "RIGHT", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "stereo.phase", "STEREO", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
    { "left.phase", "LEFT", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
    { "right.phase", "RIGHT", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
});

static_assert(parameterSpecs.size() == numParameterSlots);

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
