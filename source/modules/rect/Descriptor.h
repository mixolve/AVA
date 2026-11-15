#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::rect
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::rect, "RECT", 4, "rect.bypass", "rect.hidden" };
}
