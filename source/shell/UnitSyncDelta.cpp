#include "UnitSyncDelta.h"
#include "Processor.h"

#include <algorithm>
#include <map>
#include <set>

namespace ava::unit_sync
{
juce::ValueTree decode(const juce::String& encoded)
{
    juce::MemoryBlock data;
    if (!data.fromBase64Encoding(encoded)) return {};
    const auto xml = juce::AudioProcessor::getXmlFromBinary(data.getData(), static_cast<int>(data.getSize()));
    return xml != nullptr ? juce::ValueTree::fromXml(*xml) : juce::ValueTree {};
}

juce::String encode(const juce::ValueTree& state)
{
    if (const auto xml = state.createXml())
    {
        juce::MemoryBlock data;
        juce::AudioProcessor::copyXmlToBinary(*xml, data);
        return data.toBase64Encoding();
    }
    return {};
}

namespace
{
bool isLocalNavigation(const juce::Identifier& key)
{
    const auto name = key.toString();
    return name.startsWith("ava.editor.ui.scroll.")
        || name.startsWith("ava.editor.ui.collapsed.")
        || (name.startsWith("ava.crossover.") && name.endsWith(".page_scroll_y"));
}

void removeLocalNavigation(juce::ValueTree state)
{
    for (int index = state.getNumProperties(); --index >= 0;)
    {
        const auto key = state.getPropertyName(index);
        if (isLocalNavigation(key)) state.removeProperty(key, nullptr);
    }
    for (auto child : state) removeLocalNavigation(child);
}

const auto same = [](const juce::var& a, const juce::var& b)
{
    return juce::JSON::toString(a, true) == juce::JSON::toString(b, true);
};

// Stack records have explicit IDs. Merge their fields and retain local
// entries when the sender edits one existing slot or module copy.
juce::var mergeJson(const juce::var& before, const juce::var& after, const juce::var& target)
{
    if (same(before, after)) return target;
    if (const auto* fresh = after.getDynamicObject())
    {
        const auto* old = before.getDynamicObject();
        const auto* current = target.getDynamicObject();
        if (old == nullptr || current == nullptr) return target;
        auto result = juce::var(current->clone().release());
        auto& object = *result.getDynamicObject();
        for (const auto& entry : old->getProperties())
            if (!fresh->hasProperty(entry.name)) object.removeProperty(entry.name);
        for (const auto& entry : fresh->getProperties())
        {
            const auto previous = old->getProperty(entry.name);
            if (same(previous, entry.value)) continue;
            if (entry.name == juce::Identifier("state"))
            {
                auto nested = decode(current->getProperty(entry.name).toString());
                if (merge(decode(previous.toString()), decode(entry.value.toString()), nested))
                    object.setProperty(entry.name, encode(nested));
            }
            else if (!old->hasProperty(entry.name)) object.setProperty(entry.name, entry.value);
            else object.setProperty(entry.name, mergeJson(previous, entry.value, current->getProperty(entry.name)));
        }
        return result;
    }

    const auto* old = before.getArray();
    const auto* fresh = after.getArray();
    const auto* current = target.getArray();
    if (old == nullptr || fresh == nullptr || current == nullptr)
    {
        if (before.isInt() && after.isInt() && target.isInt()
            && int(after) > int(before) && int(target) > int(before))
            return juce::jmax(int(after), int(target));
        return after;
    }

    const auto id = [](const juce::var& item) { return item.getProperty("id", {}); };
    const auto keyed = [&](const auto& array)
    {
        return std::all_of(array.begin(), array.end(), [&](const auto& item) { return !id(item).isVoid(); });
    };
    if (!keyed(*old) || !keyed(*fresh) || !keyed(*current))
    {
        if (old->size() != fresh->size() || current->size() != old->size())
            return same(before, target) ? after : target;
        juce::Array<juce::var> result;
        for (int index = 0; index < fresh->size(); ++index)
            result.add(mergeJson((*old)[index], (*fresh)[index], (*current)[index]));
        return result;
    }

    const auto find = [&](const auto& array, const juce::var& key)
    {
        for (int index = 0; index < array.size(); ++index)
            if (id(array[index]) == key) return index;
        return -1;
    };
    auto result = *current;
    for (const auto& item : *old)
        if (find(*fresh, id(item)) < 0)
            if (const auto index = find(result, id(item)); index >= 0)
                result.remove(index);
    for (int i = 0; i < fresh->size(); ++i)
    {
        const auto& item = fresh->getReference(i);
        const auto previous = find(*old, id(item));
        const auto local = find(result, id(item));
        if (previous >= 0 && local >= 0)
        {
            if (item.getProperty("type", {}) == result[local].getProperty("type", {})
                && item.getProperty("created", {}) == result[local].getProperty("created", {}))
                result.set(local, mergeJson((*old)[previous], item, result[local]));
        }
        else if (previous < 0 && local < 0)
        {
            const auto anchor = i > 0 ? find(result, id((*fresh)[i - 1])) : -1;
            const auto state = decode(item.getProperty("state", {}).toString());
            if (state.isValid())
            {
                removeLocalNavigation(state);
                auto copy = item.getDynamicObject()->clone();
                copy->setProperty("state", encode(state));
                result.insert(anchor + 1, juce::var(copy.release()));
            }
            else result.insert(anchor + 1, item);
        }
    }
    juce::Array<juce::var> oldOrder, newOrder;
    for (const auto& item : *old)
        if (find(*fresh, id(item)) >= 0) oldOrder.add(id(item));
    for (const auto& item : *fresh)
        if (find(*old, id(item)) >= 0) newOrder.add(id(item));
    if (oldOrder != newOrder)
    {
        juce::Array<juce::var> ordered;
        for (const auto& item : *fresh)
            if (const auto i = find(result, id(item)); i >= 0) ordered.add(result[i]);
        int next = 0;
        for (int i = 0; i < result.size(); ++i)
            if (find(*fresh, id(result[i])) >= 0) result.set(i, ordered[next++]);
    }
    return result;
}

juce::String childKey(const juce::ValueTree& child, int index)
{
    for (const auto* key : {"id", "index"})
        if (child.hasProperty(key))
            return child.getType().toString() + ":" + key + ":" + child.getProperty(key).toString();
    return child.getType().toString() + ":" + juce::String(index);
}

auto indexChildren(const juce::ValueTree& tree)
{
    std::map<juce::String, juce::ValueTree> result;
    int index = 0;
    for (const auto child : tree) result.emplace(childKey(child, index++), child);
    return result;
}

juce::String mergeList(const juce::String& before, const juce::String& after, const juce::String& target)
{
    const auto split = [](const juce::String& text)
    {
        auto list = juce::StringArray::fromTokens(text, "|", "");
        list.removeEmptyStrings();
        return list;
    };
    const auto old = split(before), fresh = split(after);
    auto current = split(target);
    for (const auto& value : old)
        if (!fresh.contains(value)) current.removeString(value);
    for (int i = 0; i < fresh.size(); ++i)
        if (!old.contains(fresh[i]) && !current.contains(fresh[i]))
            current.insert(i > 0 ? current.indexOf(fresh[i - 1]) + 1 : 0, fresh[i]);
    auto reordered = fresh;
    for (int i = reordered.size(); --i >= 0;)
        if (!current.contains(reordered[i])) reordered.remove(i);
    int next = 0;
    for (int i = 0; i < current.size(); ++i)
        if (fresh.contains(current[i])) current.set(i, reordered[next++]);
    return current.joinIntoString("|");
}

juce::String mergeStacks(const juce::String& before, const juce::String& after, const juce::String& target)
{
    auto old = juce::JSON::parse(before), fresh = juce::JSON::parse(after);
    const auto local = juce::JSON::parse(target);
    const auto oldBands = old.getProperty("bands", {});
    const auto freshBands = fresh.getProperty("bands", {});
    const auto localBands = local.getProperty("bands", {});
    if (oldBands.getArray() == nullptr || freshBands.getArray() == nullptr || localBands.getArray() == nullptr)
        return target;

    std::map<int, int> uids;
    std::set<int> used;
    int next = int(local.getProperty("nextId", 0));
    const auto visit = [](const juce::var& bands, const auto& callback)
    {
        for (int band = 0; band < bands.size(); ++band)
        {
            const auto slots = bands[band].getProperty("slots", {});
            for (int index = 0; index < slots.size(); ++index) callback(band, slots[index]);
        }
    };
    visit(localBands, [&](int, const juce::var& slot) { used.insert(int(slot.getProperty("id", -1))); });
    const auto map = [&](int band, const juce::var& slot)
    {
        const int uid = slot.getProperty("id", -1);
        if (uids.contains(uid)) return;
        const auto slots = localBands[band].getProperty("slots", {});
        for (int i = 0; i < slots.size(); ++i)
            if (slots[i].getProperty("type", {}) == slot.getProperty("type", {})
                && slots[i].getProperty("created", {}) == slot.getProperty("created", {}))
            {
                uids.emplace(uid, int(slots[i].getProperty("id", -1)));
                return;
            }
        while (used.contains(next)) ++next;
        uids.emplace(uid, next++);
    };
    visit(oldBands, map);
    visit(freshBands, map);
    const auto rewrite = [&](juce::var& state, const juce::var& bands)
    {
        const auto setUid = [&](const juce::var& slot)
        {
            if (auto* object = slot.getDynamicObject())
                if (const auto id = uids.find(int(object->getProperty("id"))); id != uids.end())
                    object->setProperty("id", id->second);
        };
        visit(bands, [&](int, const juce::var& slot) { setUid(slot); });
        const auto copies = state.getProperty("copies", {});
        for (int i = 0; i < copies.size(); ++i) setUid(copies[i]);
    };
    rewrite(old, oldBands);
    rewrite(fresh, freshBands);
    old.getDynamicObject()->setProperty("nextId", local.getProperty("nextId", 0));
    if (fresh.getProperty("copies", {}).size() > old.getProperty("copies", {}).size())
        fresh.getDynamicObject()->setProperty("nextId", next);
    else fresh.getDynamicObject()->setProperty("nextId", local.getProperty("nextId", 0));
    return juce::JSON::toString(mergeJson(old, fresh, local), true);
}
}

bool merge(const juce::ValueTree& before, const juce::ValueTree& after, juce::ValueTree& target)
{
    if (!before.isValid() || !after.hasType(before.getType()) || !target.hasType(before.getType())
        || before.isEquivalentTo(after)) return false;
    bool changed = false;
    for (int index = 0; index < before.getNumProperties(); ++index)
    {
        const auto key = before.getPropertyName(index);
        if (!isLocalNavigation(key) && !after.hasProperty(key) && target.hasProperty(key))
        {
            target.removeProperty(key, nullptr);
            changed = true;
        }
    }
    for (int index = 0; index < after.getNumProperties(); ++index)
    {
        const auto key = after.getPropertyName(index);
        if (isLocalNavigation(key)) continue;
        const auto& old = before.getProperty(key);
        const auto& value = after.getProperty(key);
        const auto& local = target.getProperty(key);
        if (old == value || local == value) continue;
        juce::var updated = value;
        if (key == juce::Identifier(AvaAudioProcessor::moduleStackStateKey))
            updated = mergeStacks(old.toString(), value.toString(), local.toString());
        else if (key == juce::Identifier("ava.editor.ui.quick_access")
                 || key == juce::Identifier("ava.editor.ui.pinned_routing_order"))
            updated = mergeList(old.toString(), value.toString(), local.toString());
        else if (key == juce::Identifier(AvaAudioProcessor::eqlModuleStateKey)
                 || key == juce::Identifier(AvaAudioProcessor::dynModuleStateKey)
                 || key == juce::Identifier(AvaAudioProcessor::blockChainModuleStateKey))
        {
            auto embedded = decode(local.toString());
            if (!merge(decode(old.toString()), decode(value.toString()), embedded)) continue;
            updated = encode(embedded);
        }
        if (local != updated)
        {
            target.setProperty(key, updated, nullptr);
            changed = true;
        }
    }

    const auto oldChildren = indexChildren(before);
    const auto newChildren = indexChildren(after);
    const auto localChildren = indexChildren(target);
    for (const auto& [key, child] : oldChildren)
        if (!newChildren.contains(key))
            if (const auto local = localChildren.find(key); local != localChildren.end())
            {
                target.removeChild(local->second, nullptr);
                changed = true;
            }
    int index = 0;
    for (const auto child : after)
    {
        const auto key = childKey(child, index);
        const auto previous = oldChildren.find(key);
        const auto found = localChildren.find(key);
        if (previous != oldChildren.end() && found != localChildren.end())
        {
            auto local = found->second;
            changed |= merge(previous->second, child, local);
        }
        else if (previous == oldChildren.end() && found == localChildren.end())
        {
            const auto added = child.createCopy();
            removeLocalNavigation(added);
            target.addChild(added, index, nullptr);
            changed = true;
        }
        ++index;
    }
    return changed;
}
}
