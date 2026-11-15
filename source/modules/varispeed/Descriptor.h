#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::varispeed
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::varispeed, "VSPEED", 6, "varispeed.bypass", "varispeed.hidden" };
}
