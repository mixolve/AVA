#pragma once
#include "BlockOrder.h"

namespace tls::gain
{
enum class Id : unsigned char { stereo, left, right, mid, side };
inline constexpr size_t count=5;
using Order=std::array<Id,count>;
struct Description { Id id; const char* name; const char* bypassSuffix; };
inline constexpr std::array<Description,count> descriptions {{
    {Id::stereo,"STEREO","stereo.gain.bypass"}, {Id::left,"LEFT","left.gain.bypass"}, {Id::right,"RIGHT","right.gain.bypass"}, {Id::mid,"MID","mid.gain.bypass"}, {Id::side,"SIDE","side.gain.bypass"}
}};
inline constexpr Order defaultOrder {Id::stereo,Id::left,Id::right,Id::mid,Id::side};
using blocks::valid;
using blocks::pack;
using blocks::serialise;
using blocks::parse;
inline Order unpack(uint64_t packed) noexcept { return blocks::unpackOrder(packed,defaultOrder); }
inline juce::Identifier stateKey(size_t range)
{ return juce::Identifier("ava.crossover.tls.gain_order."+juce::String(int(range))); }
}
