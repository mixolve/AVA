#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::listen
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::listen, "LISTEN", 8, "listen.bypass", "listen.hidden" };
}
