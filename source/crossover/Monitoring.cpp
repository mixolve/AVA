#include "Editor.h"
#include "Page.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/ParameterControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

void CrossoverModuleComponent::setExternalCrossoverRange(const size_t rangeIndex)
{
    visibleRangeIndex = juce::jmin(rangeIndex, getActiveRangeCount() - 1);
    updateRangeNavigation();
    updatePageVisibility();
    resized();
}

void CrossoverModuleComponent::selectCrossoverRange(const size_t rangeIndex)
{
    visibleRangeIndex = juce::jmin(rangeIndex, getActiveRangeCount() - 1);

    if (autoSoloEnabled)
        manualSoloMask = {};

    updateRangeNavigation();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    resized();
    notifyPageChanged();
    clearFocus();
}

void CrossoverModuleComponent::toggleManualSolo(const size_t rangeIndex)
{
    if (autoSoloEnabled || getActiveRangeCount() <= 1 || rangeIndex >= getActiveRangeCount())
        return;

    visibleRangeIndex = juce::jmin(rangeIndex, numRanges - 1);
    if (auto* parameter = soloParameters[visibleRangeIndex])
        setParameterNormalisedValue(*parameter, parameter->getValue() >= 0.5f ? 0.0f : 1.0f);
    synchroniseManualSoloMaskFromParameters();
    updateRangeNavigation();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    clearFocus();
}

void CrossoverModuleComponent::activeSplitCountChanged()
{
    visibleRangeIndex = juce::jmin(visibleRangeIndex, getActiveRangeCount() - 1);
    manualSoloMask = {};

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateRangeNavigation();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    resized();
    notifyPageChanged();
}

void CrossoverModuleComponent::notifyPageChanged()
{
    if (config.onPageChanged != nullptr)
        config.onPageChanged();
}

void CrossoverModuleComponent::setAutoSoloEnabled(const bool shouldBeEnabled)
{
    autoSoloEnabled = shouldBeEnabled;
    manualSoloMask = {};

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateRangeNavigation();
    syncMonitorParameters();
    saveUiState();
}

void CrossoverModuleComponent::setManualSoloInclusive(const bool shouldBeInclusive)
{
    manualSoloInclusive = shouldBeInclusive;

    if (! manualSoloInclusive)
    {
        size_t soloRangeToKeep = visibleRangeIndex;

        if (! manualSoloMask[soloRangeToKeep])
        {
            for (size_t rangeIndex = 0; rangeIndex < getActiveRangeCount(); ++rangeIndex)
            {
                if (manualSoloMask[rangeIndex])
                {
                    soloRangeToKeep = rangeIndex;
                    break;
                }
            }
        }

        const auto shouldKeepSolo = soloRangeToKeep < getActiveRangeCount() && manualSoloMask[soloRangeToKeep];
        manualSoloMask = {};

        if (shouldKeepSolo)
            manualSoloMask[soloRangeToKeep] = true;
    }

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateRangeNavigation();
    syncMonitorParameters();
    saveUiState();
}

void CrossoverModuleComponent::syncMonitorParameters()
{
    // Publish the mode before writing its parameter mask, so every writer sees
    // the same exclusivity policy, including a mode switch in this editor.
    setBool(valueTreeState.state, config.moduleKey, "auto_solo_enabled", autoSoloEnabled);
    setBool(valueTreeState.state, config.moduleKey, "manual_solo_inclusive", manualSoloInclusive);
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto* parameter = soloParameters[rangeIndex];

        if (parameter == nullptr)
            continue;

        const auto enabled = rangeIndex < getActiveRangeCount()
            && ((autoSoloEnabled && rangeIndex == visibleRangeIndex)
                || (! autoSoloEnabled && manualSoloMask[rangeIndex]));
        setParameterNormalisedValue(*parameter, parameter->convertTo0to1(enabled ? 1.0f : 0.0f));
    }

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();
}

void CrossoverModuleComponent::synchroniseManualSoloMaskFromParameters()
{
    if (autoSoloEnabled)
        return;

    const auto activeRangeCount = getActiveRangeCount();

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
        manualSoloMask[rangeIndex] = rangeIndex < activeRangeCount && isRangeSoloEnabled(rangeIndex);
}

bool CrossoverModuleComponent::isRangeSoloEnabled(const size_t rangeIndex) const noexcept
{
    return rangeIndex < soloParameters.size()
        && soloParameters[rangeIndex] != nullptr
        && soloParameters[rangeIndex]->getValue() >= 0.5f;
}

void CrossoverModuleComponent::updateRangeNavigation()
{
    const auto activeRangeCount = getActiveRangeCount();
    for (size_t range = 0; range < numRanges; ++range)
    {
        if (auto* control = bandControls[range].get())
        {
            // With XOV=0 there is no other band to navigate to. Disable the
            // entire BAND-1 control, including its SL host-slot indicator.
            // Restore normal interaction as soon as XOV creates another band.
            const auto selectable = activeRangeCount > 1 && range < activeRangeCount;
            control->setInteractionEnabled(selectable);
            // Keep Ctrl+click docking available even when navigation is inactive.
            control->setInterceptsMouseClicks(true, true);
            control->getProperties().set("textButtonBaseColour", juce::int64(range == visibleRangeIndex ? 0xffffffff : 0xffbbbbbb));
            control->repaint();
        }
        if (auto* page = rangePages[range].get()) page->refreshExternalState();
    }
    updateSoloState();
}
