#pragma once

#include "../shared/BlockParameterTypes.h"

namespace ava::modules::phase
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::stereoPhase, { "stereo.phase", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg", 2, "phase.bypass" } , {"PHASE", "STEREO"} },
    { ParameterSlot::leftPhase, { "left.phase", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg", 2, "phase.bypass" } , {"PHASE", "LEFT"} },
    { ParameterSlot::rightPhase, { "right.phase", ParameterType::floating, -180.0f, 180.0f, 0.01f, 0.0f, "deg", 2, "phase.bypass" } , {"PHASE", "RIGHT"} },
    { ParameterSlot::phaseBypass, { "phase.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"PHASE", "BYPASS"} },
    { ParameterSlot::phaseHidden, { "phase.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"PHASE", "HIDDEN"} },
});
}
