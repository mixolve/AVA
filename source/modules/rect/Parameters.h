#pragma once
#include <array>

namespace ava::modules::rect {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    bool halfPositive=false,halfNegative=false,fullPositive=false,fullNegative=false;
};
}
