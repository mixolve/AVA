#pragma once
#include <array>
#include "Engine.h"

namespace ava::modules::varispeed {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    ava::modules::chain::dsp::VarispeedEngine::Parameters varispeed;
};
}
