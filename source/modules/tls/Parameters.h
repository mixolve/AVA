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
    listenMsPlus3,
    listenSs,
    stereoDelay,
    leftDelay,
    rightDelay,
    stereoPhase,
    leftPhase,
    rightPhase,
    combCount,
    combShift,
    combShiftBypass,
    combCountBypass,
    listenBypass,
    gainBypass,
    delayBypass,
    phaseBypass,
    panoramaBypass,
    shearBypass,
    balanceBypass,
    orthoBypass,
    rectBypass,
    gainStereoBypass,
    gainLeftBypass,
    gainRightBypass,
    gainMidBypass,
    gainSideBypass,
    listenHidden,
    gainHidden,
    delayHidden,
    phaseHidden,
    panoramaHidden,
    shearHidden,
    balanceHidden,
    orthoHidden,
    rectHidden,
    combHidden,
    count
};

inline constexpr size_t numParameterSlots = static_cast<size_t>(ParameterSlot::count);

struct ParameterSpec
{
    const char* suffix = "";
    ParameterType type = ParameterType::floating;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.01f;
    float defaultValue = 0.0f;
    const char* label = "";
    int displayDecimals = 2;
};

inline constexpr auto parameterSpecs = std::to_array<ParameterSpec>({
    { "mid.gain", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "mid.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "side.gain", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "side.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "left.gain", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "left.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "right.gain", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "right.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "stereo.gain", ParameterType::floating, -99.0f, 48.0f, 0.01f, 0.0f, "dB" },
    { "stereo.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "hpos", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "hneg", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "fpos", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "fneg", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "left.pan", ParameterType::floating, -100.0f, 100.0f, 0.01f, -100.0f, "%" },
    { "right.pan", ParameterType::floating, -100.0f, 100.0f, 0.01f, 100.0f, "%" },
    { "law", ParameterType::floating, 0.0f, 6.0f, 0.01f, 0.0f, "dB", 2 },
    { "impact", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "direction", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mid.balance", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "side.balance", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "%" },
    { "degree", ParameterType::floating, 0.0f, 359.99f, 0.01f, 0.0f, "deg" },
    { "flip-right", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "lc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "rc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "sc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "ll", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "rr", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "ms+3", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "ss", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "stereo.delay", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "left.delay", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "right.delay", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2 },
    { "stereo.phase", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
    { "left.phase", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
    { "right.phase", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg" },
    { "comb.count", ParameterType::floating, 0.0f, 100.0f, 1.0f, 0.0f, "", 0 },
    { "comb.shift", ParameterType::floating, -50.0f, 50.0f, 0.01f, 0.0f, "%", 0 },
    { "comb.shift.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "comb.count.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "delay.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "phase.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "panorama.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "shear.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "balance.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "ortho.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "rect.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "stereo.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "left.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "right.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "mid.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "side.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "listen.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "gain.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "delay.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "phase.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "panorama.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "shear.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "balance.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "ortho.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "rect.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
    { "comb.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" },
});

static_assert(parameterSpecs.size() == numParameterSlots);
inline constexpr std::array blockBypassSlots { ParameterSlot::listenBypass, ParameterSlot::gainBypass, ParameterSlot::delayBypass, ParameterSlot::phaseBypass, ParameterSlot::panoramaBypass, ParameterSlot::shearBypass, ParameterSlot::balanceBypass, ParameterSlot::orthoBypass, ParameterSlot::rectBypass, ParameterSlot::combCountBypass };
inline constexpr std::array blockHiddenSlots { ParameterSlot::listenHidden, ParameterSlot::gainHidden, ParameterSlot::delayHidden, ParameterSlot::phaseHidden, ParameterSlot::panoramaHidden, ParameterSlot::shearHidden, ParameterSlot::balanceHidden, ParameterSlot::orthoHidden, ParameterSlot::rectHidden, ParameterSlot::combHidden };
inline constexpr std::array gainBypassSlots { ParameterSlot::gainStereoBypass, ParameterSlot::gainLeftBypass, ParameterSlot::gainRightBypass, ParameterSlot::gainMidBypass, ParameterSlot::gainSideBypass };

constexpr size_t toIndex(const ParameterSlot slot)
{
    return static_cast<size_t>(slot);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
