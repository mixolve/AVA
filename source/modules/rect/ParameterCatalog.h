#pragma once

#include "../shared/BlockParameterTypes.h"

namespace ava::modules::rect
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::halfPositive, { "hpos", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"RECT", "HALF POSITIVE"} },
    { ParameterSlot::halfNegative, { "hneg", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"RECT", "HALF NEGATIVE"} },
    { ParameterSlot::fullPositive, { "fpos", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"RECT", "FULL POSITIVE"} },
    { ParameterSlot::fullNegative, { "fneg", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"RECT", "FULL NEGATIVE"} },
    { ParameterSlot::rectBypass, { "rect.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"RECT", "BYPASS"} },
    { ParameterSlot::rectHidden, { "rect.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"RECT", "HIDDEN"} },
});
}
