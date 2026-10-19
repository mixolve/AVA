#pragma once

#include "Splitter.h"
#include "DynControlGroups.h"
#include "../modules/tls/BlockOrder.h"
#include "../modules/tls/GainOrder.h"
#include "../modules/shared/StateValidation.h"

#include <JuceHeader.h>

#include <array>
#include <limits>
#include <cmath>

namespace crossover_ui
{
inline juce::String makeStatePropertyName(const juce::String& moduleKey, const juce::String& property)
{
    return "ava.crossover." + moduleKey + "." + property;
}

inline juce::Identifier makeStatePropertyId(const juce::String& moduleKey, const juce::String& property)
{
    return juce::Identifier { makeStatePropertyName(moduleKey, property) };
}

inline bool isCurrentStateProperty(const juce::Identifier& property, const juce::String& moduleKey)
{
    constexpr std::array<const char*, 6> fixedProperties {
        "auto_solo_enabled",
        "manual_solo_inclusive",
        "crossover_settings_active",
        "visible_band_index",
        "page_scroll_y",
        "has_ui_state"
    };

    for (const auto* name : fixedProperties)
        if (property == makeStatePropertyId(moduleKey, name))
            return true;

    for (size_t rangeIndex = 0; rangeIndex < ava::crossover::Splitter::numRanges; ++rangeIndex)
        if (property == makeStatePropertyId(moduleKey,
                                            "manual_solo." + juce::String(static_cast<int>(rangeIndex))))
            return true;

    if (moduleKey == "tls")
        for(size_t range=0;range<ava::crossover::Splitter::numRanges;++range)
            if(property==tls::blocks::stateKey(range) || property==tls::gain::stateKey(range)) return true;

    if (moduleKey == "dyn")
        for (size_t range = 0; range < ava::crossover::Splitter::numRanges; ++range)
            for (const auto& group : dynControlGroups)
                if (property == makeStatePropertyId(moduleKey, "group." + juce::String(int(range)) + "." + group.key))
                    return true;

    return false;
}

inline bool hasExactIntegerValue(const juce::var& value, const int minimum, const int maximum)
{
    const auto text = value.toString();
    const auto parsed = text.getIntValue();
    return text == juce::String(parsed) && parsed >= minimum && parsed <= maximum;
}

inline bool hasExactBooleanValue(const juce::var& value)
{
    const auto text = value.toString();
    return text == "0" || text == "1";
}

inline bool isCurrentStatePropertyValue(const juce::ValueTree& state,
                                        const juce::Identifier& property,
                                        const juce::String& moduleKey)
{
    for (const auto* name : { "auto_solo_enabled",
                              "manual_solo_inclusive",
                              "crossover_settings_active",
                              "has_ui_state" })
    {
        const auto id = makeStatePropertyId(moduleKey, name);
        if (property == id)
            return hasExactBooleanValue(state.getProperty(id));
    }

    const auto visibleRangeId = makeStatePropertyId(moduleKey, "visible_band_index");
    if (property == visibleRangeId)
        return hasExactIntegerValue(state.getProperty(visibleRangeId),
                                    0,
                                    static_cast<int>(ava::crossover::Splitter::numRanges - 1));

    const auto pageScrollId = makeStatePropertyId(moduleKey, "page_scroll_y");
    if (property == pageScrollId)
        return hasExactIntegerValue(state.getProperty(pageScrollId), 0, std::numeric_limits<int>::max());

    for (size_t rangeIndex = 0; rangeIndex < ava::crossover::Splitter::numRanges; ++rangeIndex)
    {
        const auto manualSoloId = makeStatePropertyId(
            moduleKey, "manual_solo." + juce::String(static_cast<int>(rangeIndex)));

        if (property == manualSoloId)
            return hasExactBooleanValue(state.getProperty(manualSoloId));
    }

    if (moduleKey == "tls") {
        for(size_t range=0;range<ava::crossover::Splitter::numRanges;++range) {
            if(property==tls::blocks::stateKey(range)) {
                auto order=tls::blocks::defaultOrder;
                return tls::blocks::parse(state.getProperty(property).toString(),order);
            }
            if(property==tls::gain::stateKey(range)) {
                auto order=tls::gain::defaultOrder;
                return tls::gain::parse(state.getProperty(property).toString(),order);
            }
        }
    }

    if (moduleKey == "dyn")
        for (size_t range = 0; range < ava::crossover::Splitter::numRanges; ++range)
            for (const auto& group : dynControlGroups)
                if (property == makeStatePropertyId(moduleKey, "group." + juce::String(int(range)) + "." + group.key))
                    return hasExactBooleanValue(state.getProperty(property));

    return false;
}

inline bool hasCurrentStateProperties(const juce::ValueTree& state, const juce::String& moduleKey)
{
    if (moduleKey == "tls")
        for (size_t range = 0; range < ava::crossover::Splitter::numRanges; ++range)
            if (! state.hasProperty(tls::blocks::stateKey(range))
                || ! state.hasProperty(tls::gain::stateKey(range)))
                return false;

    for (int propertyIndex = 0; propertyIndex < state.getNumProperties(); ++propertyIndex)
    {
        const auto property = state.getPropertyName(propertyIndex);

        if (! isCurrentStateProperty(property, moduleKey)
            || ! isCurrentStatePropertyValue(state, property, moduleKey))
            return false;
    }

    return true;
}
}
