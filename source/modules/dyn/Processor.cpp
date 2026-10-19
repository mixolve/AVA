#include "Processor.h"

#include "../shared/DspUtilities.h"

DynModuleProcessor::DynModuleProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
      valueTreeState(moduleParameterHost, &undoManager, "dyn_state", createParameterLayout())
{
    cacheParameterPointers();
    spectralProcessors[0] = std::make_unique<DynSpectralProcessor>(ownerProcessor);
    setParameterListenersEnabled(true);
}

DynModuleProcessor::~DynModuleProcessor()
{
    setParameterListenersEnabled(false);
}

void DynModuleProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    processorBank.prepare(sampleRate, samplesPerBlock, ownerProcessor.getTotalNumOutputChannels());
    syncParameters(true);
    processorBank.reset();
    spectralSampleRate = sampleRate;
    spectralBlockSize = samplesPerBlock;
    domainFadeSamples = juce::jmax(1, juce::roundToInt(sampleRate * 0.005));
    domainFadeRemaining.fill(0);
    lastDomainOutput = {};
    for (size_t range = 0; range < numRanges; ++range)
    {
        processingSidechainChannel[range] = juce::roundToInt(loadParameter(range, dyn::parameters::ParameterSlot::sidechainChannel));
        processingSpectral[range] = isSpectralRange(range);
        processingTransust[range] = isTransustRange(range);
        processingCorrelation[range] = isCorrelationRange(range);
        if (spectralProcessors[range] != nullptr)
            spectralProcessors[range]->prepareToPlay(sampleRate, samplesPerBlock);
    }
}

void DynModuleProcessor::releaseResources()
{
    spectralSampleRate = 0.0;
    processorBank.releaseResources();
    for (auto& spectral : spectralProcessors)
        if (spectral != nullptr) spectral->releaseResources();
}

void DynModuleProcessor::resetProcessingState() noexcept
{
    processorBank.reset();
    for (auto& spectral : spectralProcessors)
        if (spectral != nullptr) spectral->resetProcessingState();
    domainFadeRemaining.fill(0);
    lastDomainOutput = {};
}

void DynModuleProcessor::processBlock(juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    ava::modules::dsp::clearOutputOnlyChannels(ownerProcessor, buffer);
    syncParameters(false, true);
    processRange(0, buffer);
}

juce::AudioProcessorValueTreeState& DynModuleProcessor::getValueTreeState() noexcept
{
    return valueTreeState;
}

const juce::AudioProcessorValueTreeState& DynModuleProcessor::getValueTreeState() const noexcept
{
    return valueTreeState;
}

juce::UndoManager& DynModuleProcessor::getUndoManager() noexcept
{
    return undoManager;
}

const juce::UndoManager& DynModuleProcessor::getUndoManager() const noexcept
{
    return undoManager;
}

dyn::dsp::ProcessorBank::RangeLatencies DynModuleProcessor::getRangeLatencies() const noexcept
{
    auto latencies = processorBank.getRangeLatencies();
    for (size_t range = 0; range < numRanges; ++range)
        if (isSpectralRange(range) && spectralProcessors[range] != nullptr)
            latencies[range] = spectralProcessors[range]->getLatencySamples(juce::roundToInt(
                spectralSampleRate * loadParameter(range, dyn::parameters::ParameterSlot::lookahead) * 0.001));
    return latencies;
}

size_t DynModuleProcessor::ensureRangeCount(const size_t rangeCount)
{
    const auto createdRangeCount = processorBank.ensureRangeCount(rangeCount);
    processorBank.setRangeParameters(currentRangeParameters);
    for (size_t range = 0; range < createdRangeCount; ++range)
        if (spectralProcessors[range] == nullptr)
        {
            spectralProcessors[range] = std::make_unique<DynSpectralProcessor>(ownerProcessor);
            if (spectralSampleRate > 0.0)
                spectralProcessors[range]->prepareToPlay(spectralSampleRate, spectralBlockSize);
        }
    return createdRangeCount;
}

size_t DynModuleProcessor::getCreatedRangeCount() const noexcept
{
    return processorBank.getCreatedRangeCount();
}

void DynModuleProcessor::processRange(const size_t rangeIndex, juce::AudioBuffer<float>& buffer,
                                      const juce::AudioBuffer<float>* routingInputs, const int routingOffset)
{
    if (rangeIndex >= numRanges || buffer.getNumChannels() <= 0) return;
    const auto channelChoice = juce::roundToInt(loadParameter(rangeIndex, dyn::parameters::ParameterSlot::sidechainChannel));
    const auto selected = ava::channels::decode(channelChoice);
    juce::AudioBuffer<float> sidechainView;
    std::array<float*, 2> keyData {};
    const auto keyAvailable = routingInputs != nullptr && selected[0] < routingInputs->getNumChannels()
        && routingOffset >= 0 && routingOffset + buffer.getNumSamples() <= routingInputs->getNumSamples();
    if (keyAvailable)
    {
        const auto right = selected[1] >= 0 && selected[1] < routingInputs->getNumChannels()
            && ownerProcessor.getChannelCountOfBus(true, selected[1] / 2) > selected[1] % 2
                ? selected[1] : selected[0];
        keyData = { const_cast<float*>(routingInputs->getReadPointer(selected[0], routingOffset)),
                    const_cast<float*>(routingInputs->getReadPointer(right, routingOffset)) };
        sidechainView.setDataToReferTo(keyData.data(), 2, buffer.getNumSamples());
    }
    const auto* sidechain = keyAvailable ? &sidechainView : nullptr;
    constexpr int sidechainOffset = 0;
    const auto spectral = isSpectralRange(rangeIndex);
    const auto transust = isTransustRange(rangeIndex);
    const auto correlation = isCorrelationRange(rangeIndex);
    if (spectral != processingSpectral[rangeIndex] || transust != processingTransust[rangeIndex]
        || correlation != processingCorrelation[rangeIndex]
        || channelChoice != processingSidechainChannel[rangeIndex])
    {
        processingSidechainChannel[rangeIndex] = channelChoice;
        processingSpectral[rangeIndex] = spectral;
        processingTransust[rangeIndex] = transust;
        processingCorrelation[rangeIndex] = correlation;
        if (auto* core = processorBank.getProcessor(rangeIndex)) core->reset();
        if (spectralProcessors[rangeIndex] != nullptr) spectralProcessors[rangeIndex]->resetProcessingState();
        domainFadeRemaining[rangeIndex] = domainFadeSamples;
    }
    if (spectral)
    {
        processSpectralRange(rangeIndex, buffer, sidechain, sidechainOffset);
        applyDomainFade(rangeIndex, buffer);
        return;
    }
    auto* core = processorBank.getProcessor(rangeIndex);
    if (core == nullptr || buffer.getNumChannels() <= 0)
        return;
    auto* left = buffer.getWritePointer(0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    const auto sidechainAvailable = sidechain != nullptr && sidechain->getNumChannels() > 0
        && sidechainOffset >= 0 && sidechainOffset + buffer.getNumSamples() <= sidechain->getNumSamples();
    const auto* keyL = sidechainAvailable ? sidechain->getReadPointer(0, sidechainOffset) : nullptr;
    const auto* keyR = sidechainAvailable && sidechain->getNumChannels() > 1
        ? sidechain->getReadPointer(1, sidechainOffset) : keyL;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto result = core->processSample(left[sample], right != nullptr ? right[sample] : left[sample],
            keyL != nullptr ? keyL[sample] : 0.0, keyR != nullptr ? keyR[sample] : 0.0);
        left[sample] = static_cast<float>(result.left);
        if (right != nullptr)
            right[sample] = static_cast<float>(result.right);
    }
    applyDomainFade(rangeIndex, buffer);
}

void DynModuleProcessor::setProcessingInputChannel(const int choice) noexcept
{
    if (choice == processingInputChannel) return;
    processingInputChannel = choice;
    markParametersDirty();
}

bool DynModuleProcessor::isSidechainChannelAvailable(const int choice) const
{
    return ava::channels::isAvailable(ownerProcessor, choice);
}

void DynModuleProcessor::markParametersDirty() noexcept
{
    parametersDirty.store(true, std::memory_order_relaxed);
}

juce::AudioProcessorValueTreeState::ParameterLayout DynModuleProcessor::createParameterLayout()
{
    return dyn::parameters::createParameterLayout();
}

float DynModuleProcessor::loadParameter(size_t rangeIndex, dyn::parameters::ParameterSlot slot) const noexcept
{
    if (rangeIndex >= numRanges) return 0.0f;
    const auto* value = rawRangeParameters[rangeIndex][dyn::parameters::toIndex(slot)];
    return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f;
}

bool DynModuleProcessor::isSpectralRange(size_t rangeIndex) const noexcept
{
    return loadParameter(rangeIndex, dyn::parameters::ParameterSlot::domain) >= 0.5f;
}

bool DynModuleProcessor::isCorrelationRange(size_t rangeIndex) const noexcept
{
    return isSpectralRange(rangeIndex)
        && loadParameter(rangeIndex, dyn::parameters::ParameterSlot::mode) >= 0.5f;
}

bool DynModuleProcessor::isTransustRange(size_t rangeIndex) const noexcept
{
    return ! isSpectralRange(rangeIndex)
        && loadParameter(rangeIndex, dyn::parameters::ParameterSlot::mode) >= 0.5f;
}

void* DynModuleProcessor::getDomainEditorIdentity(size_t rangeIndex) const noexcept
{
    if (isCorrelationRange(rangeIndex)) return const_cast<bool*>(&processingCorrelation[rangeIndex]);
    if (isSpectralRange(rangeIndex) && rangeIndex < numRanges)
        return spectralProcessors[rangeIndex].get();
    if (isTransustRange(rangeIndex)) return const_cast<bool*>(&processingTransust[rangeIndex]);
    return const_cast<DynModuleProcessor*>(this);
}

void DynModuleProcessor::processSpectralRange(size_t rangeIndex, juce::AudioBuffer<float>& buffer,
                                             const juce::AudioBuffer<float>* sidechain, const int sidechainOffset)
{
    auto* spectral = spectralProcessors[rangeIndex].get();
    if (spectral == nullptr) return;
    const auto& p = currentRangeParameters[rangeIndex];
    using Slot = dyn::parameters::ParameterSlot;
    static constexpr std::array<int, 5> sizes {1024, 2048, 4096, 8192, 16384};
    static constexpr std::array<int, 3> overlaps {8, 16, 32};
    DynSpectralProcessor::ProcessingSettings settings;
    settings.fftSize = sizes[static_cast<size_t>(juce::jlimit(0, 4, juce::roundToInt(loadParameter(rangeIndex, Slot::fftSize))))];
    settings.overlapFactor = overlaps[static_cast<size_t>(juce::jlimit(0, 2, juce::roundToInt(loadParameter(rangeIndex, Slot::fftOverlap))))];
    settings.slopeDbPerOct = loadParameter(rangeIndex, Slot::spectralSlope);
    settings.thresholdDb = p.threshold;
    settings.thresholdHysteresisDb = p.thresholdHysteresisDb;
    settings.adaptiveAmount = p.adaptive;
    settings.adaptiveOffset = p.adaptiveOffset;
    settings.adaptiveAttackMs = p.adaptiveAttack;
    settings.adaptiveHoldMs = p.adaptiveHold;
    settings.adaptiveReleaseMs = p.adaptiveRelease;
    settings.adaptiveTimeMs = p.adaptiveTime;
    settings.attackMs = p.attackMs;
    settings.holdMs = p.holdMs;
    settings.delayMs = p.delayMs;
    settings.autoRelease = static_cast<float>(ava::modules::dsp::roundToParameterStep(p.autoRelease)) * 0.01f;
    settings.releaseMemoryMs = p.releaseMemoryMs;
    settings.releaseDepth = static_cast<float>(ava::modules::dsp::roundToParameterStep(p.releaseDepth)) * 0.01f;
    settings.releasePeakMs = p.releasePeakMs;
    settings.releaseRmsMs = p.releaseRmsMs;
    settings.attackCurve = p.attackCurve;
    settings.releaseCurve = p.releaseCurve;
    settings.releaseType = p.releaseType * 0.01f;
    settings.tension = p.tension;
    settings.tensionFloorDb = p.tensionFloor;
    settings.tensionHysteresis = p.tensionHysteresis * 0.01f;
    settings.kneeDb = p.compKnee;
    settings.ratio = p.ratio >= 99.995f ? std::numeric_limits<float>::infinity() : p.ratio;
    settings.progression = static_cast<float>(ava::modules::dsp::roundToParameterStep(p.progression)) * 0.01f;
    settings.progressionSpan = p.progressionSpan;
    settings.rangeDb = p.range;
    settings.rangeCurve = p.rangeCurve * 0.01f;
    settings.lookaheadMs = p.lookaheadMs;
    settings.rmsMix = juce::jlimit(0.0f, 1.0f, static_cast<float>(ava::modules::dsp::roundToParameterStep(p.peakRms)) * 0.01f);
    settings.peakCrestDb = p.peakCrestDb;
    settings.rmsTimeMs = p.rmsTime;
    settings.inputDb = p.input;
    settings.makeupDb = p.makeUp;
    settings.outputDb = p.output;
    settings.mix = p.mix * 0.01f;
    settings.useSidechainInput = p.useSidechainInput;
    settings.sidechainListen = p.sidechainListen;
    if (isCorrelationRange(rangeIndex))
    {
        settings.correlationMode = true;
        settings.progressionSpan = loadParameter(rangeIndex, Slot::corrProgressionSpan);
        settings.correlationPeakCrest = loadParameter(rangeIndex, Slot::corrPeakCrest);
        settings.correlationThresholdHysteresis = loadParameter(rangeIndex, Slot::corrThresholdHysteresis);
        settings.correlationThreshold = loadParameter(rangeIndex, Slot::corrThreshold);
        settings.correlationSlopePerOctave = loadParameter(rangeIndex, Slot::corrSlope);
        settings.correlationSmoothing = loadParameter(rangeIndex, Slot::corrSmoothing);
        settings.correlationImpact = loadParameter(rangeIndex, Slot::corrImpact);
        settings.correlationTensionFloor = loadParameter(rangeIndex, Slot::corrTensionFloor);
        settings.adaptiveOffset = loadParameter(rangeIndex, Slot::corrOffset);
        settings.correlationKnee = loadParameter(rangeIndex, Slot::corrKnee);
    }
    spectral->processBlock(buffer, settings, p.delta, sidechain, sidechainOffset);
}

void DynModuleProcessor::applyDomainFade(size_t rangeIndex, juce::AudioBuffer<float>& buffer) noexcept
{
    auto& remaining = domainFadeRemaining[rangeIndex];
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    const auto previous = lastDomainOutput[rangeIndex];
    for (int i = 0; i < buffer.getNumSamples() && remaining > 0; ++i, --remaining)
    {
        const auto amount = 1.0f - static_cast<float>(remaining) / static_cast<float>(domainFadeSamples);
        for (int c = 0; c < channels; ++c)
            buffer.setSample(c, i, previous[static_cast<size_t>(c)] * (1.0f - amount) + buffer.getSample(c, i) * amount);
    }
    if (buffer.getNumSamples() > 0)
        for (int c = 0; c < channels; ++c)
            lastDomainOutput[rangeIndex][static_cast<size_t>(c)] = buffer.getSample(c, buffer.getNumSamples() - 1);
}
