#pragma once
#include "../shared/OrderCodec.h"

namespace ava::modules::chain::gain
{
enum class Id : unsigned char { stereo, left, right, mid, side };
inline constexpr size_t count=5;
using Order=std::array<Id,count>;
struct Description { Id id; const char* name; const char* bypassSuffix; };
inline constexpr std::array<Description,count> descriptions {{
    {Id::stereo,"STEREO","stereo.gain.bypass"}, {Id::left,"LEFT","left.gain.bypass"}, {Id::right,"RIGHT","right.gain.bypass"}, {Id::mid,"MID","mid.gain.bypass"}, {Id::side,"SIDE","side.gain.bypass"}
}};
inline constexpr Order defaultOrder {Id::stereo,Id::left,Id::right,Id::mid,Id::side};
using ava::modules::order::valid;
using ava::modules::order::pack;
using ava::modules::order::serialise;
using ava::modules::order::parse;
inline Order unpack(uint64_t packed) noexcept { return ava::modules::order::unpackOrder(packed,defaultOrder); }
inline juce::Identifier stateKey(size_t range)
{ return juce::Identifier("ava.crossover.chain.gain_order."+juce::String(int(range))); }
}
