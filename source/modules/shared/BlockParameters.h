#pragma once

#include "BlockParameterTypes.h"
#include "../listen/ParameterCatalog.h"
#include "../gain/ParameterCatalog.h"
#include "../delay/ParameterCatalog.h"
#include "../phase/ParameterCatalog.h"
#include "../panorama/ParameterCatalog.h"
#include "../shear/ParameterCatalog.h"
#include "../balance/ParameterCatalog.h"
#include "../ortho/ParameterCatalog.h"
#include "../rect/ParameterCatalog.h"
#include "../comb/ParameterCatalog.h"
#include "../shift/ParameterCatalog.h"
#include "../varispeed/ParameterCatalog.h"
#include "../sibil/ParameterCatalog.h"

namespace ava::modules::chain::parameters
{
// Shared composition only: each block declares its own metadata in its folder.
// Compose by the stable, declared parameter slot indices so each ID has one owner.
inline constexpr auto parameterSpecs = []
{
    std::array<ParameterSpec, numParameterSlots> result {};
    const auto assign = [&result] (const auto& catalog)
    {
        for (const auto& entry : catalog)
            result[toIndex(entry.slot)] = entry.spec;
    };
    assign(ava::modules::listen::parameterCatalog);
    assign(ava::modules::gain::parameterCatalog);
    assign(ava::modules::delay::parameterCatalog);
    assign(ava::modules::phase::parameterCatalog);
    assign(ava::modules::panorama::parameterCatalog);
    assign(ava::modules::shear::parameterCatalog);
    assign(ava::modules::balance::parameterCatalog);
    assign(ava::modules::ortho::parameterCatalog);
    assign(ava::modules::rect::parameterCatalog);
    assign(ava::modules::comb::parameterCatalog);
    assign(ava::modules::shift::parameterCatalog);
    assign(ava::modules::varispeed::parameterCatalog);
    assign(ava::modules::sibil::parameterCatalog);
    return result;
}();

inline constexpr auto hostLabels = []
{
    std::array<HostLabel, numParameterSlots> result {};
    const auto assign = [&result] (const auto& catalog)
    {
        for (const auto& entry : catalog)
            result[toIndex(entry.slot)] = entry.hostLabel;
    };
    assign(ava::modules::listen::parameterCatalog);
    assign(ava::modules::gain::parameterCatalog);
    assign(ava::modules::delay::parameterCatalog);
    assign(ava::modules::phase::parameterCatalog);
    assign(ava::modules::panorama::parameterCatalog);
    assign(ava::modules::shear::parameterCatalog);
    assign(ava::modules::balance::parameterCatalog);
    assign(ava::modules::ortho::parameterCatalog);
    assign(ava::modules::rect::parameterCatalog);
    assign(ava::modules::comb::parameterCatalog);
    assign(ava::modules::shift::parameterCatalog);
    assign(ava::modules::varispeed::parameterCatalog);
    assign(ava::modules::sibil::parameterCatalog);
    return result;
}();

inline constexpr bool allParameterSlotsOwnedExactlyOnce = []
{
    std::array<int, numParameterSlots> uses {};
    const auto mark = [&uses] (const auto& catalog)
    {
        for (const auto& entry : catalog)
            ++uses[toIndex(entry.slot)];
    };
    mark(ava::modules::listen::parameterCatalog);
    mark(ava::modules::gain::parameterCatalog);
    mark(ava::modules::delay::parameterCatalog);
    mark(ava::modules::phase::parameterCatalog);
    mark(ava::modules::panorama::parameterCatalog);
    mark(ava::modules::shear::parameterCatalog);
    mark(ava::modules::balance::parameterCatalog);
    mark(ava::modules::ortho::parameterCatalog);
    mark(ava::modules::rect::parameterCatalog);
    mark(ava::modules::comb::parameterCatalog);
    mark(ava::modules::shift::parameterCatalog);
    mark(ava::modules::varispeed::parameterCatalog);
    mark(ava::modules::sibil::parameterCatalog);
    for (const auto usesForSlot : uses)
        if (usesForSlot != 1) return false;
    return true;
}();
static_assert(allParameterSlotsOwnedExactlyOnce, "Every parameter slot must have exactly one owning block");
static_assert(parameterSpecs.size() == numParameterSlots);

inline constexpr std::array blockBypassSlots { ParameterSlot::listenBypass, ParameterSlot::gainBypass, ParameterSlot::delayBypass, ParameterSlot::phaseBypass, ParameterSlot::panoramaBypass, ParameterSlot::shearBypass, ParameterSlot::balanceBypass, ParameterSlot::orthoBypass, ParameterSlot::rectBypass, ParameterSlot::combCountBypass, ParameterSlot::shiftBypass, ParameterSlot::varispeedBypass, ParameterSlot::sibilBypass };
inline constexpr std::array blockHiddenSlots { ParameterSlot::listenHidden, ParameterSlot::gainHidden, ParameterSlot::delayHidden, ParameterSlot::phaseHidden, ParameterSlot::panoramaHidden, ParameterSlot::shearHidden, ParameterSlot::balanceHidden, ParameterSlot::orthoHidden, ParameterSlot::rectHidden, ParameterSlot::combHidden, ParameterSlot::shiftHidden, ParameterSlot::varispeedHidden, ParameterSlot::sibilHidden };
inline constexpr std::array gainBypassSlots { ParameterSlot::gainStereoBypass, ParameterSlot::gainLeftBypass, ParameterSlot::gainRightBypass, ParameterSlot::gainMidBypass, ParameterSlot::gainSideBypass };

inline constexpr std::array listenExclusiveSlots { ParameterSlot::listenLc, ParameterSlot::listenRc,
    ParameterSlot::listenMc, ParameterSlot::listenSc, ParameterSlot::listenLl, ParameterSlot::listenRr, ParameterSlot::listenSs };
inline constexpr std::array rectExclusiveSlots { ParameterSlot::halfPositive, ParameterSlot::halfNegative,
    ParameterSlot::fullPositive, ParameterSlot::fullNegative };

ControlPolicy getControlPolicy(const juce::AudioProcessorValueTreeState& state, size_t range, const juce::String& suffix);

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
}
