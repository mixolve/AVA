#include "Editor.h"
#include "Page.h"
#include "UiSupport.h"

#include "../shell/ChoiceControl.h"
#include "../shell/ParameterControl.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace crossover_ui;

size_t CrossoverModuleComponent::getActiveSplitCount() const
{
    if (activeSplitCountParameter == nullptr)
        return numRanges - 1;

    return static_cast<size_t>(juce::jlimit(0,
                                           static_cast<int>(numRanges - 1),
                                           static_cast<int>(std::round(activeSplitCountParameter->convertFrom0to1(
                                               activeSplitCountParameter->getValue())))));
}

size_t CrossoverModuleComponent::getActiveRangeCount() const
{
    return getActiveSplitCount() + 1;
}

bool CrossoverModuleComponent::setParameterNormalisedValue(juce::RangedAudioParameter& parameter, const float normalisedValue)
{
    const auto value = juce::jlimit(0.0f, 1.0f, normalisedValue);
    const auto wasDifferent = std::abs(parameter.getValue() - value) > 1.0e-6f;

    if (! wasDifferent)
        return false;

    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(value);
    parameter.endChangeGesture();

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();

    return true;
}

void CrossoverModuleComponent::clearFocus()
{
    if (config.clearKeyboardFocus != nullptr)
        config.clearKeyboardFocus();
    else
        clearKeyboardFocus(*this);
}
