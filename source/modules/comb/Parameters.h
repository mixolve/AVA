#pragma once
#include <array>

namespace ava::modules::comb {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    bool combCountBypass=false,combShiftBypass=false;int combCount=0;float combShift=0.f,combMix=100.f;
};
}
