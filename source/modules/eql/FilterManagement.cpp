#include "FilterSupport.h"
#include "../shared/StateValidation.h"
#include "ParameterIds.h"

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
    float phase = 0.0f;
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
        readParameterValue(state, EqlModuleProcessor::getFilterPhaseParamId(filterIndex)),
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
    setParameterValue(state, EqlModuleProcessor::getFilterPhaseParamId(filterIndex), values.phase);
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

    const ModelEdit edit(*this);
    const auto newIndex = currentCount;
    setFilterParameterValues(parameters, newIndex, makeDefaultFilterParameterValues(FilterType::bell));
    setActiveFilterCount(currentCount + 1);
    return true;
}

std::optional<EqlModuleProcessor::FilterIndexMap> EqlModuleProcessor::removeFilter(const int filterIndex) noexcept
{
    const auto currentCount = getActiveFilterCount();
    if (currentCount <= 1 || !juce::isPositiveAndBelow(filterIndex, currentCount)) return std::nullopt;

    const ModelEdit edit(*this);
    FilterIndexMap mapping;
    for (int index = 0; index < maxFilterCount; ++index)
        mapping[size_t(index)] = index == filterIndex ? -1 : index > filterIndex ? index - 1 : index;
    for (int source = filterIndex + 1; source < currentCount; ++source)
        setFilterParameterValues(parameters, mapping[size_t(source)], readFilterParameterValues(parameters, source));
    resetFilterParameterValues(parameters, currentCount - 1);
    setActiveFilterCount(currentCount - 1);
    resetRequested.store(true, std::memory_order_release);
    restoreGainsRequested.store(true, std::memory_order_release);
    if (onFilterRemapped) onFilterRemapped(mapping);
    return mapping;
}

bool EqlModuleProcessor::applyFilterOrder(const std::vector<int>& orderedFilterIndices) noexcept
{
    const auto currentCount = getActiveFilterCount();

    if (currentCount <= 1 || static_cast<int>(orderedFilterIndices.size()) != currentCount)
        return false;

    if (!ava::modules::state::isPermutation(orderedFilterIndices, currentCount))
        return false;

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

    const ModelEdit edit(*this);
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

    resetRequested.store(true, std::memory_order_release);
    restoreGainsRequested.store(true, std::memory_order_release);
    FilterIndexMap mapping;
    for (int i = 0; i < maxFilterCount; ++i) mapping[size_t(i)] = i;
    for (int destination = 0; destination < currentCount; ++destination)
        mapping[size_t(orderedFilterIndices[size_t(destination)])] = destination;
    if (onFilterRemapped) onFilterRemapped(mapping);
    return true;
}

void EqlModuleProcessor::setActiveFilterCount(const int newCount) noexcept
{
    const auto count = clampActiveFilterCount(newCount);
    prepareFilterStorage(count);
    activeFilterCount.store(count, std::memory_order_release);
    markEqlFiltersDirty();
    if (onLayoutChanged) onLayoutChanged();
}

void EqlModuleProcessor::markEqlFiltersDirty() noexcept
{
    eqlFiltersDirty.store(true, std::memory_order_release);
}

void EqlModuleProcessor::prepareFilterStorage(int count)
{
    // Called only by a model transaction before publishing the new active count.
    // Slots are never removed while this processor can still be rendered.
    bellOrderFilters.ensure(count); shelfOrderFilters.ensure(count);
    tiltFilters.ensure(count); cutBlendFilters.ensure(count); phaseFirFilters.ensure(count);
}
