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

void CrossoverModuleComponent::setExternalCrossoverRange(const size_t rangeIndex)
{
    crossoverSettingsActive = false;
    visibleRangeIndex = juce::jmin(rangeIndex, getActiveRangeCount() - 1);
    updatePageVisibility();
    resized();
}

void CrossoverModuleComponent::selectCrossoverRange(const size_t rangeIndex)
{
    visibleRangeIndex = juce::jmin(rangeIndex, getActiveRangeCount() - 1);
    crossoverSettingsActive = false;

    if (autoSoloEnabled)
        manualSoloMask = {};

    updateMonitorButtons();
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
    crossoverSettingsActive = false;
    const auto shouldSoloRange = ! manualSoloMask[visibleRangeIndex];

    if (! manualSoloInclusive)
        manualSoloMask = {};

    manualSoloMask[visibleRangeIndex] = shouldSoloRange;
    updateMonitorButtons();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    clearFocus();
}

void CrossoverModuleComponent::changeActiveSplitCount(const int delta)
{
    if (activeSplitCountParameter == nullptr)
        return;

    const auto currentValue = static_cast<int>(
        std::round(activeSplitCountParameter->convertFrom0to1(activeSplitCountParameter->getValue())));
    const auto newValue = juce::jlimit(0, static_cast<int>(numRanges - 1), currentValue + delta);

    if (newValue == currentValue)
        return;

    setParameterPlainValue(config.makeCrossoverSplitCountParameterId(), static_cast<float>(newValue));
    visibleRangeIndex = juce::jmin(visibleRangeIndex, getActiveRangeCount() - 1);
    manualSoloMask = {};

    for (size_t splitIndex = 0; splitIndex < static_cast<size_t>(newValue); ++splitIndex)
        constrainSplitFrequency(splitIndex);

    if (crossoverSettingsPage != nullptr)
        crossoverSettingsPage->refreshExternalState();

    updateMonitorButtons();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    resized();
}

void CrossoverModuleComponent::showCrossoverSettings()
{
    crossoverSettingsActive = true;
    updateMonitorButtons();
    updatePageVisibility();
    syncMonitorParameters();
    saveUiState();
    resized();
    notifyPageChanged();
    clearFocus();
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

    updateMonitorButtons();
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

    updateMonitorButtons();
    syncMonitorParameters();
    saveUiState();
}

void CrossoverModuleComponent::syncMonitorParameters()
{
    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        auto* parameter = soloParameters[rangeIndex];

        if (parameter == nullptr)
            continue;

        const auto enabled = rangeIndex < getActiveRangeCount()
            && ((autoSoloEnabled && ! crossoverSettingsActive && rangeIndex == visibleRangeIndex)
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

void CrossoverModuleComponent::updateMonitorButtons()
{
    const auto activeRangeCount = getActiveRangeCount();

    for (size_t rangeIndex = 0; rangeIndex < numRanges; ++rangeIndex)
    {
        if (auto* button = monitorButtons[rangeIndex].get())
        {
            const auto isActiveRange = rangeIndex < activeRangeCount;
            button->setVisible(true);
            button->setEnabled(isActiveRange);
            button->setAlpha(1.0f);
            button->setToggleState(! crossoverSettingsActive && rangeIndex == visibleRangeIndex, juce::dontSendNotification);
        }

        if (auto* page = rangePages[rangeIndex].get())
            page->refreshExternalState();
    }

    if (auto* button = monitorButtons[numRanges].get())
    {
        button->setVisible(true);
        button->setEnabled(true);
        button->setAlpha(1.0f);
        button->setToggleState(crossoverSettingsActive, juce::dontSendNotification);
    }
}

