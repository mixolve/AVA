#pragma once

#include "Splitter.h"

#include <array>
#include <cstddef>

namespace ava::crossover
{
struct Settings
{
    Splitter::SplitFrequencies splitFrequencies { 134.0, 523.0, 2093.0, 5000.0, 10000.0 };
    std::array<bool, Splitter::numRanges> soloMask {};
    size_t activeSplitCount = 0;
};
}
