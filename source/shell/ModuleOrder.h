#pragma once
#include "../modules/shared/StateValidation.h"
#include "../modules/shared/BlockCatalog.h"
#include "../modules/shared/OrderCodec.h"
#include "../modules/eql/Descriptor.h"
#include "../modules/dyn/Descriptor.h"
#include <array>

namespace module_bypass
{
struct Description { int id; const char* name; const char* bypassStateKey; };
inline constexpr std::array<Description, 3> descriptions {{{0,"CHAIN","ava.module_bypass.chain"},{1,"EQL","ava.module_bypass.eql"},{2,"DYN","ava.module_bypass.dyn"}}};
}
namespace module_order
{
inline constexpr int eql = 12, dyn = 13, sibil = 14;
inline constexpr bool isChain(int type) { return (type >= 0 && type < eql) || type == sibil; }
inline constexpr int chainIndex(int type) { return type == sibil ? 12 : type; }
inline constexpr int fromChain(int index) { return index == 12 ? sibil : index; }
// Per-band EQL/DYN hidden bits represent DSP bypass, not MODULES visibility.
// New projects start unbypassed; saved project flags are still restored.
inline constexpr unsigned defaultHiddenMask = 0u;
using Order = std::array<int, 14>;
inline constexpr Order defaultOrder {eql,0,1,2,3,4,5,6,7,8,9,10,11,dyn};
// Each BAND has its own independently saved module order.
inline juce::String bandStateKey(size_t rangeIndex)
{
    return "ava.block_order.band-" + juce::String(int(rangeIndex + 1));
}
inline juce::String bandHiddenKey(size_t rangeIndex, int moduleId)
{
    return "ava.block_hidden.band-" + juce::String(int(rangeIndex + 1))
        + (moduleId == eql ? ".eql" : ".dyn");
}
struct Description { int id; const char* name; };
inline constexpr std::array<Description, 15> descriptions {{
    {0, ava::modules::chain::blocks::descriptions[0].name},
    {1, ava::modules::chain::blocks::descriptions[1].name},
    {2, ava::modules::chain::blocks::descriptions[2].name},
    {3, ava::modules::chain::blocks::descriptions[3].name},
    {4, ava::modules::chain::blocks::descriptions[4].name},
    {5, ava::modules::chain::blocks::descriptions[5].name},
    {6, ava::modules::chain::blocks::descriptions[6].name},
    {7, ava::modules::chain::blocks::descriptions[7].name},
    {8, ava::modules::chain::blocks::descriptions[8].name},
    {9, ava::modules::chain::blocks::descriptions[9].name},
    {10, ava::modules::chain::blocks::descriptions[10].name},
    {11, ava::modules::chain::blocks::descriptions[11].name},
    {eql, ava::modules::eql::displayName},
    {dyn, ava::modules::dyn::displayName},
    {sibil, ava::modules::sibil::description.name}
}};
using CatalogOrder = std::array<int, descriptions.size()>;
inline constexpr CatalogOrder defaultCatalogOrder {eql,dyn,sibil,0,1,2,3,4,5,6,7,8,9,10,11};

using ava::modules::order::valid;
using ava::modules::order::pack;
using ava::modules::order::serialise;
using ava::modules::order::parse;
inline Order unpack(uint64_t packed) noexcept
{
    return ava::modules::order::unpackOrder(packed, defaultOrder);
}
}
