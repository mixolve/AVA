#pragma once
#include <array>

namespace ava::modules::listen {
struct Parameters {
    // Each effect owns its entire parameter subset and availability state.
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    bool listenLc=false,listenRc=false,listenMc=false,listenSc=false,listenLl=false,listenRr=false,listenMsPlus3=false,listenSs=false;
};
}
