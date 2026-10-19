#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstdint>

namespace tls::blocks
{
enum class Id : unsigned char { listen, gain, delay, phase, panorama, shear, balance, ortho, rect, comb };
constexpr size_t count = 10;
using Order = std::array<Id, count>;
struct Description { Id id; const char* name; size_t rows; const char* bypassSuffix; const char* hiddenSuffix; };
inline constexpr std::array<Description, count> descriptions {{
    {Id::listen,"LISTEN",8,"listen.bypass","listen.hidden"}, {Id::gain,"GAIN",5,"gain.bypass","gain.hidden"}, {Id::delay,"DELAY",3,"delay.bypass","delay.hidden"},
    {Id::phase,"PHASE",3,"phase.bypass","phase.hidden"}, {Id::panorama,"PANORAMA",3,"panorama.bypass","panorama.hidden"}, {Id::shear,"SHEAR",2,"shear.bypass","shear.hidden"},
    {Id::balance,"MS-BALANCE",2,"balance.bypass","balance.hidden"}, {Id::ortho,"ORTHO",3,"ortho.bypass","ortho.hidden"}, {Id::rect,"RECT",4,"rect.bypass","rect.hidden"}, {Id::comb,"COMB",2,"comb.count.bypass","comb.hidden"}
}};
inline constexpr Order defaultOrder { Id::listen, Id::gain, Id::delay, Id::phase, Id::panorama,
    Id::shear, Id::balance, Id::ortho, Id::rect, Id::comb };
template <typename IdType, size_t size>
inline bool valid(const std::array<IdType,size>& order) noexcept
{
    unsigned seen=0;
    for (auto id:order) {
        const auto index=static_cast<unsigned>(id);
        if(index>=size || (seen & (1u<<index))!=0) return false;
        seen|=1u<<index;
    }
    return true;
}
template <typename IdType, size_t size>
inline uint64_t pack(const std::array<IdType,size>& order) noexcept
{
    uint64_t result=0;
    for(size_t i=0;i<size;++i) result|=uint64_t(order[i])<<(4*i);
    return result;
}
template <typename OrderType>
inline OrderType unpackOrder(uint64_t packed, const OrderType& fallback) noexcept
{
    OrderType result;
    for(size_t i=0;i<result.size();++i) result[i]=static_cast<typename OrderType::value_type>((packed>>(4*i))&15);
    return valid(result)?result:fallback;
}
inline Order unpack(uint64_t packed) noexcept { return unpackOrder(packed,defaultOrder); }
template <typename IdType, size_t size>
inline juce::String serialise(const std::array<IdType,size>& order)
{
    juce::String result;
    for(auto id:order) result<<juce::String(static_cast<int>(id));
    return result;
}
template <typename IdType, size_t size>
inline bool parse(const juce::String& text, std::array<IdType,size>& result)
{
    if(text.length()!=int(size)) return false;
    for(size_t i=0;i<size;++i) {
        const auto c=text[int(i)];
        if(c<'0' || c>'9') return false;
        result[i]=static_cast<IdType>(c-'0');
    }
    return valid(result);
}
inline juce::Identifier stateKey(size_t range)
{ return juce::Identifier("ava.crossover.tls.block_order."+juce::String(int(range))); }
}
