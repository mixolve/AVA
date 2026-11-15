#pragma once

// Owns the parameter definitions for this block. Slot indices and identifiers
// are intentionally stable for existing host automation and saved sessions.
#include "../shared/BlockParameterTypes.h"

namespace ava::modules::shear
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::impact, { "impact", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "", 2, "shear.bypass" } , {"SHEAR", "IMPACT"} },
    { ParameterSlot::impactDirection, { "direction", ParameterType::choice, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"SHEAR", "DIRECT"} },
    { ParameterSlot::shearBypass, { "shear.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"SHEAR", "BYPASS"} },
    { ParameterSlot::shearHidden, { "shear.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" } , {"SHEAR", "HIDDEN"} },
});
} // namespace ava::modules::shear
