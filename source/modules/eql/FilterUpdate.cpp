#include "ProcessorSupport.h"

#include <cmath>

void EqlModuleProcessor::resetFilters() noexcept
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

    for (auto& cachedState : cachedFilterStates)
        cachedState.valid = false;
}

void EqlModuleProcessor::updateFilters()
{
    const juce::ScopedLock lock(filterProcessLock);

    if (currentSampleRate <= 0.0)
        return;

    const auto filterCount = getActiveFilterCount();

    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        const auto filterArrayIndex = static_cast<size_t>(filterIndex);

        if (filterIndex >= filterCount)
        {
            if (! filterDesignMatches(filterArrayIndex, false, FilterType::bell, 0.0f, 0.0f, 0.0f, 0.0f))
            {
                setBellIdentityResponse(filterArrayIndex);
                setShelfIdentityResponse(filterArrayIndex);
                setCutIdentityResponse(filterArrayIndex);
                setTiltIdentityResponse(filterArrayIndex);
                setPhaseIdentityResponse(filterArrayIndex);
                storeFilterDesignState(filterArrayIndex, false, FilterType::bell, 0.0f, 0.0f, 0.0f, 0.0f);
            }
            continue;
        }

        const auto filterType = getFilterTypeForSection(filterArrayIndex);
        const auto frequency = filterFrequencyParams[filterArrayIndex] != nullptr
            ? juce::jlimit(minimumVisibleFilterFrequency,
                           maximumVisibleFilterFrequency,
                           filterFrequencyParams[filterArrayIndex]->load(std::memory_order_relaxed))
            : defaultFilterFrequencyHz;
        const auto designFrequency = computeDesignFrequency(static_cast<double>(frequency), currentSampleRate);
        const auto slope = filterSlopeChoiceParams[filterArrayIndex] != nullptr
            ? static_cast<float>(EqlModuleProcessor::getBellSlopeValueForChoiceIndex(filterSlopeChoiceParams[filterArrayIndex]->getIndex()))
            : EqlModuleProcessor::fixedSlopeDbPerOct;
        const auto bandwidth = filterBandwidthParams[filterArrayIndex] != nullptr
            ? juce::jlimit(minimumBellBandwidth,
                           maximumBellBandwidth,
                           filterBandwidthParams[filterArrayIndex]->load(std::memory_order_relaxed))
            : 1.0f;
        const auto gainDb = effectiveFilterGainDb[filterArrayIndex];
        const auto& cachedState = cachedFilterStates[filterArrayIndex];
        const auto isContinuousUpdate = cachedState.valid
            && cachedState.active
            && cachedState.type == filterType;
        const auto transitionSamples = isContinuousUpdate
            ? juce::jmax(1, juce::roundToInt(currentSampleRate * coefficientTransitionSeconds))
            : 0;

        if (isVolumeFilterType(filterType))
        {
            if (! filterDesignMatches(filterArrayIndex, true, filterType, 0.0f, 0.0f, 0.0f, gainDb))
            {
                setBellIdentityResponse(filterArrayIndex);
                setShelfIdentityResponse(filterArrayIndex);
                setCutIdentityResponse(filterArrayIndex);
                setTiltIdentityResponse(filterArrayIndex);
                setPhaseIdentityResponse(filterArrayIndex);
                storeFilterDesignState(filterArrayIndex, true, filterType, 0.0f, 0.0f, 0.0f, gainDb);
            }
            continue;
        }

        const auto cachedBandwidth = isTiltFilterType(filterType) ? 0.0f : bandwidth;
        const auto cachedSlope = isTiltFilterType(filterType) ? 0.0f : slope;

        if (isCutFilterType(filterType))
        {
            if (filterDesignMatches(filterArrayIndex, true, filterType, frequency, bandwidth, slope, 0.0f))
                continue;

            setBellIdentityResponse(filterArrayIndex);
            setShelfIdentityResponse(filterArrayIndex);
            setTiltIdentityResponse(filterArrayIndex);
            setPhaseIdentityResponse(filterArrayIndex);
            if (! (cachedFilterStates[filterArrayIndex].valid
                && cachedFilterStates[filterArrayIndex].active
                && cachedFilterStates[filterArrayIndex].type == filterType))
            {
                setCutIdentityResponse(filterArrayIndex);
            }

            rebuildCutBlendFilter(filterArrayIndex,
                                  filterType,
                                  designFrequency,
                                  static_cast<double>(slope));
            updatePhaseFirFilter(phaseFirFilters[filterArrayIndex],
                                 filterType,
                                 designFrequency,
                                 static_cast<double>(bandwidth),
                                 static_cast<double>(slope),
                                 0.0);

            storeFilterDesignState(filterArrayIndex, true, filterType, frequency, bandwidth, slope, 0.0f);

            continue;
        }

        if (std::abs(gainDb) < 1.0e-6f)
        {
            if (! filterDesignMatches(filterArrayIndex, true, filterType, frequency, cachedBandwidth, cachedSlope, gainDb))
            {
                if (isContinuousUpdate && isTiltFilterType(filterType))
                {
                    setBellIdentityResponse(filterArrayIndex);
                    setShelfIdentityResponse(filterArrayIndex);
                    setCutIdentityResponse(filterArrayIndex);
                    BiquadCascade target;
                    tiltFilters[filterArrayIndex].transitionTo(target, transitionSamples);
                    setPhaseIdentityResponse(filterArrayIndex);
                    storeFilterDesignState(filterArrayIndex, true, filterType, frequency, 0.0f, 0.0f, gainDb);
                    continue;
                }

                if (isContinuousUpdate && isShelfFilterType(filterType))
                {
                    setBellIdentityResponse(filterArrayIndex);
                    setCutIdentityResponse(filterArrayIndex);
                    setTiltIdentityResponse(filterArrayIndex);
                    const auto slopeBlend = mapShelfSlopeToBlend(static_cast<double>(slope));

                    for (int order = 1; order <= static_cast<int>(maxShelfOrder); ++order)
                    {
                        BiquadCascade target;
                        const auto orderIsAudible = order == slopeBlend.lowerOrder
                            || order == slopeBlend.upperOrder;
                        shelfOrderFilters[filterArrayIndex][static_cast<size_t>(order - 1)].transitionTo(
                            target,
                            orderIsAudible ? transitionSamples : 0);
                    }

                    setPhaseIdentityResponse(filterArrayIndex);
                    storeFilterDesignState(filterArrayIndex, true, filterType, frequency, bandwidth, slope, gainDb);
                    continue;
                }

                setBellIdentityResponse(filterArrayIndex);
                setShelfIdentityResponse(filterArrayIndex);
                setCutIdentityResponse(filterArrayIndex);
                setTiltIdentityResponse(filterArrayIndex);
                setPhaseIdentityResponse(filterArrayIndex);
                storeFilterDesignState(filterArrayIndex, true, filterType, frequency, cachedBandwidth, cachedSlope, gainDb);
            }
            continue;
        }

        if (filterDesignMatches(filterArrayIndex, true, filterType, frequency, cachedBandwidth, cachedSlope, gainDb))
            continue;

        const auto gain = juce::Decibels::decibelsToGain(static_cast<double>(gainDb));

        if (filterType == FilterType::bell)
        {
            setShelfIdentityResponse(filterArrayIndex);
            setCutIdentityResponse(filterArrayIndex);
            setTiltIdentityResponse(filterArrayIndex);

            for (int order = 1; order <= static_cast<int>(maxBellOrder); ++order)
            {
                updateBellOrderFilter(bellOrderFilters[filterArrayIndex][static_cast<size_t>(order - 1)],
                                      order,
                                      designFrequency,
                                      static_cast<double>(bandwidth),
                                      gain);
            }

            updatePhaseFirFilter(phaseFirFilters[filterArrayIndex],
                                 filterType,
                                 designFrequency,
                                 static_cast<double>(bandwidth),
                                 static_cast<double>(slope),
                                 static_cast<double>(gainDb));

            storeFilterDesignState(filterArrayIndex, true, filterType, frequency, bandwidth, slope, gainDb);

            continue;
        }

        if (isTiltFilterType(filterType))
        {
            setBellIdentityResponse(filterArrayIndex);
            setShelfIdentityResponse(filterArrayIndex);
            setCutIdentityResponse(filterArrayIndex);
            BiquadCascade target;
            updateTiltFilter(target, designFrequency, static_cast<double>(gainDb));
            tiltFilters[filterArrayIndex].transitionTo(target, transitionSamples);
            updatePhaseFirFilter(phaseFirFilters[filterArrayIndex],
                                 filterType,
                                 designFrequency,
                                 static_cast<double>(bandwidth),
                                 static_cast<double>(slope),
                                 static_cast<double>(gainDb));
            storeFilterDesignState(filterArrayIndex, true, filterType, frequency, 0.0f, 0.0f, gainDb);

            continue;
        }

        setBellIdentityResponse(filterArrayIndex);
        setCutIdentityResponse(filterArrayIndex);
        setTiltIdentityResponse(filterArrayIndex);
        const auto slopeBlend = mapShelfSlopeToBlend(static_cast<double>(slope));

        for (int order = 1; order <= static_cast<int>(maxShelfOrder); ++order)
        {
            BiquadCascade target;
            updateShelfOrderFilterRaw(target,
                                      filterType,
                                      order,
                                      designFrequency,
                                      static_cast<double>(bandwidth),
                                      gain);
            const auto orderIsAudible = order == slopeBlend.lowerOrder
                || order == slopeBlend.upperOrder;
            shelfOrderFilters[filterArrayIndex][static_cast<size_t>(order - 1)].transitionTo(target,
                                                                                             orderIsAudible
                                                                                                 ? transitionSamples
                                                                                                 : 0);
        }

        updatePhaseFirFilter(phaseFirFilters[filterArrayIndex],
                             filterType,
                             designFrequency,
                             static_cast<double>(bandwidth),
                             static_cast<double>(slope),
                             static_cast<double>(gainDb));

        storeFilterDesignState(filterArrayIndex, true, filterType, frequency, bandwidth, slope, gainDb);
    }
}
