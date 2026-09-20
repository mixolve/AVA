#include "FilterSupport.h"
#include "FilterParameters.h"

#include <memory>
#include <vector>

namespace
{
void setParameterValue(juce::AudioProcessorValueTreeState& state,
                       const juce::String& parameterId,
                       const float value)
{
    auto* parameter = state.getParameter(parameterId);

    if (parameter == nullptr)
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    parameter->endChangeGesture();
}

float readParameterValue(juce::AudioProcessorValueTreeState& state, const juce::String& parameterId)
{
    if (auto* parameter = state.getParameter(parameterId))
        return parameter->convertFrom0to1(parameter->getValue());

    return 0.0f;
}

struct FilterParameterValues
{
    float type = 0.0f;
    float place = 0.0f;
    float frequency = 0.0f;
    float bandwidth = 0.0f;
    float orderChoice = 0.0f;
    float gain = 0.0f;
    float bypass = 0.0f;
};

FilterParameterValues makeDefaultFilterParameterValues(const EqlModuleProcessor::FilterType type)
{
    return {
        static_cast<float>(EqlModuleProcessor::choiceIndexFromFilterType(type)),
        0.0f,
        defaultFilterFrequencyHz,
        defaultFilterBandwidthOctaves,
        static_cast<float>(EqlModuleProcessor::getOrderChoiceForSlopeDbPerOct(defaultFilterSlopeDbPerOct)),
        0.0f,
        0.0f
    };
}

FilterParameterValues readFilterParameterValues(juce::AudioProcessorValueTreeState& state, const int filterIndex)
{
    return {
        readParameterValue(state, EqlModuleProcessor::getFilterTypeParamId(filterIndex)),
        readParameterValue(state, EqlModuleProcessor::getFilterPlaceParamId(filterIndex)),
        readParameterValue(state, EqlModuleProcessor::getFilterFrequencyParamId(filterIndex)),
        readParameterValue(state, EqlModuleProcessor::getFilterBandwidthParamId(filterIndex)),
        readParameterValue(state, EqlModuleProcessor::getFilterOrderParamId(filterIndex)),
        readParameterValue(state, EqlModuleProcessor::getFilterGainParamId(filterIndex)),
        readParameterValue(state, EqlModuleProcessor::getFilterBypassParamId(filterIndex))
    };
}

void setFilterParameterValues(juce::AudioProcessorValueTreeState& state,
                              const int filterIndex,
                              const FilterParameterValues& values)
{
    setParameterValue(state, EqlModuleProcessor::getFilterTypeParamId(filterIndex), values.type);
    setParameterValue(state, EqlModuleProcessor::getFilterPlaceParamId(filterIndex), values.place);
    setParameterValue(state, EqlModuleProcessor::getFilterFrequencyParamId(filterIndex), values.frequency);
    setParameterValue(state, EqlModuleProcessor::getFilterBandwidthParamId(filterIndex), values.bandwidth);
    setParameterValue(state, EqlModuleProcessor::getFilterOrderParamId(filterIndex), values.orderChoice);
    setParameterValue(state, EqlModuleProcessor::getFilterGainParamId(filterIndex), values.gain);
    setParameterValue(state, EqlModuleProcessor::getFilterBypassParamId(filterIndex), values.bypass);
}

void resetFilterParameterValues(juce::AudioProcessorValueTreeState& state, const int filterIndex)
{
    forEachFilterParameterId(filterIndex,
                             [&state] (const juce::String& parameterId)
                             {
                                 if (auto* parameter = state.getParameter(parameterId))
                                     parameter->setValueNotifyingHost(parameter->getDefaultValue());
                             });
}
}

int EqlModuleProcessor::getActiveFilterCount() const noexcept
{
    return activeFilterCount.load(std::memory_order_relaxed);
}

bool EqlModuleProcessor::addFilter() noexcept
{
    const auto currentCount = getActiveFilterCount();

    if (currentCount >= maxFilterCount)
        return false;

    const auto newIndex = currentCount;
    setFilterParameterValues(parameters, newIndex, makeDefaultFilterParameterValues(FilterType::bell));
    setActiveFilterCount(currentCount + 1);
    return true;
}

bool EqlModuleProcessor::removeFilter(const int filterIndex) noexcept
{
    const auto currentCount = getActiveFilterCount();

    if (currentCount <= 0 || filterIndex < 0 || filterIndex >= currentCount)
        return false;

    for (int sourceIndex = filterIndex + 1; sourceIndex < currentCount; ++sourceIndex)
    {
        const auto destinationIndex = sourceIndex - 1;
        setFilterParameterValues(parameters, destinationIndex, readFilterParameterValues(parameters, sourceIndex));
    }

    resetFilterParameterValues(parameters, currentCount - 1);

    setActiveFilterCount(currentCount - 1);
    resetFilters();
    updateFilters();
    return true;
}

bool EqlModuleProcessor::clearFilters() noexcept
{
    const auto currentCount = getActiveFilterCount();

    if (currentCount <= 0)
        return false;

    for (int filterIndex = 0; filterIndex < currentCount; ++filterIndex)
        resetFilterParameterValues(parameters, filterIndex);

    setActiveFilterCount(0);
    resetFilters();
    updateFilters();
    return true;
}

bool EqlModuleProcessor::applyFilterOrder(const std::vector<int>& orderedFilterIndices) noexcept
{
    const auto currentCount = getActiveFilterCount();

    if (currentCount <= 1 || static_cast<int>(orderedFilterIndices.size()) != currentCount)
        return false;

    std::vector<bool> used(static_cast<size_t>(currentCount), false);

    for (const auto sourceIndex : orderedFilterIndices)
    {
        if (! juce::isPositiveAndBelow(sourceIndex, currentCount))
            return false;

        if (used[static_cast<size_t>(sourceIndex)])
            return false;

        used[static_cast<size_t>(sourceIndex)] = true;
    }

    auto alreadyInOrder = true;

    for (int destinationIndex = 0; destinationIndex < currentCount; ++destinationIndex)
    {
        if (orderedFilterIndices[static_cast<size_t>(destinationIndex)] != destinationIndex)
        {
            alreadyInOrder = false;
            break;
        }
    }

    if (alreadyInOrder)
        return false;

    std::vector<FilterParameterValues> snapshots(static_cast<size_t>(currentCount));

    for (int sourceIndex = 0; sourceIndex < currentCount; ++sourceIndex)
        snapshots[static_cast<size_t>(sourceIndex)] = readFilterParameterValues(parameters, sourceIndex);

    for (int destinationIndex = 0; destinationIndex < currentCount; ++destinationIndex)
    {
        const auto sourceIndex = orderedFilterIndices[static_cast<size_t>(destinationIndex)];
        setFilterParameterValues(parameters,
                                 destinationIndex,
                                 snapshots[static_cast<size_t>(sourceIndex)]);
    }

    resetFilters();
    updateFilters();
    return true;
}

void EqlModuleProcessor::setActiveFilterCount(const int newCount) noexcept
{
    activeFilterCount.store(clampActiveFilterCount(newCount), std::memory_order_relaxed);
    markEqlFiltersDirty();
}

void EqlModuleProcessor::markEqlFiltersDirty() noexcept
{
    eqlFiltersDirty.store(true, std::memory_order_release);
}
