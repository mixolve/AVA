#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::ortho
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::ortho, "ORTHO", 3, "ortho.bypass", "ortho.hidden" };
}
