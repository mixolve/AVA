#pragma once
#include <array>

namespace ava::modules::panorama {
struct Parameters {
    // Each effect owns its entire parameter subset and availability state.
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float left=-100.f,right=100.f,law=0.f;
};
}
