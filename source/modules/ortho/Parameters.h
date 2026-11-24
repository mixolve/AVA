#pragma once
#include <array>

namespace ava::modules::ortho {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float degree=0.f;
    bool flipRight=false;
    bool flipRightBypassed=false;
};
}
