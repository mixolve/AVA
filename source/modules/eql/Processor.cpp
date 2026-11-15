#include "Processor.h"
#include "FilterSupport.h"

#include <cmath>

void EqlModuleProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    prepared.store(false, std::memory_order_release);
    processingModelUpdatesEnabled = true;
    processingFilterLimit = maxFilterCount;

    currentSampleRate = sampleRate;
    preparedNumChannels = static_cast<int>(maxSupportedChannels);
    filterProcessBufferA.setSize(preparedNumChannels, juce::jmax(1, samplesPerBlock));
    filterProcessBufferB.setSize(preparedNumChannels, juce::jmax(1, samplesPerBlock));

    syncParameters();
    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        const auto arrayIndex = static_cast<size_t>(filterIndex);
        const auto gainDb = blockSettings.filters[arrayIndex].gain;
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

    resetFilters();
    currentSampleRate = 0.0;
}

bool EqlModuleProcessor::updateSmoothedFilterGains(const int samplesPerBlock) noexcept
{
    auto changed = false;

    for (int filterIndex = 0; filterIndex < maxFilterCount; ++filterIndex)
    {
        const auto arrayIndex = static_cast<size_t>(filterIndex);
        const auto targetGainDb = blockSettings.filters[arrayIndex].gain;
        if (blockSettings.filters[arrayIndex].phase
            && !isCutFilterType(blockSettings.filters[arrayIndex].type) && !isVolumeFilterType(blockSettings.filters[arrayIndex].type))
        {
            changed = changed || std::abs(effectiveFilterGainDb[arrayIndex] - targetGainDb) > 1.0e-5f;
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

    resetFilterHistory();

    filterProcessBufferA.clear();
    filterProcessBufferB.clear();
}

void EqlModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (! prepared.load(std::memory_order_acquire))
        return;

    const auto processChannels = juce::jmin(buffer.getNumChannels(), preparedNumChannels);
    const auto processSamples = buffer.getNumSamples();

    if (processChannels <= 0 || processSamples <= 0)
        return;

    filterProcessBufferA.setSize(processChannels, processSamples, false, false, true);
    filterProcessBufferB.setSize(processChannels, processSamples, false, false, true);

    const auto coherent = syncParameters();
    if (coherent && resetRequested.exchange(false, std::memory_order_acq_rel)) resetFilters();
    if (coherent && restoreGainsRequested.exchange(false, std::memory_order_acq_rel))
        for (size_t i = 0; i < blockSettings.filters.size(); ++i) {
            effectiveFilterGainDb[i] = blockSettings.filters[i].gain;
            filterGainSmoothers[i].setCurrentAndTargetValue(effectiveFilterGainDb[i]);
        }
    const auto gainSmoothingChanged = updateSmoothedFilterGains(processSamples);
    const auto filtersDirty = coherent && eqlFiltersDirty.exchange(false, std::memory_order_acq_rel);

    if (gainSmoothingChanged || filtersDirty)
        updateFilters();

    const auto filterCount = blockSettings.count;

    for (int filterIndex = 0; filterIndex < filterCount; ++filterIndex)
        processFilterSection(buffer, filterIndex, processChannels);
}

void EqlModuleProcessor::processFilterSection(juce::AudioBuffer<float>& targetBuffer,
                                              const int filterIndex,
                                              const int processChannels)
{
    const auto processSignal = [processChannels] (juce::AudioBuffer<float>& signal, auto&& processor)
    {
        processor(signal, processChannels);
    };

    const auto filterArrayIndex = static_cast<size_t>(filterIndex);
    const auto& settings = blockSettings.filters[filterArrayIndex];
    const auto filterType = settings.type;
    const auto filterBypassed = settings.bypass;

    if (filterBypassed)
        return;

    const auto phaseEnabled = settings.phase;
    if (phaseEnabled && !isCutFilterType(filterType) && !isVolumeFilterType(filterType))
    {
        if (filterType == FilterType::bell && settings.order == 0)
            return;
        phaseFirFilters[filterArrayIndex].process(targetBuffer, processChannels);
        return;
    }

    if (isVolumeFilterType(filterType))
    {
        const auto gainDb = effectiveFilterGainDb[filterArrayIndex];

        if (std::abs(gainDb) < 1.0e-6f)
            return;

        const auto gain = juce::Decibels::decibelsToGain(gainDb);
        processSignal(targetBuffer, [gain] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            for (int channel = 0; channel < targetChannels; ++channel)
                targetBufferToProcess.applyGain(channel, 0, targetBufferToProcess.getNumSamples(), gain);
        });
        return;
    }

    const auto slopeDbPerOct = static_cast<double>(getSlopeDbPerOctForOrderChoice(settings.order));

    if (filterType == FilterType::bell)
    {
        if (settings.order == 0)
            return;

        if (bellOrderFilters[filterArrayIndex].front().sectionCount <= 0)
            return;

        const auto bellSlopeBlend = mapBellSlopeToBlend(slopeDbPerOct);
        const auto lowerOrder = bellSlopeBlend.lowerOrder;
        const auto upperOrder = bellSlopeBlend.upperOrder;
        const auto blend = bellSlopeBlend.blend;
        auto& orderFilters = bellOrderFilters[filterArrayIndex];

        processSignal(targetBuffer, [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            if (lowerOrder == upperOrder || blend < 1.0e-6)
            {
                if (lowerOrder > 0)
                    orderFilters[0].process(targetBufferToProcess, targetChannels);
                return;
            }

            if (lowerOrder > 0)
            {
                filterProcessBufferA.makeCopyOf(targetBufferToProcess, true);
                orderFilters[0].process(filterProcessBufferA, targetChannels);
            }
            else
            {
                filterProcessBufferA.makeCopyOf(targetBufferToProcess, true);
            }

            filterProcessBufferB.makeCopyOf(targetBufferToProcess, true);

            if (upperOrder > 0)
                orderFilters[1].process(filterProcessBufferB, targetChannels);

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

        processSignal(targetBuffer, [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            tiltFilters[filterArrayIndex].process(targetBufferToProcess, targetChannels);
        });
        return;
    }

    if (isShelfFilterType(filterType))
    {
        if (shelfOrderFilters[filterArrayIndex].front().stageCount <= 0)
            return;

        processSignal(targetBuffer, [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            auto& orderFilters = shelfOrderFilters[filterArrayIndex];
            const auto slopeBlend = mapShelfSlopeToBlend(slopeDbPerOct);
            const auto lowerOrder = slopeBlend.lowerOrder;
            const auto upperOrder = slopeBlend.upperOrder;
            const auto blend = slopeBlend.blend;

            if (lowerOrder == upperOrder || blend < 1.0e-6)
            {
                orderFilters[0].process(targetBufferToProcess, targetChannels);
                return;
            }

            filterProcessBufferA.makeCopyOf(targetBufferToProcess, true);
            filterProcessBufferB.makeCopyOf(targetBufferToProcess, true);
            orderFilters[0].process(filterProcessBufferA, targetChannels);
            orderFilters[1].process(filterProcessBufferB, targetChannels);

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

        processSignal(targetBuffer, [&] (juce::AudioBuffer<float>& targetBufferToProcess, int targetChannels)
        {
            cutBlendFilters[filterArrayIndex].process(targetBufferToProcess, targetChannels);
        });
    }
}

bool EqlModuleProcessor::syncParameters() noexcept
{
    if (!processingModelUpdatesEnabled) return false;
    const auto version = modelVersion.load(std::memory_order_acquire);
    if ((version & 1u) != 0) return false;
    BlockSettings next;
    next.count = juce::jmin(processingFilterLimit, getActiveFilterCount());
    for (size_t i = 0; i < next.filters.size(); ++i) {
        auto& f = next.filters[i];
        f.type = getFilterTypeForSection(i);
        f.phase = filterPhaseParams[i]->load(std::memory_order_relaxed) >= 0.5f;
        f.bypass = filterBypassParams[i]->load(std::memory_order_relaxed) >= 0.5f;
        f.frequency = filterFrequencyParams[i]->load(std::memory_order_relaxed);
        f.bandwidth = filterBandwidthParams[i]->load(std::memory_order_relaxed);
        f.order = filterOrderParams[i]->getIndex();
        const auto view = gainPresentation(f.phase && getFilterControlAvailability(f.type, f.order).phase);
        f.gain = float(view.toPlain(view.toDisplay(filterGainParams[i]->load(std::memory_order_relaxed))));
        f.frequency = float(frequencyPresentation(f.type).limits.clipValue(f.frequency));
    }
    if (modelVersion.load(std::memory_order_acquire) != version) return false;
    blockSettings = next;
    int latency = 0;
    for (int i = 0; i < next.count; ++i) {
        const auto& f = next.filters[size_t(i)];
        if (f.phase && !f.bypass && !isCutFilterType(f.type) && !isVolumeFilterType(f.type)
            && !(f.type == FilterType::bell && f.order == 0)) latency += phaseFirLatencySamples;
    }
    processingLatency.store(latency, std::memory_order_release);
    return true;
}

int EqlModuleProcessor::getLatencySamples() const noexcept
{
    return processingLatency.load(std::memory_order_acquire);
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

    const auto phase = blockSettings.filters[filterIndex].phase;
    if (cachedState.phase != phase || cachedState.active != active
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
    cachedState.phase = blockSettings.filters[filterIndex].phase;
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
        filterPhaseParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterPhaseParamId(filterIndex));
        filterFrequencyParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterFrequencyParamId(filterIndex));
        filterBandwidthParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterBandwidthParamId(filterIndex));
        filterOrderParams[static_cast<size_t>(filterIndex)] = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter(getFilterOrderParamId(filterIndex)));
        filterGainParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterGainParamId(filterIndex));
        filterBypassParams[static_cast<size_t>(filterIndex)] = parameters.getRawParameterValue(getFilterBypassParamId(filterIndex));
    }

    setActiveFilterCount(1);
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
