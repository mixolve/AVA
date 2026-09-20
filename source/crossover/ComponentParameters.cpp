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

bool CrossoverModuleComponent::setParameterPlainValue(const juce::String& parameterId, const float plainValue)
{
    if (auto* parameter = valueTreeState.getParameter(parameterId))
        return setParameterNormalisedValue(*parameter, parameter->convertTo0to1(plainValue));

    return false;
}

bool CrossoverModuleComponent::swapParameterPlainValues(const juce::String& firstParameterId,
                                                        const juce::String& secondParameterId)
{
    auto* firstParameter = valueTreeState.getParameter(firstParameterId);
    auto* secondParameter = valueTreeState.getParameter(secondParameterId);

    if (firstParameter == nullptr || secondParameter == nullptr)
        return false;

    const auto firstValue = firstParameter->convertFrom0to1(firstParameter->getValue());
    const auto secondValue = secondParameter->convertFrom0to1(secondParameter->getValue());

    if (std::abs(firstValue - secondValue) <= 1.0e-6f)
        return false;

    if (config.undoManager != nullptr)
        config.undoManager->beginNewTransaction();

    firstParameter->beginChangeGesture();
    secondParameter->beginChangeGesture();
    firstParameter->setValueNotifyingHost(firstParameter->convertTo0to1(secondValue));
    secondParameter->setValueNotifyingHost(secondParameter->convertTo0to1(firstValue));
    secondParameter->endChangeGesture();
    firstParameter->endChangeGesture();

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();

    return true;
}

bool CrossoverModuleComponent::constrainSplitFrequency(const size_t splitIndex)
{
    const auto activeSplitCount = getActiveSplitCount();

    if (splitIndex >= activeSplitCount || splitIndex >= splitParameterSuffixes.size())
        return false;

    const auto parameterId = config.makeCrossoverParameterId(splitParameterSuffixes[splitIndex]);
    auto* parameter = valueTreeState.getParameter(parameterId);

    if (parameter == nullptr)
        return false;

    auto lowerBound = parameter->convertFrom0to1(0.0f);
    auto upperBound = parameter->convertFrom0to1(1.0f);

    if (splitIndex > 0)
    {
        const auto previousId = config.makeCrossoverParameterId(splitParameterSuffixes[splitIndex - 1]);

        if (auto* previousParameter = valueTreeState.getParameter(previousId))
            lowerBound = juce::jmax(lowerBound,
                                    previousParameter->convertFrom0to1(previousParameter->getValue()) + minSplitFrequencyGapHz);
    }

    if (splitIndex + 1 < activeSplitCount)
    {
        const auto nextId = config.makeCrossoverParameterId(splitParameterSuffixes[splitIndex + 1]);

        if (auto* nextParameter = valueTreeState.getParameter(nextId))
            upperBound = juce::jmin(upperBound,
                                    nextParameter->convertFrom0to1(nextParameter->getValue()) - minSplitFrequencyGapHz);
    }

    const auto currentValue = parameter->convertFrom0to1(parameter->getValue());
    const auto constrainedValue = juce::jlimit(lowerBound, juce::jmax(lowerBound, upperBound), currentValue);

    if (std::abs(currentValue - constrainedValue) <= 1.0e-6f)
        return false;

    return setParameterPlainValue(parameterId, constrainedValue);
}

bool CrossoverModuleComponent::setParameterNormalisedValue(juce::RangedAudioParameter& parameter, const float normalisedValue)
{
    const auto value = juce::jlimit(0.0f, 1.0f, normalisedValue);
    const auto wasDifferent = std::abs(parameter.getValue() - value) > 1.0e-6f;

    if (! wasDifferent)
        return false;

    if (config.undoManager != nullptr)
        config.undoManager->beginNewTransaction();

    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(value);
    parameter.endChangeGesture();

    if (config.markParametersDirty != nullptr)
        config.markParametersDirty();

    return true;
}

bool CrossoverModuleComponent::assignButtonToHostSlot(const juce::String& parameterId,
                                                       const juce::String& fallbackName,
                                                       const BoxTextButton* button)
{
    if (config.assignHostSlot == nullptr)
        return false;

    if (auto* parameter = valueTreeState.getParameter(parameterId))
    {
        return config.assignHostSlot(parameterId,
                                     button != nullptr ? button->getButtonText() : fallbackName,
                                     parameter->getValue());
    }

    return false;
}

void CrossoverModuleComponent::clearFocus()
{
    shell_parameter_focus::clearFocus(*this);

    if (config.clearKeyboardFocus != nullptr)
        config.clearKeyboardFocus();
    else
        clearKeyboardFocus(*this);
}
