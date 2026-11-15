#pragma once
#include "BlockTypes.h"
#include "../gain/GainOrder.h"
#include "../listen/Parameters.h"
#include "../gain/Parameters.h"
#include "../delay/Parameters.h"
#include "../phase/Parameters.h"
#include "../panorama/Parameters.h"
#include "../shear/Parameters.h"
#include "../balance/Parameters.h"
#include "../ortho/Parameters.h"
#include "../rect/Parameters.h"
#include "../comb/Parameters.h"
#include "../shift/Parameters.h"
#include "../varispeed/Parameters.h"
#include "../sibil/Parameters.h"

namespace ava::modules::chain::dsp {
struct BlockParameters final {
    ava::modules::listen::Parameters listen;
    ava::modules::gain::Parameters gain;
    ava::modules::delay::Parameters delay;
    ava::modules::phase::Parameters phase;
    ava::modules::panorama::Parameters panorama;
    ava::modules::shear::Parameters shear;
    ava::modules::balance::Parameters balance;
    ava::modules::ortho::Parameters ortho;
    ava::modules::rect::Parameters rect;
    ava::modules::comb::Parameters comb;
    ava::modules::shift::Parameters shift;
    ava::modules::varispeed::Parameters varispeed;
    ava::modules::sibil::Parameters sibil;
    gain::Order gainOrder = gain::defaultOrder;
};
}
