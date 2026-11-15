#pragma once

// Owns the parameter definitions for this block. Slot indices and identifiers
// are intentionally stable for existing host automation and saved sessions.
#include "../shared/BlockParameterTypes.h"

namespace ava::modules::listen
{
using ava::modules::chain::parameters::ParameterSlot;
using ava::modules::chain::parameters::ParameterType;
using ava::modules::chain::parameters::SlotDefinition;

inline constexpr auto parameterCatalog = std::to_array<SlotDefinition>({
    { ParameterSlot::listenLc, { "lc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "LEFT CENTER"} },
    { ParameterSlot::listenRc, { "rc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "RIGHT CENTER"} },
    { ParameterSlot::listenMc, { "mc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "MID CENTER"} },
    { ParameterSlot::listenSc, { "sc", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "SIDE CENTER"} },
    { ParameterSlot::listenLl, { "ll", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "LEFT LEFT"} },
    { ParameterSlot::listenRr, { "rr", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "RIGHT RIGHT"} },
    { ParameterSlot::listenMsPlus3, { "ms+3", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "MS+3"} },
    { ParameterSlot::listenSs, { "ss", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "SIDE SIDE"} },
    { ParameterSlot::listenBypass, { "listen.bypass", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 0.0f, "" } , {"LISTEN", "BYPASS"} },
    { ParameterSlot::listenHidden, { "listen.hidden", ParameterType::boolean, 0.0f, 1.0f, 1.0f, 1.0f, "" } , {"LISTEN", "HIDDEN"} },
});
} // namespace ava::modules::listen
