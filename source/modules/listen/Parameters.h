#pragma once
#include <array>

namespace ava::modules::listen {
struct Parameters {
    bool hidden = false;
    bool bypassed = false;
    bool available() const noexcept { return !hidden && !bypassed; }
    bool listenLc=false,listenRc=false,listenMc=false,listenSc=false,listenLl=false,listenRr=false,listenMsPlus3=false,listenSs=false;
};
}
