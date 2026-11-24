#pragma once

#include "../shared/BlockParameterTypes.h"

namespace ava::modules::panorama
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::left, { "left.pan", ParameterType::floating, -100.0f, 100.0f, 0.01f, -100.0f, "", 2, "panorama.bypass" } , {"PAN", "LEFT"} },
    { ParameterSlot::right, { "right.pan", ParameterType::floating, -100.0f, 100.0f, 0.01f, 100.0f, "", 2, "panorama.bypass" } , {"PAN", "RIGHT"} },
    { ParameterSlot::law, { "law", ParameterType::floating, 0.0f, 6.0f, 0.01f, 0.0f, "dB", 2, "panorama.bypass" } , {"PAN", "LAW"} },
    { ParameterSlot::panoramaBypass, { "panorama.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"PAN", "BYPASS"} },
    { ParameterSlot::panoramaHidden, { "panorama.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"PAN", "HIDDEN"} },
});
}
