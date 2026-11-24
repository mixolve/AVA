#include "Processor.h"
#include "PresetStorage.h"
#include "StateValidation.h"
#include "../shared/StateValidation.h"

namespace eql_state
{
bool isCurrentState(const juce::ValueTree& state,
                    juce::AudioProcessorValueTreeState& parameters)
{
    if (! ava::modules::state::hasValidParameterState(state, parameters))
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

bool EqlModuleProcessor::restoreState(juce::ValueTree restoredState, const bool resolvePreset) noexcept
{
    if (! eql_state::isCurrentState(restoredState, parameters))
        return false;

    const auto restoredFilterCount = static_cast<int>(restoredState.getProperty(activeFilterCountStateKey));
    const ModelEdit edit(*this);
    parameters.replaceState(restoredState);
    setActiveFilterCount(restoredFilterCount);
    restoreGainsRequested.store(true, std::memory_order_release);
    resetRequested.store(true, std::memory_order_release);
    markEqlFiltersDirty();

    if (resolvePreset)
    {
        const auto catalog = loadFilterPresetsXml();
        resolveFilterPresetSelection(catalog.get());
    }
    return true;
}
