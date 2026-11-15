#pragma once

#include <JuceHeader.h>
#include "ParameterLayout.h"
#include "DspUtilities.h"
#include <array>
#include <cstddef>

namespace ava::modules::chain::parameters
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
    delayLinkOpp,
    combMix,
    shiftPitch,
    shiftFrequency,
    shiftBypass,
    shiftHidden,
    shiftWindow,
    shiftPitchMode,
    shiftDetector,
    shiftTransients,
    shiftIndependentPhase,
    shiftOrder,
    shiftQ,
    shiftGainOrig,
    shiftGainPitched,
    varispeedRate,
    varispeedThreshold, varispeedRetrigger, varispeedCrossfade, varispeedPreserve, varispeedBypass, varispeedHidden, varispeedUnits, varispeedQuality,
    flipRightBypass,
    shiftSoloOrig,
    shiftSoloPitched,
    shiftGainOrigBypass,
    shiftGainPitchedBypass,
    sibilLevel, sibilBrightness, sibilNoise, sibilWindow, sibilFade, sibilGainS, sibilGainRest, sibilSoloS, sibilSoloRest, sibilBypass, sibilHidden, sibilRange, sibilMin, sibilMax, sibilPre, sibilPost,
    count
};

inline constexpr size_t numParameterSlots = static_cast<size_t>(ParameterSlot::count);

using ava::modules::parameters::ControlMetadata;
using ava::modules::parameters::ControlPolicy;

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
    const char* bypassSuffix = "";
    ControlMetadata control {};
};


struct HostLabel
{
    const char* block;
    const char* label;
};

struct SlotDefinition
{
    ParameterSlot slot;
    ParameterSpec spec;
    HostLabel hostLabel;
};

constexpr size_t toIndex(const ParameterSlot slot) noexcept
{
    return static_cast<size_t>(slot);
}
} // namespace ava::modules::chain::parameters
