#pragma once
#include <array>

namespace ava::modules::ortho {
struct Parameters {
    // Each effect owns its entire parameter subset and availability state.
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float degree=0.f;
    bool flipRight=false;
    bool flipRightBypassed=false;
};
}
