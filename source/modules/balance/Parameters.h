#pragma once
#include <array>

namespace ava::modules::balance {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float mid=0.f,side=0.f;
};
}
