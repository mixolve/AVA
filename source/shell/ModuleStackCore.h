#pragma once

#include <array>
#include <algorithm>
#include <cstdint>
#include <vector>

// A slot is a position in a single crossover band's actual signal path.
// IDs 0..13 are the original processors; copy IDs start at 14 and are unique
// within the plugin instance. Added types have reserved negative original UIDs;
// 'created' is monotonic per band and type.
namespace ava::module_stack {
// Existing positive original/copy UIDs never move when a type is added.
inline constexpr int originalCount = 14;
inline constexpr int typeCount = 15;
inline constexpr int originalUid(int type) { return type < originalCount ? type : originalCount - type - 2; }
inline constexpr int maximumSlotsPerBand = 64;
struct Slot {
    int uid = -1;
    int type = 0;
    int created = 1;
    bool enabled = true; // Independent BP (false = bypassed)
    bool shown = false;  // MODULES activation and editor visibility
};
// Only a switched-on MODULES tile that is not BP-bypassed may run DSP.
// Collapsing the MODULES section itself does not change this state.
inline bool processingActive(const Slot& slot) noexcept { return slot.shown && slot.enabled; }
using Order = std::vector<Slot>;
template<class ModuleOrder>
inline Order initial(const ModuleOrder& order) {
    Order result;
    result.reserve(typeCount);
    for (auto type : order) result.push_back({originalUid(type), type, 1, true, false});
    for (int type=originalCount; type<typeCount; ++type)
        result.push_back({originalUid(type),type,1,true,false});
    return result;
}
inline const Slot* find(const Order& order, int uid) {
    const auto it = std::find_if(order.begin(),order.end(),[uid](const Slot& s){ return s.uid == uid; });
    return it == order.end() ? nullptr : &*it;
}
inline Slot* find(Order& order,int uid) {
    const auto it = std::find_if(order.begin(),order.end(),[uid](const Slot& s){ return s.uid == uid; });
    return it == order.end() ? nullptr : &*it;
}
inline bool insertCopy(Order& order,int sourceUid,int newUid,int serial) {
    if (newUid < originalCount || order.size() >= maximumSlotsPerBand
        || find(order, newUid) != nullptr) return false;
    const auto it = std::find_if(order.begin(),order.end(),[sourceUid](const Slot& s){return s.uid==sourceUid;});
    if (it == order.end() || serial < 2) return false;
    const auto insertion = static_cast<size_t>(std::distance(order.begin(),it));
    const int type = it->type;
    order.insert(order.begin()+std::ptrdiff_t(insertion+1), Slot{newUid,type,serial,true,true});
    return true;
}
inline bool remove(Order& order,int uid) {
    const auto it=std::find_if(order.begin(),order.end(),[uid](const Slot& s){return s.uid==uid;});
    if(it==order.end())return false;
    order.erase(it);return true;
}
inline bool reorder(Order& order,const std::vector<int>& ids) {
    if (ids.size()!=order.size())return false;
    // Ignore an unchanged order: it must not trigger a render-graph rebuild.
    if (std::equal(ids.begin(), ids.end(), order.begin(),
                   [](int uid, const Slot& slot) { return uid == slot.uid; })) return false;
    Order changed;changed.reserve(order.size());
    for(int uid:ids) {
        const auto* source=find(order,uid);
        if(source==nullptr || find(changed,uid)!=nullptr)return false;
        changed.push_back(*source);
    }
    order=std::move(changed);return true;
}
// Display position among currently existing slots of the same module type.
// Persistent created/UID identities remain independent from presentation.
inline int displayOrdinal(const Order& order, int uid) {
    const auto* target = find(order, uid);
    if (target == nullptr) return 0;
    int ordinal = 0;
    for (const auto& slot : order) {
        if (slot.type == target->type) ++ordinal;
        if (slot.uid == uid) return ordinal;
    }
    return 0;
}
inline int nextSerial(const Order& order,int type,int previous=1) {
    for(const auto& slot:order)if(slot.type==type)previous=std::max(previous,slot.created);
    return previous+1;
}
}
