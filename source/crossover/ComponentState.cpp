#include "Component.h"
#include "Page.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/LocalParameterControl.h"
#include "../shell/ParameterControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

void CrossoverModuleComponent::loadUiState()
{
    auto& state = valueTreeState.state;
    autoSoloEnabled = getBool(state, config.moduleKey, "auto_solo_enabled", false);
    manualSoloInclusive = getBool(state, config.moduleKey, "manual_solo_inclusive", false);
    crossoverSettingsActive = getBool(state, config.moduleKey, "crossover_settings_active", false);
    visibleRangeIndex = static_cast<size_t>(juce::jlimit(0,
                                                       static_cast<int>(numRanges - 1),
                                                       getInt(state, config.moduleKey, "visible_band_index", 0)));
    restoredPageScrollY = juce::jmax(0, getInt(state, config.moduleKey, "page_scroll_y", 0));
    pageScrollRestored = false;

    manualSoloMask = {};

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        manualSoloMask[rangeIndex] = getBool(state,
                                            config.moduleKey,
                                            "manual_solo." + juce::String(static_cast<int>(rangeIndex)),
                                            false);
    }

    uiStateLoaded = getBool(state, config.moduleKey, "has_ui_state", false);
    uiStateSignature = getUiStateSignature();
}

void CrossoverModuleComponent::saveUiState()
{
    auto& state = valueTreeState.state;

    if (pageScrollRestored)
        restoredPageScrollY = pageViewport.getViewPositionY();
    setBool(state, config.moduleKey, "auto_solo_enabled", autoSoloEnabled);
    setBool(state, config.moduleKey, "manual_solo_inclusive", manualSoloInclusive);
    setBool(state, config.moduleKey, "crossover_settings_active", crossoverSettingsActive);
    setInt(state, config.moduleKey, "visible_band_index", static_cast<int>(visibleRangeIndex));
    setInt(state, config.moduleKey, "page_scroll_y", restoredPageScrollY);
    setBool(state, config.moduleKey, "has_ui_state", true);

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        setBool(state,
                config.moduleKey,
                "manual_solo." + juce::String(static_cast<int>(rangeIndex)),
                manualSoloMask[rangeIndex]);

    uiStateSignature = getUiStateSignature();
}

bool CrossoverModuleComponent::restoreUiStateIfChanged()
{
    if (getUiStateSignature() == uiStateSignature)
        return false;

    loadUiState();
    visibleRangeIndex = juce::jmin(visibleRangeIndex, getActiveRangeCount() - 1);
    updateMonitorButtons();
    updatePageVisibility();
    return true;
}

juce::String CrossoverModuleComponent::getUiStateSignature() const
{
    const auto& state = valueTreeState.state;
    juce::StringArray values;
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "has_ui_state"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "auto_solo_enabled"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "manual_solo_inclusive"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "crossover_settings_active"), false).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "visible_band_index"), 0).toString());
    values.add(state.getProperty(makeStatePropertyId(config.moduleKey, "page_scroll_y"), 0).toString());

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        values.add(state.getProperty(makeStatePropertyId(config.moduleKey,
                                                          "manual_solo." + juce::String(static_cast<int>(rangeIndex))),
                                      false).toString());
    }

    return values.joinIntoString("|");
}
