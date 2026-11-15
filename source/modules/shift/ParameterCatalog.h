#pragma once

// Owns the parameter definitions for this block. Slot indices and identifiers
// are intentionally stable for existing host automation and saved sessions.
#include "../shared/BlockParameterTypes.h"

namespace ava::modules::shift
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::shiftPitch, { "shift.pitch", ParameterType::floating, -24.0f, 24.0f, 0.01f, 0.0f, "st", 2, "shift.bypass" } , {"SHIFT", "PITCH"} },
    { ParameterSlot::shiftFrequency, { "shift.freq", ParameterType::floating, 20.0f, 20000.0f, 0.01f, 1000.0f, "Hz", 2 } , {"SHIFT", "FREQ"} },
    { ParameterSlot::shiftBypass, { "shift.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"SHIFT", "BYPASS"} },
    { ParameterSlot::shiftHidden, { "shift.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" } , {"SHIFT", "HIDDEN"} },
    { ParameterSlot::shiftWindow, { "shift.window", ParameterType::choice, 0.0f, 2.0f, 1.0f, 1.0f, "" } , {"SHIFT", "WINDOW"} },
    { ParameterSlot::shiftPitchMode, { "shift.pitch-mode", ParameterType::choice, 0.0f, 2.0f, 1.0f, 2.0f, "" } , {"SHIFT", "PITCH MODE"} },
    { ParameterSlot::shiftDetector, { "shift.detector", ParameterType::choice, 0.0f, 2.0f, 1.0f, 0.0f, "" } , {"SHIFT", "DETECTOR"} },
    { ParameterSlot::shiftTransients, { "shift.transients", ParameterType::choice, 0.0f, 2.0f, 1.0f, 0.0f, "" } , {"SHIFT", "TRANSIENTS"} },
    { ParameterSlot::shiftIndependentPhase, { "shift.independent-phase", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"SHIFT", "INDEPENDENT PHASE"} },
    { ParameterSlot::shiftOrder, { "shift.order", ParameterType::choice, 0.0f, 4.0f, 1.0f, 0.0f, "" } , {"SHIFT", "ORDER"} },
    { ParameterSlot::shiftQ, { "shift.q", ParameterType::floating, -1.0f, 1.0f, 0.01f, 0.0f, "", 2 } , {"SHIFT", "Q"} },
    { ParameterSlot::shiftGainOrig, { "shift.gain-orig", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB", 2, "shift.gain-orig.bypass" } , {"SHIFT", "GAIN ORIG"} },
    { ParameterSlot::shiftGainPitched, { "shift.gain-pitched", ParameterType::floating, -96.0f, 96.0f, 0.01f, 0.0f, "dB", 2, "shift.gain-pitched.bypass" } , {"SHIFT", "GAIN PITCHED"} },
    // The two independent toggles are the visible controls from AVA 75.
    { ParameterSlot::shiftSoloOrig, { "shift.solo-orig", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" }, {"SHIFT", "SOLO ORIG"} },
    { ParameterSlot::shiftSoloPitched, { "shift.solo-pitched", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" }, {"SHIFT", "SOLO PITCHED"} },
    { ParameterSlot::shiftGainOrigBypass, { "shift.gain-orig.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" }, {"SHIFT / GAIN ORIG", "BYPASS"} },
    { ParameterSlot::shiftGainPitchedBypass, { "shift.gain-pitched.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" }, {"SHIFT / GAIN PITCHED", "BYPASS"} },
});
} // namespace ava::modules::shift
