#pragma once

#include <algorithm>
#include <map>
#include <vector>

// Pure routing identity helpers used by UNIT SYNC. Node and module numeric IDs
// are local to each plugin instance and must not be copied to another UNIT.
namespace ava::unit_sync
{
template <typename RoutingState>
std::map<int, int> matchingNodes(const RoutingState& from, const RoutingState& to)
{
    std::map<int, int> result;
    if (from.nodes.size() != to.nodes.size()) return result;
    for (size_t i = 0; i < from.nodes.size(); ++i)
    {
        if (from.nodes[i].container != to.nodes[i].container) return {};
        result.emplace(from.nodes[i].id, to.nodes[i].id);
    }
    const auto matches = [&result](int fromId, int toId)
    {
        if (fromId == 0 || toId == 0) return fromId == 0 && toId == 0;
        const auto it = result.find(fromId);
        return it != result.end() && it->second == toId;
    };
    if (!matches(from.rootInstanceId, to.rootInstanceId)
        || !matches(from.entryInstanceId, to.entryInstanceId)) return {};
    for (size_t i = 0; i < from.nodes.size(); ++i)
    {
        const auto& a = from.nodes[i];
        const auto& b = to.nodes[i];
        if (!matches(a.serialNext, b.serialNext)
            || !matches(a.parallelNext, b.parallelNext)
            || !matches(a.groupNext, b.groupNext)) return {};
    }
    return result;
}

template <typename ModuleSlots>
int matchingModuleUid(const ModuleSlots& from, const ModuleSlots& to, int uid)
{
    const auto source = std::find_if(from.begin(), from.end(),
        [uid](const auto& slot) { return slot.uid == uid; });
    if (source == from.end()) return -1;
    const auto dest = std::find_if(to.begin(), to.end(),
        [&source](const auto& slot)
        {
            return slot.type == source->type && slot.created == source->created;
        });
    return dest != to.end() ? dest->uid : -1;
}

inline std::vector<int> remapPinned(const std::vector<int>& source,
                                    const std::map<int, int>& nodeMapping)
{
    std::vector<int> remapped;
    for (const auto id : source)
        if (const auto it = nodeMapping.find(id); it != nodeMapping.end()
            && std::find(remapped.begin(), remapped.end(), it->second) == remapped.end())
            remapped.push_back(it->second);
    return remapped;
}
}
