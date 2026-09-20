#include "Processor.h"
#include "PresetStorage.h"
#include "State.h"
#include "../shared/StateUtilities.h"

namespace eql_state
{
bool isCurrentState(const juce::ValueTree& state,
                    juce::AudioProcessorValueTreeState& parameters)
{
    if (! ava::modules::state::hasExactParameterState(state, parameters))
        return false;

    if (! state.hasProperty(EqlModuleProcessor::activeFilterCountStateKey))
        return false;

    const auto activeFilterCountText = state.getProperty(EqlModuleProcessor::activeFilterCountStateKey).toString();
    const auto activeFilterCount = activeFilterCountText.getIntValue();

    if (activeFilterCountText != juce::String(activeFilterCount)
        || activeFilterCount < 0
        || activeFilterCount > EqlModuleProcessor::maxFilterCount)
        return false;

    if (state.hasProperty(EqlModuleProcessor::filterPresetSelectedStateKey))
    {
        const auto selectedPreset = state.getProperty(EqlModuleProcessor::filterPresetSelectedStateKey).toString();

        if (selectedPreset.isEmpty() || selectedPreset != selectedPreset.trim())
            return false;
    }

    for (int propertyIndex = 0; propertyIndex < state.getNumProperties(); ++propertyIndex)
    {
        const auto propertyName = state.getPropertyName(propertyIndex);

        if (propertyName != juce::Identifier(EqlModuleProcessor::activeFilterCountStateKey)
            && propertyName != juce::Identifier(EqlModuleProcessor::filterPresetSelectedStateKey))
            return false;
    }

    return true;
}
}

bool EqlModuleProcessor::restoreState(juce::ValueTree restoredState) noexcept
{
    if (! eql_state::isCurrentState(restoredState, parameters))
        return false;

    const auto restoredFilterCount = static_cast<int>(restoredState.getProperty(activeFilterCountStateKey));
    const auto wasPrepared = prepared.exchange(false, std::memory_order_acq_rel);
    const juce::ScopedLock lock(filterProcessLock);

    parameters.replaceState(restoredState);
    setActiveFilterCount(restoredFilterCount);
    resetFilters();

    if (wasPrepared && currentSampleRate > 0.0)
    {
        updateFilters();
        eqlFiltersDirty.store(false, std::memory_order_release);
        prepared.store(true, std::memory_order_release);
    }
    else
    {
        markEqlFiltersDirty();
    }

    return true;
}

void EqlModuleProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto stateXml = createSerializableStateXml(*this))
        juce::AudioProcessor::copyXmlToBinary(*stateXml, destData);
}

bool EqlModuleProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    auto stateXml = juce::AudioProcessor::getXmlFromBinary(data, sizeInBytes);

    if (stateXml == nullptr)
        return false;

    return restoreState(juce::ValueTree::fromXml(*stateXml));
}
