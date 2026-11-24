#pragma once

#include "../shared/BlockParameterTypes.h"

namespace ava::modules::ortho
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::degree, { "degree", ParameterType::floating, 0.0f, 359.99f, 0.01f, 0.0f, "deg", 2, "ortho.bypass" } , {"ORTHO", "DEGREE"} },
    { ParameterSlot::flipRight, { "flip-right", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "", 0, "flip-right.bypass" } , {"ORTHO", "FLIP RIGHT"} },
    { ParameterSlot::flipRightBypass, { "flip-right.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"ORTHO / FLIP RIGHT", "BYPASS"} },
    { ParameterSlot::orthoBypass, { "ortho.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"ORTHO", "BYPASS"} },
    { ParameterSlot::orthoHidden, { "ortho.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"ORTHO", "HIDDEN"} },
});
}
