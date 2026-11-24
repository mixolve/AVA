#pragma once

#include <algorithm>
#include <vector>
#include <utility>

// Pinned INSTS is a navigation strip. Reordering it must never modify the
// routing graph (which may contain serial, parallel and container nodes).
namespace ava::pinned_routing
{
inline std::vector<int> arrange(const std::vector<int>& available, const std::vector<int>& saved)
{
    std::vector<int> result;
    result.reserve(available.size());
    const auto append = [&available, &result](const int id)
    {
        if (id > 0 && std::find(available.begin(), available.end(), id) != available.end()
            && std::find(result.begin(), result.end(), id) == result.end())
            result.push_back(id);
    };
    for (const auto id : saved) append(id);
    for (const auto id : available) append(id);
    return result;
}

// Live tile movement, identical to the DOCK's insertion behaviour.
// The associated ids and Component owners must be moved with the same indices.
template <typename T>
inline bool moveToIndex(std::vector<T>& items, const int from, const int to)
{
    if (from == to) return false;
    if (from < 0 || to < 0 || from >= int(items.size()) || to >= int(items.size()))
        return false;
    if (from < to)
        std::rotate(items.begin() + from, items.begin() + from + 1, items.begin() + to + 1);
    else
        std::rotate(items.begin() + to, items.begin() + from, items.begin() + from + 1);
    return true;
}

inline bool move(std::vector<int>& order, const int sourceId, const int targetId, const bool after)
{
    if (sourceId == targetId) return false;
    const auto source = std::find(order.begin(), order.end(), sourceId);
    const auto target = std::find(order.begin(), order.end(), targetId);
    if (source == order.end() || target == order.end()) return false;
    auto updated = order;
    updated.erase(std::find(updated.begin(), updated.end(), sourceId));
    const auto destination = std::find(updated.begin(), updated.end(), targetId);
    updated.insert(destination + (after ? 1 : 0), sourceId);
    if (updated == order) return false;
    order = std::move(updated);
    return true;
}
}
