#pragma once
#include <cstddef>

namespace ava::modules::chain::blocks
{
enum class Id : unsigned char { listen, gain, delay, phase, panorama, shear, balance, ortho, rect, comb, shift, varispeed, sibil };
inline constexpr size_t count = 13;
struct Description
{
    Id id;
    const char* name;
    size_t rows;
    const char* bypassSuffix;
    const char* hiddenSuffix;
};
} // namespace ava::modules::chain::blocks
