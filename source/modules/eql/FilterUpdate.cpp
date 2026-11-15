#include "FilterSupport.h"

#include <cmath>

void EqlModuleProcessor::resetFilterHistory() noexcept
{
    for (int i = 0; i < blockSettings.count; ++i) {
        const auto n = size_t(i);
        for (auto& filter : bellOrderFilters[n]) filter.reset();
        for (auto& filter : shelfOrderFilters[n]) filter.reset();
        tiltFilters[n].reset(); cutBlendFilters[n].reset(); phaseFirFilters[n].reset();
    }
}

void EqlModuleProcessor::resetFilters() noexcept
{

    resetFilterHistory();

    for (auto& cachedState : cachedFilterStates)
        cachedState.valid = false;
}

void EqlModuleProcessor::updateFilters()
{

    if (currentSampleRate <= 0.0)
        return;

    const auto filterCount = blockSettings.count;

    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        const auto filterArrayIndex = static_cast<size_t>(filterIndex);

        if (filterIndex >= filterCount) {
            cachedFilterStates[filterArrayIndex].valid = false;
            continue;
        }

        const auto& settings = blockSettings.filters[filterArrayIndex];
        const auto filterType = settings.type;
        const auto frequency = juce::jlimit(minimumVisibleFilterFrequency, maximumVisibleFilterFrequency, settings.frequency);
        const auto designFrequency = computeDesignFrequency(static_cast<double>(frequency), currentSampleRate);
        const auto slope = getSlopeDbPerOctForOrderChoice(settings.order);
        const auto bandwidth = juce::jlimit(minimumBellBandwidth, maximumBellBandwidth, settings.bandwidth);
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

        const auto phaseEnabled = settings.phase;
        if (phaseEnabled)
        {
            if (! phaseFirFilters[filterArrayIndex].active
                || ! filterDesignMatches(filterArrayIndex, true, filterType, frequency, cachedBandwidth, cachedSlope, gainDb))
            {
                updatePhaseFirFilter(phaseFirFilters[filterArrayIndex], filterType, designFrequency,
                                     static_cast<double>(bandwidth), static_cast<double>(slope), static_cast<double>(gainDb));
                storeFilterDesignState(filterArrayIndex, true, filterType, frequency, cachedBandwidth, cachedSlope, gainDb);
            }
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

                    for (size_t branch = 0; branch < 2; ++branch)
                    {
                        BiquadCascade target;
                        const auto order = branch == 0 ? slopeBlend.lowerOrder : slopeBlend.upperOrder;
                        const auto orderIsAudible = order > 0;
                        shelfOrderFilters[filterArrayIndex][branch].transitionTo(
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

            const auto slopeBlend = mapBellSlopeToBlend(static_cast<double>(slope));
            for (size_t branch = 0; branch < 2; ++branch)
            {
                const auto order = branch == 0 ? slopeBlend.lowerOrder : slopeBlend.upperOrder;
                updateBellOrderFilter(bellOrderFilters[filterArrayIndex][branch],
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

        for (size_t branch = 0; branch < 2; ++branch)
        {
            const auto order = branch == 0 ? slopeBlend.lowerOrder : slopeBlend.upperOrder;
            BiquadCascade target;
            updateShelfOrderFilterRaw(target,
                                      filterType,
                                      order,
                                      designFrequency,
                                      static_cast<double>(bandwidth),
                                      gain);
            const auto orderIsAudible = order > 0;
            shelfOrderFilters[filterArrayIndex][branch].transitionTo(target,
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
