#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::panorama
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::panorama, "PAN", 3, "panorama.bypass", "panorama.hidden" };
}
