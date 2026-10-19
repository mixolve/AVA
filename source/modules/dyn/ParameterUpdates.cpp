#include "Processor.h"
#include "../../crossover/ParameterAccess.h"

#include <cmath>

void DynModuleProcessor::setParameterListenersEnabled(const bool enabled)
{
    ava::crossover::parameters::setRangeParameterListenersEnabled(valueTreeState, *this,
                                                                  dyn::parameters::parameterSpecs, numRanges, enabled);
}

void DynModuleProcessor::parameterChanged(const juce::String& parameterID, const float newValue)
{
    if (linkedParameterUpdateInProgress.exchange(true, std::memory_order_acq_rel))
    {
        markParametersDirty();
        return;
    }

    for (const auto& range : linkedParameters)
        for (size_t linkIndex = 0; linkIndex < range.size(); ++linkIndex)
        {
            auto* source = range[linkIndex];
            if (source == nullptr || source->paramID != parameterID)
                continue;

            const auto normalizedValue = source->convertTo0to1(newValue);
            for (const auto& targets : linkedParameters)
                if (auto* target = targets[linkIndex]; target != nullptr
                    && std::abs(target->getValue() - normalizedValue) > 1.0e-6f)
                    target->setValueNotifyingHost(normalizedValue);

            linkedParameterUpdateInProgress.store(false, std::memory_order_release);
            markParametersDirty();
            return;
        }

    linkedParameterUpdateInProgress.store(false, std::memory_order_release);
    markParametersDirty();
}
