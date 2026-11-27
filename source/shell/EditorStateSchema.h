#pragma once
#include "../crossover/UiStateSchema.h"
#include "Metrics.h"
#include "ModuleOrder.h"
#include <optional>
#include <array>

namespace shell_editor_ui
{
inline juce::Identifier key(const juce::String& name) { return juce::Identifier("ava.editor.ui." + name); }

inline const juce::Identifier inactiveModulesOrderKey = key("modules_inactive_order");

inline module_order::CatalogOrder inactiveModulesOrder(const juce::ValueTree& state)
{
    auto order = module_order::defaultCatalogOrder;
    module_order::parse(state.getProperty(inactiveModulesOrderKey).toString(), order);
    return order;
}

// Persist eighths of the base size: controls and gaps stay whole pixels.
inline constexpr std::array<int, 8> zoomSteps { 6, 7, 8, 10, 12, 16, 20, 24 };
inline constexpr int defaultZoomStep = 8;
inline int zoomChoiceIndex(int step)
{
    for (int index = 0; index < int(zoomSteps.size()); ++index)
        if (step == zoomSteps[size_t(index)]) return index;
    return 2; // 100% if the property has not been initialised yet.
}
inline int zoomStepForChoice(int index)
{
    return zoomSteps[size_t(juce::jlimit(0, int(zoomSteps.size()) - 1, index))];
}
inline int zoomStep(const juce::ValueTree& state)
{
    const auto step = static_cast<int>(state.getProperty(key("zoom_step"), defaultZoomStep));
    return zoomStepForChoice(zoomChoiceIndex(step));
}
inline juce::StringArray zoomChoices()
{
    juce::StringArray choices;
    for (int step : zoomSteps)
        choices.add(juce::String(step * 12.5, step % 2 != 0 ? 1 : 0));
    return choices;
}

inline constexpr int minEditorColumns = 2;
inline constexpr int maxEditorColumns = 12;
inline int editorColumns(const juce::ValueTree& state)
{
    return juce::jlimit(minEditorColumns, maxEditorColumns,
        int(state.getProperty(key("editor_columns"), viewportControlColumns)));
}

inline int dockControlCount(const juce::ValueTree& state)
{
    return juce::jlimit(1, 12, int(state.getProperty(key("dock_count"), 1)));
}

// True when this plugin window has keyboard focus. Realtime modifier polls must
// be gated on this so that key presses meant for the host are not interpreted here.
inline bool hasPluginKeyboardFocus(const juce::Component& component)
{
    const auto* topLevel = component.getTopLevelComponent();

    if (topLevel == nullptr)
        return false;

    if (auto* peer = topLevel->getPeer())
        return peer->isFocused();

    return topLevel->hasKeyboardFocus(true);
}

inline juce::ModifierKeys pluginModifierKeys(const juce::Component& component)
{
    return hasPluginKeyboardFocus(component)
        ? juce::ModifierKeys::getCurrentModifiersRealtime()
        : juce::ModifierKeys();
}
inline std::optional<bool> validate(const juce::Identifier& property, const juce::var& value)
{
    const auto collapsedPrefix = key("collapsed.").toString();
    if (property.toString().startsWith(collapsedPrefix))
    {
        const auto id = property.toString().substring(collapsedPrefix.length());
        return id.isNotEmpty() && id.length() <= 160
            && id.containsOnly("abcdefghijklmnopqrstuvwxyz0123456789.-_")
            && crossover_ui::hasExactBooleanValue(value);
    }
    const auto selectionPrefix = key("selected.").toString();
    if (property.toString().startsWith(selectionPrefix))
    {
        const auto id = property.toString().substring(selectionPrefix.length());
        return id.isNotEmpty() && id.length() % 2 == 0 && id.containsOnly("0123456789abcdefABCDEF")
            && crossover_ui::hasExactBooleanValue(value);
    }
    if (property.toString().startsWith(key("dock.meta.").toString()))
    {
        const auto id = property.toString().substring(key("dock.meta.").toString().length());
        return id.isNotEmpty() && id.length() % 2 == 0
            && id.containsOnly("0123456789abcdefABCDEF") && value.isString()
            && value.toString().length() <= 2048;
    }
    if (property == key("quick_access"))
    {
        if (!value.isString() || value.toString().length() > 32768) return false;
        const auto order = juce::StringArray::fromTokens(value.toString(), "|", "");
        if (order.size() > 64) return false;
        for (const auto& id : order)
            if (id.isEmpty() || (id.length() % 2) != 0 || !id.containsOnly("0123456789abcdefABCDEF"))
                return false;
        return true;
    }
    if (property == inactiveModulesOrderKey)
    {
        module_order::CatalogOrder order {};
        return value.isString() && module_order::parse(value.toString(), order);
    }
    if (property == key("pinned_routing_order"))
    {
        if (!value.isString() || value.toString().length() > 704) return false;
        const auto raw = value.toString();
        const auto order = juce::StringArray::fromTokens(raw, "|", "");
        if (order.size() > 64 || (raw.isNotEmpty() && order.isEmpty())) return false;
        juce::StringArray seen;
        for (const auto& item : order)
        {
            const auto id = item.getLargeIntValue();
            if (item.isEmpty() || item.length() > 10 || !item.containsOnly("0123456789")
                || id <= 0 || id > std::numeric_limits<int>::max()
                || item != juce::String(id) || seen.contains(item)) return false;
            seen.add(item);
        }
        return order.joinIntoString("|") == raw;
    }
    if (property == key("pin_routing"))
        return crossover_ui::hasExactBooleanValue(value);
    if (property == key("editor_columns"))
        return crossover_ui::hasExactIntegerValue(value, minEditorColumns, maxEditorColumns);
    if (property == key("dock_count")) return crossover_ui::hasExactIntegerValue(value, 1, 12);
    if (property == key("zoom_step"))
    {
        for (const auto step : zoomSteps)
            if (value.toString() == juce::String(step)) return true;
        return false;
    }
    for (const auto* name : { "about.expanded", "dock_only", "dock_vertical", "routing.containers" })
        if (property == key(name)) return crossover_ui::hasExactBooleanValue(value);
    for (const auto* name : { "active_instance", "scroll.eql", "scroll.routing.x", "scroll.routing.y" })
        if (property == key(name)) return crossover_ui::hasExactIntegerValue(value, 0, std::numeric_limits<int>::max());
    for (size_t i = 0; i < ava::crossover::Splitter::numRanges; ++i)
        if (property == key("eql.expanded." + juce::String(static_cast<int>(i))))
            return crossover_ui::hasExactIntegerValue(value, -1, 63);
    return std::nullopt;
}
}
