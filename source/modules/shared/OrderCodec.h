#pragma once

#include "StateValidation.h"
#include <array>
#include <cstdint>

namespace ava::modules::order
{
template <typename IdType, size_t size>
inline bool valid(const std::array<IdType,size>& order) noexcept
{
    return ava::modules::state::isPermutation(order, static_cast<int>(size));
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
template <typename IdType, size_t size>
inline juce::String serialise(const std::array<IdType,size>& order)
{
    juce::String result;
    for (auto id : order) result += juce::String::charToString("0123456789ABCDEF"[size_t(id)]);
    return result;
}
template <typename IdType, size_t size>
inline bool parse(const juce::String& text, std::array<IdType,size>& result)
{
    if (text.length() != static_cast<int>(size)) return false;
    std::array<IdType, size> candidate {};
    for (int i = 0; i < static_cast<int>(size); ++i)
    {
        const auto id = juce::String("0123456789ABCDEF").indexOfChar(text[i]);
        if (id < 0 || id >= static_cast<int>(size)) return false;
        candidate[static_cast<size_t>(i)] = static_cast<IdType>(id);
    }
    if (!valid(candidate)) return false;
    result = candidate;
    return true;
}
}
