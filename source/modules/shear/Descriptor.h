#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::shear
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::shear, "SHEAR", 2, "shear.bypass", "shear.hidden" };
}
