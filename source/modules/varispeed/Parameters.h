#pragma once
#include <array>
#include "VarispeedEngine.h"

namespace ava::modules::varispeed {
struct Parameters {
    // Each effect owns its entire parameter subset and availability state.
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    ava::modules::chain::dsp::VarispeedEngine::Parameters varispeed;
};
}
