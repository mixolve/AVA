#pragma once

#include "../shared/BlockParameterTypes.h"

namespace ava::modules::comb
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::combCount, { "comb.count", ParameterType::floating, 0.0f, 100.0f, 1.0f, 0.0f, "", 0, "comb.count.bypass" } , {"COMB", "COUNT"} },
    { ParameterSlot::combShift, { "comb.shift", ParameterType::floating, -50.0f, 50.0f, 0.01f, 0.0f, "", 0, "comb.shift.bypass" } , {"COMB", "SHIFT"} },
    { ParameterSlot::combShiftBypass, { "comb.shift.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"COMB / SHIFT", "BYPASS"} },
    { ParameterSlot::combCountBypass, { "comb.count.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"COMB / COUNT", "BYPASS"} },
    { ParameterSlot::combHidden, { "comb.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"COMB", "HIDDEN"} },
    { ParameterSlot::combMix, { "comb.mix", ParameterType::floating, 0.0f, 100.0f, 0.01f, 100.0f, "%", 2, "comb.count.bypass" } , {"COMB", "MIX"} },
});
}
