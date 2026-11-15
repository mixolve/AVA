#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::delay
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::delay, "DELAY", 4, "delay.bypass", "delay.hidden" };
}
