#pragma once
#include <array>

namespace ava::modules::shear {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float impact=0.f;bool impactToRight=false;
};
}
