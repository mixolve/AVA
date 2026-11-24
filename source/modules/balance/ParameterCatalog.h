#pragma once

#include "../shared/BlockParameterTypes.h"

namespace ava::modules::balance
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::mid, { "mid.balance", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "", 2, "balance.bypass" } , {"MS-BAL", "MID"} },
    { ParameterSlot::side, { "side.balance", ParameterType::floating, -100.0f, 100.0f, 0.01f, 0.0f, "", 2, "balance.bypass" } , {"MS-BAL", "SIDE"} },
    { ParameterSlot::balanceBypass, { "balance.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"MS-BAL", "BYPASS"} },
    { ParameterSlot::balanceHidden, { "balance.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"MS-BAL", "HIDDEN"} },
});
}
