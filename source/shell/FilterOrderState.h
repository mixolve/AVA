#pragma once

#include "../modules/shared/StateValidation.h"

#include <optional>
#include <vector>

namespace shell_filter_order_state
{
using ava::modules::state::isPermutation;

inline juce::String encode(const std::vector<int>& order, const int activeCount)
{
    if (activeCount <= 0 || ! isPermutation(order, activeCount))
        return {};

    juce::StringArray values;
    values.ensureStorageAllocated(activeCount);

    for (int orderIndex = 0; orderIndex < activeCount; ++orderIndex)
        values.add(juce::String(order[static_cast<size_t>(orderIndex)]));

    return values.joinIntoString(",");
}

inline std::optional<std::vector<int>> decode(const juce::String& text,
                                              const int activeCount,
                                              const int maximumCount)
{
    if (activeCount < 0 || activeCount > maximumCount)
        return std::nullopt;

    if (activeCount == 0)
        return text.isEmpty() ? std::optional<std::vector<int>> { std::vector<int> {} }
                              : std::nullopt;

    if (text.isEmpty())
        return std::nullopt;

    const auto tokens = juce::StringArray::fromTokens(text, ",", "");

    if (tokens.size() != activeCount)
        return std::nullopt;

    std::vector<int> order;
    order.reserve(static_cast<size_t>(maximumCount));

    for (const auto& token : tokens)
    {
        const auto filterIndex = token.getIntValue();

        if (token != juce::String(filterIndex))
            return std::nullopt;

        order.push_back(filterIndex);
    }

    if (! isPermutation(order, activeCount))
        return std::nullopt;

    for (int filterIndex = activeCount; filterIndex < maximumCount; ++filterIndex)
        order.push_back(filterIndex);

    return order;
}

inline bool isCurrentEncoding(const juce::String& text, const int maximumCount)
{
    if (text.isEmpty())
        return false;

    const auto tokens = juce::StringArray::fromTokens(text, ",", "");
    return decode(text, tokens.size(), maximumCount).has_value();
}

inline std::vector<int> makeIdentity(const int maximumCount)
{
    std::vector<int> order;
    order.reserve(static_cast<size_t>(juce::jmax(0, maximumCount)));

    for (int filterIndex = 0; filterIndex < maximumCount; ++filterIndex)
        order.push_back(filterIndex);

    return order;
}
}
