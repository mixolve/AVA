#pragma once
#include <array>

namespace ava::modules::delay {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float stereoDelayMs=0.f,leftDelayMs=0.f,rightDelayMs=0.f;
};
}
