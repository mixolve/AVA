#pragma once
#include "../shared/BlockTypes.h"

// Metadata owned by this block; the shared chain only composes descriptors.
namespace ava::modules::balance
{
inline constexpr ava::modules::chain::blocks::Description description {
    ava::modules::chain::blocks::Id::balance, "MS-BAL", 2, "balance.bypass", "balance.hidden" };
}
