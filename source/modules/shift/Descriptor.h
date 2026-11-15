#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::shift
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::shift, "SHIFT", 13, "shift.bypass", "shift.hidden" };
}
