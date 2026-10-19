#pragma once
#include "../crossover/UiStateSchema.h"
#include <optional>

namespace shell_editor_ui
{
inline juce::Identifier key(const juce::String& name) { return juce::Identifier("ava.editor.ui." + name); }
inline std::optional<bool> validate(const juce::Identifier& property, const juce::var& value)
{
    for (const auto* name : { "osc.expanded" })
        if (property == key(name)) return crossover_ui::hasExactBooleanValue(value);
    for (const auto* name : { "active_instance", "scroll.eql", "scroll.host" })
        if (property == key(name)) return crossover_ui::hasExactIntegerValue(value, 0, std::numeric_limits<int>::max());
    for (size_t i = 0; i < ava::crossover::Splitter::numRanges; ++i)
        if (property == key("eql.expanded." + juce::String(static_cast<int>(i))))
            return crossover_ui::hasExactIntegerValue(value, -1, 63);
    return std::nullopt;
}
}
