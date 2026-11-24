#pragma once
#include "../shared/BlockTypes.h"

namespace ava::modules::listen
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::listen, "LISTEN", 8, "listen.bypass", "listen.hidden" };
}
