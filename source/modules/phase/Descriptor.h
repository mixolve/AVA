#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::phase
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::phase, "PHASE", 3, "phase.bypass", "phase.hidden" };
}
