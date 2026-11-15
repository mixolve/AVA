#pragma once

// Owns the parameter definitions for this block. Slot indices and identifiers
// are intentionally stable for existing host automation and saved sessions.
#include "../shared/BlockParameterTypes.h"

namespace ava::modules::gain
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::gainMid, { "mid.gain", ParameterType::floating, float(ava::modules::dsp::gainMinimumDb), float(ava::modules::dsp::gainMaximumDb), 0.01f, 0.0f, "dB", 2, "mid.gain.bypass", { "mid.mute.icon", "MT", true } } , {"GAIN", "MID"} },
    { ParameterSlot::gainMidMute, { "mid.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / MID", "MUTE"} },
    { ParameterSlot::gainSide, { "side.gain", ParameterType::floating, float(ava::modules::dsp::gainMinimumDb), float(ava::modules::dsp::gainMaximumDb), 0.01f, 0.0f, "dB", 2, "side.gain.bypass", { "side.mute.icon", "MT", true } } , {"GAIN", "SIDE"} },
    { ParameterSlot::gainSideMute, { "side.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / SIDE", "MUTE"} },
    { ParameterSlot::gainL, { "left.gain", ParameterType::floating, float(ava::modules::dsp::gainMinimumDb), float(ava::modules::dsp::gainMaximumDb), 0.01f, 0.0f, "dB", 2, "left.gain.bypass", { "left.mute.icon", "MT", true } } , {"GAIN", "LEFT"} },
    { ParameterSlot::gainLMute, { "left.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / LEFT", "MUTE"} },
    { ParameterSlot::gainR, { "right.gain", ParameterType::floating, float(ava::modules::dsp::gainMinimumDb), float(ava::modules::dsp::gainMaximumDb), 0.01f, 0.0f, "dB", 2, "right.gain.bypass", { "right.mute.icon", "MT", true } } , {"GAIN", "RIGHT"} },
    { ParameterSlot::gainRMute, { "right.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / RIGHT", "MUTE"} },
    { ParameterSlot::gainLr, { "stereo.gain", ParameterType::floating, float(ava::modules::dsp::gainMinimumDb), float(ava::modules::dsp::gainMaximumDb), 0.01f, 0.0f, "dB", 2, "stereo.gain.bypass", { "stereo.mute.icon", "MT", true } } , {"GAIN", "STEREO"} },
    { ParameterSlot::gainLrMute, { "stereo.mute.icon", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / STEREO", "MUTE"} },
    { ParameterSlot::gainBypass, { "gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN", "BYPASS"} },
    { ParameterSlot::gainStereoBypass, { "stereo.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / STEREO", "BYPASS"} },
    { ParameterSlot::gainLeftBypass, { "left.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / LEFT", "BYPASS"} },
    { ParameterSlot::gainRightBypass, { "right.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / RIGHT", "BYPASS"} },
    { ParameterSlot::gainMidBypass, { "mid.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / MID", "BYPASS"} },
    { ParameterSlot::gainSideBypass, { "side.gain.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"GAIN / SIDE", "BYPASS"} },
    { ParameterSlot::gainHidden, { "gain.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" } , {"GAIN", "HIDDEN"} },
});
} // namespace ava::modules::gain
