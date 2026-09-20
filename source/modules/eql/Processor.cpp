#include "Processor.h"
#include "FilterSupport.h"

#include <cmath>

void EqlModuleProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    prepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(filterProcessLock);

    currentSampleRate = sampleRate;
    preparedNumChannels = static_cast<int>(maxSupportedChannels);
    filterProcessBufferA.setSize(preparedNumChannels, juce::jmax(1, samplesPerBlock));
    filterProcessBufferB.setSize(preparedNumChannels, juce::jmax(1, samplesPerBlock));
    placeWorkBuffer.setSize(1, juce::jmax(1, samplesPerBlock));
    placeAuxBuffer.setSize(preparedNumChannels, juce::jmax(1, samplesPerBlock));

    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        const auto arrayIndex = static_cast<size_t>(filterIndex);
        const auto gainDb = filterGainParams[arrayIndex] != nullptr
            ? juce::jlimit(-48.0f, 48.0f, filterGainParams[arrayIndex]->load(std::memory_order_relaxed))
            : 0.0f;
        filterGainSmoothers[arrayIndex].reset(sampleRate, 0.10);
        filterGainSmoothers[arrayIndex].setCurrentAndTargetValue(gainDb);
        effectiveFilterGainDb[arrayIndex] = gainDb;
    }

    resetFilters();
    markEqlFiltersDirty();
    updateFilters();
    eqlFiltersDirty.store(false, std::memory_order_release);
    prepared.store(true, std::memory_order_release);
}

void EqlModuleProcessor::releaseResources()
{
    prepared.store(false, std::memory_order_release);
    const juce::ScopedLock lock(filterProcessLock);

    resetFilters();
    currentSampleRate = 0.0;
}

bool EqlModuleProcessor::updateSmoothedFilterGains(const int samplesPerBlock) noexcept
{
    auto changed = false;

    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        const auto arrayIndex = static_cast<size_t>(filterIndex);
        const auto targetGainDb = filterGainParams[arrayIndex] != nullptr
            ? juce::jlimit(-48.0f, 48.0f, filterGainParams[arrayIndex]->load(std::memory_order_relaxed))
            : 0.0f;
        const auto placeChoice = filterPlaceParams[arrayIndex] != nullptr
            ? juce::jlimit(0,
                           7,
                           static_cast<int>(std::lround(filterPlaceParams[arrayIndex]->load(std::memory_order_relaxed))))
            : 0;

        if (isPhasePlaceChoice(placeChoice))
        {
            filterGainSmoothers[arrayIndex].setCurrentAndTargetValue(targetGainDb);
            effectiveFilterGainDb[arrayIndex] = targetGainDb;
            continue;
        }

        auto& smoother = filterGainSmoothers[arrayIndex];
        smoother.setTargetValue(targetGainDb);
        const auto nextGainDb = smoother.skip(juce::jmax(1, samplesPerBlock));

        if (std::abs(nextGainDb - effectiveFilterGainDb[arrayIndex]) > 1.0e-5f)
        {
            effectiveFilterGainDb[arrayIndex] = nextGainDb;
            changed = true;
        }
    }

    return changed;
}

void EqlModuleProcessor::resetProcessingState() noexcept
{
    const juce::ScopedLock lock(filterProcessLock);

    for (auto& orderFilters : bellOrderFilters)
        for (auto& filter : orderFilters)
            filter.reset();

    for (auto& orderFilters : shelfOrderFilters)
        for (auto& filter : orderFilters)
            filter.reset();

    for (auto& filter : tiltFilters)
        filter.reset();

    for (auto& filter : cutBlendFilters)
        filter.reset();

    for (auto& filter : phaseFirFilters)
        filter.reset();

    filterProcessBufferA.clear();
    filterProcessBufferB.clear();
    placeWorkBuffer.clear();
    placeAuxBuffer.clear();
}

void EqlModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (! prepared.load(std::memory_order_acquire))
        return;

    const juce::ScopedTryLock lock(filterProcessLock);

    if (! lock.isLocked())
        return;

    if (! prepared.load(std::memory_order_acquire))
        return;

    const auto processChannels = juce::jmin(buffer.getNumChannels(), preparedNumChannels);
    const auto processSamples = buffer.getNumSamples();

    if (processChannels <= 0 || processSamples <= 0)
        return;

    filterProcessBufferA.setSize(processChannels, processSamples, false, false, true);
    filterProcessBufferB.setSize(processChannels, processSamples, false, false, true);
    placeWorkBuffer.setSize(1, processSamples, false, false, true);
    placeAuxBuffer.setSize(processChannels, processSamples, false, false, true);

    const auto gainSmoothingChanged = updateSmoothedFilterGains(processSamples);
    const auto filtersDirty = eqlFiltersDirty.exchange(false, std::memory_order_acq_rel);

    if (gainSmoothingChanged || filtersDirty)
        updateFilters();

    const auto filterCount = getActiveFilterCount();

    for (int filterIndex = 0; filterIndex < filterCount; ++filterIndex)
        processFilterSection(buffer, filterIndex, processChannels);
}

void EqlModuleProcessor::processFilterSection(juce::AudioBuffer<float>& targetBuffer,
                                              const int filterIndex,
                                              const int processChannels)
{
    auto processPlacedSignal = [this, processChannels] (juce::AudioBuffer<float>& placedBuffer,
                                                        const int placedFilterIndex,
                                                        auto&& processor)
    {
        if (processChannels < 2)
        {
            processor(placedBuffer, processChannels);
            return;
        }

        const auto mode = filterPlaceParams[static_cast<size_t>(placedFilterIndex)] != nullptr
            ? juce::jlimit(0, 7, static_cast<int>(std::lround(filterPlaceParams[static_cast<size_t>(placedFilterIndex)]->load(std::memory_order_relaxed))))
            : 0;

        if (mode == 0 || mode == 5)
        {
            processor(placedBuffer, processChannels);
            return;
        }

        auto& workBuffer = placeWorkBuffer;
        auto& auxBuffer = placeAuxBuffer;

        switch (mode)
        {
            case 1:
                workBuffer.copyFrom(0, 0, placedBuffer, 0, 0, placedBuffer.getNumSamples());
                processor(workBuffer, 1);
                placedBuffer.copyFrom(0, 0, workBuffer, 0, 0, placedBuffer.getNumSamples());
                break;

            case 2:
                workBuffer.copyFrom(0, 0, placedBuffer, 1, 0, placedBuffer.getNumSamples());
                processor(workBuffer, 1);
                placedBuffer.copyFrom(1, 0, workBuffer, 0, 0, placedBuffer.getNumSamples());
                break;

            case 3:
                auxBuffer.copyFrom(0, 0, placedBuffer, 0, 0, placedBuffer.getNumSamples());
                auxBuffer.copyFrom(1, 0, placedBuffer, 1, 0, placedBuffer.getNumSamples());
                for (int sampleIndex = 0; sampleIndex < placedBuffer.getNumSamples(); ++sampleIndex)
                {
                    const auto left = auxBuffer.getReadPointer(0)[sampleIndex];
                    const auto right = auxBuffer.getReadPointer(1)[sampleIndex];
                    workBuffer.setSample(0, sampleIndex, 0.5f * (left + right));
                }
                processor(workBuffer, 1);
                for (int sampleIndex = 0; sampleIndex < placedBuffer.getNumSamples(); ++sampleIndex)
                {
                    const auto processedMid = workBuffer.getReadPointer(0)[sampleIndex];
                    const auto side = 0.5f * (auxBuffer.getReadPointer(0)[sampleIndex] - auxBuffer.getReadPointer(1)[sampleIndex]);
                    placedBuffer.setSample(0, sampleIndex, processedMid + side);
                    placedBuffer.setSample(1, sampleIndex, processedMid - side);
                }
                break;

            case 4:
                auxBuffer.copyFrom(0, 0, placedBuffer, 0, 0, placedBuffer.getNumSamples());
                auxBuffer.copyFrom(1, 0, placedBuffer, 1, 0, placedBuffer.getNumSamples());
                for (int sampleIndex = 0; sampleIndex < placedBuffer.getNumSamples(); ++sampleIndex)
                {
                    const auto left = auxBuffer.getReadPointer(0)[sampleIndex];
                    const auto right = auxBuffer.getReadPointer(1)[sampleIndex];
                    workBuffer.setSample(0, sampleIndex, 0.5f * (left - right));
                }
                processor(workBuffer, 1);
                for (int sampleIndex = 0; sampleIndex < placedBuffer.getNumSamples(); ++sampleIndex)
                {
                    const auto side = workBuffer.getReadPointer(0)[sampleIndex];
                    const auto mid = 0.5f * (auxBuffer.getReadPointer(0)[sampleIndex] + auxBuffer.getReadPointer(1)[sampleIndex]);
                    placedBuffer.setSample(0, sampleIndex, mid + side);
                    placedBuffer.setSample(1, sampleIndex, mid - side);
                }
                break;

            case 6:
                workBuffer.copyFrom(0, 0, placedBuffer, 0, 0, placedBuffer.getNumSamples());
                processor(workBuffer, 1);
                placedBuffer.copyFrom(0, 0, workBuffer, 0, 0, placedBuffer.getNumSamples());
                break;

            case 7:
                workBuffer.copyFrom(0, 0, placedBuffer, 1, 0, placedBuffer.getNumSamples());
                processor(workBuffer, 1);
                placedBuffer.copyFrom(1, 0, workBuffer, 0, 0, placedBuffer.getNumSamples());
                break;

            default:
                processor(placedBuffer, processChannels);
                break;
        }
    };


    const auto filterArrayIndex = static_cast<size_t>(filterIndex);
    const auto filterType = getFilterTypeForSection(filterArrayIndex);
    const auto filterBypassed = filterBypassParams[filterArrayIndex] != nullptr
        && filterBypassParams[filterArrayIndex]->load(std::memory_order_relaxed) >= 0.5f;

    if (filterBypassed)
        return;

    auto placeChoice = filterPlaceParams[filterArrayIndex] != nullptr
        ? juce::jlimit(0, 7, static_cast<int>(std::lround(filterPlaceParams[filterArrayIndex]->load(std::memory_order_relaxed))))
        : 0;

    if (isVolumeFilterType(filterType) && isPhasePlaceChoice(placeChoice))
        placeChoice = 0;

    if (isPhasePlaceChoice(placeChoice) && ! isCutFilterType(filterType))
    {
        if (filterType == FilterType::bell)
        {
            if (filterOrderParams[filterArrayIndex] != nullptr
                && filterOrderParams[filterArrayIndex]->getIndex() == 0)
                return;

            if (! phaseFirFilters[filterArrayIndex].active)
                return;

            phaseFirFilters[filterArrayIndex].processWithChannelMask(targetBuffer,
                                                                   juce::jmin(targetBuffer.getNumChannels(), preparedNumChannels),
                                                                   placeChoice != 7,
                                                                   placeChoice != 6);
            return;
        }

        if (! phaseFirFilters[filterArrayIndex].active)
            return;

        phaseFirFilters[filterArrayIndex].processWithChannelMask(targetBuffer,
                                                               juce::jmin(targetBuffer.getNumChannels(), preparedNumChannels),
                                                               placeChoice != 7,
                                                               placeChoice != 6);

        return;
    }

    if (isVolumeFilterType(filterType))
    {
        const auto gainDb = effectiveFilterGainDb[filterArrayIndex];

        if (std::abs(gainDb) < 1.0e-6f)
            return;

        const auto gain = juce::Decibels::decibelsToGain(gainDb);
        processPlacedSignal(targetBuffer, static_cast<int>(filterArrayIndex), [gain] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            for (int channel = 0; channel < targetChannels; ++channel)
                targetBufferToProcess.applyGain(channel, 0, targetBufferToProcess.getNumSamples(), gain);
        });
        return;
    }

    const auto slopeDbPerOct = filterOrderParams[filterArrayIndex] != nullptr
        ? static_cast<double>(EqlModuleProcessor::getSlopeDbPerOctForOrderChoice(filterOrderParams[filterArrayIndex]->getIndex()))
        : static_cast<double>(EqlModuleProcessor::fixedSlopeDbPerOct);

    if (filterType == FilterType::bell)
    {
        if (filterOrderParams[filterArrayIndex] != nullptr
            && filterOrderParams[filterArrayIndex]->getIndex() == 0)
            return;

        if (bellOrderFilters[filterArrayIndex].front().sectionCount <= 0)
            return;

        const auto bellSlopeBlend = mapBellSlopeToBlend(slopeDbPerOct);
        const auto lowerOrder = bellSlopeBlend.lowerOrder;
        const auto upperOrder = bellSlopeBlend.upperOrder;
        const auto blend = bellSlopeBlend.blend;
        auto& orderFilters = bellOrderFilters[filterArrayIndex];

        processPlacedSignal(targetBuffer, static_cast<int>(filterArrayIndex), [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            if (lowerOrder == upperOrder || blend < 1.0e-6)
            {
                if (lowerOrder > 0)
                    orderFilters[static_cast<size_t>(lowerOrder - 1)].process(targetBufferToProcess, targetChannels);
                return;
            }

            if (lowerOrder > 0)
            {
                filterProcessBufferA.makeCopyOf(targetBufferToProcess, true);
                orderFilters[static_cast<size_t>(lowerOrder - 1)].process(filterProcessBufferA, targetChannels);
            }
            else
            {
                filterProcessBufferA.makeCopyOf(targetBufferToProcess, true);
            }

            filterProcessBufferB.makeCopyOf(targetBufferToProcess, true);

            if (upperOrder > 0)
                orderFilters[static_cast<size_t>(upperOrder - 1)].process(filterProcessBufferB, targetChannels);

            for (int channel = 0; channel < targetChannels; ++channel)
            {
                auto* output = targetBufferToProcess.getWritePointer(channel);
                const auto* lower = filterProcessBufferA.getReadPointer(channel);
                const auto* upper = filterProcessBufferB.getReadPointer(channel);

                for (int sampleIndex = 0; sampleIndex < targetBufferToProcess.getNumSamples(); ++sampleIndex)
                    output[sampleIndex] = static_cast<float>(lower[sampleIndex]
                                                             + ((upper[sampleIndex] - lower[sampleIndex]) * blend));
            }
        });
        return;
    }

    if (isTiltFilterType(filterType))
    {
        if (tiltFilters[filterArrayIndex].stageCount <= 0)
            return;

        processPlacedSignal(targetBuffer, static_cast<int>(filterArrayIndex), [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            tiltFilters[filterArrayIndex].process(targetBufferToProcess, targetChannels);
        });
        return;
    }

    if (isShelfFilterType(filterType))
    {
        if (shelfOrderFilters[filterArrayIndex].front().stageCount <= 0)
            return;

        processPlacedSignal(targetBuffer, static_cast<int>(filterArrayIndex), [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            auto& orderFilters = shelfOrderFilters[filterArrayIndex];
            const auto slopeBlend = mapShelfSlopeToBlend(slopeDbPerOct);
            const auto lowerOrder = slopeBlend.lowerOrder;
            const auto upperOrder = slopeBlend.upperOrder;
            const auto blend = slopeBlend.blend;

            if (lowerOrder == upperOrder || blend < 1.0e-6)
            {
                orderFilters[static_cast<size_t>(lowerOrder - 1)].process(targetBufferToProcess, targetChannels);
                return;
            }

            filterProcessBufferA.makeCopyOf(targetBufferToProcess, true);
            filterProcessBufferB.makeCopyOf(targetBufferToProcess, true);
            orderFilters[static_cast<size_t>(lowerOrder - 1)].process(filterProcessBufferA, targetChannels);
            orderFilters[static_cast<size_t>(upperOrder - 1)].process(filterProcessBufferB, targetChannels);

            for (int channel = 0; channel < targetChannels; ++channel)
            {
                auto* output = targetBufferToProcess.getWritePointer(channel);
                const auto* lower = filterProcessBufferA.getReadPointer(channel);
                const auto* upper = filterProcessBufferB.getReadPointer(channel);

                for (int sampleIndex = 0; sampleIndex < targetBufferToProcess.getNumSamples(); ++sampleIndex)
                    output[sampleIndex] = static_cast<float>(lower[sampleIndex]
                                                             + ((upper[sampleIndex] - lower[sampleIndex]) * blend));
            }
        });
        return;
    }

    if (isCutFilterType(filterType))
    {
        if (cutBlendFilters[filterArrayIndex].stageCount <= 0)
            return;

        processPlacedSignal(targetBuffer, static_cast<int>(filterArrayIndex), [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            cutBlendFilters[filterArrayIndex].process(targetBufferToProcess, targetChannels);
        });
    }
}

int EqlModuleProcessor::getLatencySamples() const noexcept
{
    const auto filterCount = getActiveFilterCount();

    for (int filterIndex = 0; filterIndex < filterCount; ++filterIndex)
    {
        const auto filterArrayIndex = static_cast<size_t>(filterIndex);
        const auto placeChoice = filterPlaceParams[filterArrayIndex] != nullptr
            ? juce::jlimit(0, 7, static_cast<int>(std::lround(filterPlaceParams[filterArrayIndex]->load(std::memory_order_relaxed))))
            : 0;

        if (isPhasePlaceChoice(placeChoice) && phaseFirFilters[filterArrayIndex].active)
            return phaseFirLatencySamples;
    }

    return 0;
}

EqlModuleProcessor::FilterType EqlModuleProcessor::getFilterTypeForSection(const size_t filterIndex) const noexcept
{
    if (filterIndex >= filterTypeParams.size() || filterTypeParams[filterIndex] == nullptr)
        return FilterType::bell;

    return filterTypeFromChoiceIndex(static_cast<int>(std::lround(filterTypeParams[filterIndex]->load(std::memory_order_relaxed))));
}

bool EqlModuleProcessor::filterDesignMatches(const size_t filterIndex,
                                             const bool active,
                                             const FilterType type,
                                             const float frequency,
                                             const float bandwidth,
                                             const float slope,
                                             const float gainDb) const noexcept
{
    const auto& cachedState = cachedFilterStates[filterIndex];

    if (! cachedState.valid)
        return false;

    if (cachedState.active != active
        || cachedState.type != type
        || std::abs(cachedState.sampleRate - currentSampleRate) > 1.0e-9)
        return false;

    if (! active)
        return true;

    return std::abs(cachedState.frequency - frequency) < 1.0e-4f
        && std::abs(cachedState.bandwidth - bandwidth) < 1.0e-4f
        && std::abs(cachedState.slope - slope) < 1.0e-4f
        && std::abs(cachedState.gainDb - gainDb) < 1.0e-4f;
}

void EqlModuleProcessor::storeFilterDesignState(const size_t filterIndex,
                                                const bool active,
                                                const FilterType type,
                                                const float frequency,
                                                const float bandwidth,
                                                const float slope,
                                                const float gainDb) noexcept
{
    auto& cachedState = cachedFilterStates[filterIndex];
    cachedState.valid = true;
    cachedState.active = active;
    cachedState.type = type;
    cachedState.frequency = frequency;
    cachedState.bandwidth = bandwidth;
    cachedState.slope = slope;
    cachedState.gainDb = gainDb;
    cachedState.sampleRate = currentSampleRate;
}

EqlModuleProcessor::EqlModuleProcessor()
    : parameters(moduleParameterHost, nullptr, "eql_state", createParameterLayout())
{
    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        filterTypeParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterTypeParamId(filterIndex));
        filterPlaceParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterPlaceParamId(filterIndex));
        filterFrequencyParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterFrequencyParamId(filterIndex));
        filterBandwidthParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterBandwidthParamId(filterIndex));
        filterOrderParams[static_cast<size_t>(filterIndex)] = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(getFilterOrderParamId(filterIndex)));
        filterGainParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterGainParamId(filterIndex));
        filterBypassParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterBypassParamId(filterIndex));
    }

    setParameterListenersEnabled(true);
}

EqlModuleProcessor::~EqlModuleProcessor()
{
    setParameterListenersEnabled(false);
}

juce::AudioProcessorValueTreeState& EqlModuleProcessor::getValueTreeState() noexcept
{
    return parameters;
}

const juce::AudioProcessorValueTreeState& EqlModuleProcessor::getValueTreeState() const noexcept
{
    return parameters;
}
