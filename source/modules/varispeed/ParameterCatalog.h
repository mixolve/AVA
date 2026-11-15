#pragma once

// Owns the parameter definitions for this block. Slot indices and identifiers
// are intentionally stable for existing host automation and saved sessions.
#include "../shared/BlockParameterTypes.h"

namespace ava::modules::varispeed
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::varispeedRate, { "varispeed.rate", ParameterType::floating, 0.25f, 4.0f, 0.0f, 1.0f, "x", 3, "varispeed.bypass", {"varispeed.units", "R/P"} } , {"VSPEED", "RATE"} },
    { ParameterSlot::varispeedThreshold, { "varispeed.threshold", ParameterType::floating, -96.0f, 0.0f, 0.01f, -48.0f, "dB", 2 } , {"VSPEED", "THRESHOLD"} },
    { ParameterSlot::varispeedRetrigger, { "varispeed.retrigger", ParameterType::floating, 10.0f, 1000.0f, 0.01f, 60.0f, "ms", 2 } , {"VSPEED", "RETRIGGER"} },
    { ParameterSlot::varispeedCrossfade, { "varispeed.crossfade", ParameterType::floating, 0.0f, 100.0f, 0.01f, 2.0f, "ms", 2 } , {"VSPEED", "FADE"} },
    { ParameterSlot::varispeedPreserve, { "varispeed.preserve", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"VSPEED", "PRESERVE PITCH"} },
    { ParameterSlot::varispeedBypass, { "varispeed.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"VSPEED", "BYPASS"} },
    { ParameterSlot::varispeedHidden, { "varispeed.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" } , {"VSPEED", "HIDDEN"} },
    { ParameterSlot::varispeedUnits, { "varispeed.units", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"VSPEED", "RATE/PITCH UNITS"} },
    { ParameterSlot::varispeedQuality, { "varispeed.quality", ParameterType::choice, 0.0f, 2.0f, 1.0f, 0.0f, "" } , {"VSPEED", "SOUNDTOUCH QUALITY"} },
});
} // namespace ava::modules::varispeed
