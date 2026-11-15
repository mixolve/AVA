#pragma once

// Owns the parameter definitions for this block. Slot indices and identifiers
// are intentionally stable for existing host automation and saved sessions.
#include "../shared/BlockParameterTypes.h"

namespace ava::modules::delay
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::stereoDelay, { "stereo.delay", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2, "delay.bypass" } , {"DELAY", "STEREO"} },
    { ParameterSlot::leftDelay, { "left.delay", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2, "delay.bypass" } , {"DELAY", "LEFT"} },
    { ParameterSlot::rightDelay, { "right.delay", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "ms", 2, "delay.bypass" } , {"DELAY", "RIGHT"} },
    { ParameterSlot::delayBypass, { "delay.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"DELAY", "BYPASS"} },
    { ParameterSlot::delayHidden, { "delay.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" } , {"DELAY", "HIDDEN"} },
    { ParameterSlot::delayLinkOpp, { "delay.link.opp", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "", 0, "delay.bypass" } , {"DELAY", "LINK OPPOSITE"} },
});
} // namespace ava::modules::delay
