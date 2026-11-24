#pragma once
#include <array>

namespace ava::modules::phase {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float stereoPhase=0.f,leftPhase=0.f,rightPhase=0.f;
};
}
