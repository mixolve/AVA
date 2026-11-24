#pragma once
#include <array>
#include "Engine.h"

namespace ava::modules::shift {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float shiftPitch=0.f,shiftFrequency=1000.f;ava::modules::chain::dsp::ShiftEngine::Options shiftOptions;
};
}
