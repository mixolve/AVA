#pragma once
#include "../shared/BlockTypes.h"

namespace ava::modules::delay
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::delay, "DELAY", 4, "delay.bypass", "delay.hidden" };
}
