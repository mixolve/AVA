#pragma once

#include "ModuleStackCore.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>
#include <vector>

// A stack is restored independently of other saved plugin settings.
// Check its internal references before replacing the live stack.
namespace ava::module_stack::state
{
inline constexpr int maximumSavedSerial = 1000000;

struct Copy
{
    int band = 0;
    Slot slot;
    juce::String encodedState;
};

struct Decoded
{
    int nextUid = originalCount;
    std::vector<Order> stacks;
    std::vector<std::array<int, typeCount>> lastCreated;
    std::map<int, Copy> copies;
};

inline int exactInteger(const juce::var& value, const char* name)
{
    const auto* object = value.getDynamicObject();
    if (object == nullptr || !object->hasProperty(name)) return -1;
    const auto number = object->getProperty(name);
    return number.isInt() ? int(number) : -1;
}

inline std::optional<Decoded> decode(const juce::String& serialized, size_t expectedBands)
{
    const auto data = juce::JSON::parse(serialized);
    const auto* root = data.getDynamicObject();
    if (root == nullptr) return std::nullopt;

    Decoded result;
    result.nextUid = exactInteger(data, "nextId");
    if (result.nextUid < originalCount || result.nextUid > maximumSavedSerial)
        return std::nullopt;

    const auto bandsValue = root->getProperty("bands");
    const auto copiesValue = root->getProperty("copies");
    const auto* bands = bandsValue.getArray();
    const auto* copies = copiesValue.getArray();
    if (bands == nullptr || copies == nullptr || bands->size() != int(expectedBands))
        return std::nullopt;

    result.stacks.reserve(expectedBands);
    result.lastCreated.reserve(expectedBands);
    for (size_t band = 0; band < expectedBands; ++band)
    {
        const auto& bandData = (*bands)[int(band)];
        const auto* object = bandData.getDynamicObject();
        if (object == nullptr) return std::nullopt;

        const auto entriesValue = object->getProperty("slots");
        const auto serialsValue = object->getProperty("lastCreated");
        const auto* entries = entriesValue.getArray();
        const auto* serials = serialsValue.getArray();
        const auto addedCreated = object->getProperty("addedCreated");
        if (entries == nullptr || serials == nullptr
            || entries->size() > maximumSlotsPerBand
            || serials->size() != originalCount
            || (typeCount > originalCount && addedCreated.getDynamicObject() == nullptr))
            return std::nullopt;

        Order order;
        std::set<int> ids;
        std::set<std::pair<int, int>> serialIdentities;
        for (const auto& entry : *entries)
        {
            const auto* slotData = entry.getDynamicObject();
            if (slotData == nullptr) return std::nullopt;
            const int uid = exactInteger(entry, "id");
            const int type = exactInteger(entry, "type");
            const int created = exactInteger(entry, "created");
            if (type < 0 || type >= typeCount || created < 1 || created > maximumSavedSerial
                || (uid < originalCount && (uid != originalUid(type) || created != 1))
                || (uid >= originalCount && uid >= result.nextUid)
                || !slotData->getProperty("enabled").isBool()
                || !slotData->getProperty("shown").isBool()
                || !ids.insert(uid).second
                || !serialIdentities.emplace(type, created).second)
                return std::nullopt;

            const Slot slot {uid, type, created, bool(slotData->getProperty("enabled")),
                             bool(slotData->getProperty("shown"))};
            order.push_back(slot);
            if (uid >= originalCount
                && !result.copies.emplace(uid, Copy{int(band), slot, {}}).second)
                return std::nullopt;
        }

        std::array<int, typeCount> counters {};
        for (int type = 0; type < typeCount; ++type)
        {
            const auto serial = type < originalCount ? (*serials)[type]
                : addedCreated.getProperty(juce::Identifier(juce::String(type)), {});
            if (!serial.isInt() || int(serial) < 1 || int(serial) > maximumSavedSerial)
                return std::nullopt;
            counters[size_t(type)] = int(serial);

        }
        for (const auto& slot : order)
            if (slot.created > counters[size_t(slot.type)]) return std::nullopt;
        result.stacks.push_back(std::move(order));
        result.lastCreated.push_back(counters);
    }

    if (copies->size() != int(result.copies.size()))
        return std::nullopt;
    std::set<int> decoded;
    for (const auto& entry : *copies)
    {
        const int uid = exactInteger(entry, "id");
        const auto* object = entry.getDynamicObject();
        const auto found = result.copies.find(uid);
        if (object == nullptr || found == result.copies.end() || !decoded.insert(uid).second)
            return std::nullopt;
        const auto saved = object->getProperty("state");
        if (!saved.isString()) return std::nullopt;
        juce::MemoryBlock bytes;
        if (!bytes.fromBase64Encoding(saved.toString()) || bytes.isEmpty())
            return std::nullopt;
        found->second.encodedState = saved.toString();
    }
    return result;
}
}
