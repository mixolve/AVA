#pragma once
#include <array>

namespace ava::modules::rect {
struct Parameters {
    // Each effect owns its entire parameter subset and availability state.
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    bool halfPositive=false,halfNegative=false,fullPositive=false,fullNegative=false;
};
}
