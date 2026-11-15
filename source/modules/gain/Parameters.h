#pragma once
#include <array>

namespace ava::modules::gain {
struct Parameters {
    // Each effect owns its entire parameter subset and availability state.
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    float gainMid=0.f,gainSide=0.f,gainL=0.f,gainR=0.f,gainLr=0.f;
bool gainMidMute=false,gainSideMute=false,gainLMute=false,gainRMute=false,gainLrMute=false;
std::array<bool,5> gainBypass {};
};
}
