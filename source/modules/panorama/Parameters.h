#pragma once
#include <array>

namespace ava::modules::panorama {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float left=-100.f,right=100.f,law=0.f;
};
}
