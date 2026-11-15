#pragma once

#include "BlockTypes.h"
#include "../listen/Descriptor.h"
#include "../gain/Descriptor.h"
#include "../delay/Descriptor.h"
#include "../phase/Descriptor.h"
#include "../panorama/Descriptor.h"
#include "../shear/Descriptor.h"
#include "../balance/Descriptor.h"
#include "../ortho/Descriptor.h"
#include "../rect/Descriptor.h"
#include "../comb/Descriptor.h"
#include "../shift/Descriptor.h"
#include "../varispeed/Descriptor.h"
#include "../sibil/Descriptor.h"
#include <array>

namespace ava::modules::chain::blocks
{
inline constexpr std::array<Description, count> descriptions {{
    ava::modules::listen::description,
    ava::modules::gain::description,
    ava::modules::delay::description,
    ava::modules::phase::description,
    ava::modules::panorama::description,
    ava::modules::shear::description,
    ava::modules::balance::description,
    ava::modules::ortho::description,
    ava::modules::rect::description,
    ava::modules::comb::description,
    ava::modules::shift::description,
    ava::modules::varispeed::description,
    ava::modules::sibil::description
}};
}
