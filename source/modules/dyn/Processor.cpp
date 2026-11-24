#include "Processor.h"
#include "../../shell/Processor.h"

#include "../shared/DspUtilities.h"
#include "TensionTransfer.h"

DynModuleProcessor::DynModuleProcessor(juce::AudioProcessor& owner)
    : ownerProcessor(owner),
      valueTreeState(moduleParameterHost, nullptr, "dyn_state", dyn::parameters::createParameterLayout())
{
    if (auto* instance = dynamic_cast<AvaAudioProcessor*>(&ownerProcessor))
    {
        syncTypeParameter = instance->getValueTreeState().getRawParameterValue(AvaAudioProcessor::paramSyncTypeId);
        bpmParameter = instance->getValueTreeState().getRawParameterValue(AvaAudioProcessor::paramBpmId);
    }
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
    for (size_t range = 0; range < numRanges; ++range)
    {
        processingSidechainChannel[range] = juce::roundToInt(loadParameter(range, dyn::parameters::ParameterSlot::sidechainChannel));
        processingSpectral[range] = isSpectralRange(range);
        processingTransientSustain[range] = isTransientSustainRange(range);
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
}

juce::AudioProcessorValueTreeState& DynModuleProcessor::getValueTreeState() noexcept
{
    return valueTreeState;
}

const juce::AudioProcessorValueTreeState& DynModuleProcessor::getValueTreeState() const noexcept
{
    return valueTreeState;
}

dyn::dsp::ProcessorBank::RangeLatencies DynModuleProcessor::getRangeLatencies() const noexcept
{
    auto latencies = processorBank.getRangeLatencies();
    for (size_t range = 0; range < getCreatedRangeCount(); ++range)
        if (isSpectralRange(range) && spectralProcessors[range] != nullptr)
            latencies[range] = spectralProcessors[range]->getLatencySamples(juce::roundToInt(
                spectralSampleRate * loadParameter(range, dyn::parameters::ParameterSlot::lookahead) * 0.001));
    return latencies;
}

size_t DynModuleProcessor::ensureRangeCount(const size_t rangeCount)
{
    const auto target = std::clamp(rangeCount, size_t(1), numRanges);
    for (size_t range = processorBank.getCreatedRangeCount(); range < target; ++range)
        if (spectralProcessors[range] == nullptr) {
            spectralProcessors[range] = std::make_unique<DynSpectralProcessor>(ownerProcessor);
            if (spectralSampleRate > 0.0) spectralProcessors[range]->prepareToPlay(spectralSampleRate, spectralBlockSize);
        }
    const auto createdRangeCount = processorBank.ensureRangeCount(target);
    markParametersDirty();
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
    const auto transientSustain = isTransientSustainRange(rangeIndex);
    const auto correlation = isCorrelationRange(rangeIndex);
    if (spectral != processingSpectral[rangeIndex] || transientSustain != processingTransientSustain[rangeIndex]
        || correlation != processingCorrelation[rangeIndex]
        || channelChoice != processingSidechainChannel[rangeIndex])
    {
        processingSidechainChannel[rangeIndex] = channelChoice;
        processingSpectral[rangeIndex] = spectral;
        processingTransientSustain[rangeIndex] = transientSustain;
        processingCorrelation[rangeIndex] = correlation;
        if (auto* core = processorBank.getProcessor(rangeIndex)) core->reset();
        if (spectralProcessors[rangeIndex] != nullptr) spectralProcessors[rangeIndex]->resetProcessingState();
    }
    if (spectral)
    {
        processSpectralRange(rangeIndex, buffer, sidechain, sidechainOffset);
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

bool DynModuleProcessor::listensToOtherInput(size_t rangeIndex, int inputChoice) const noexcept
{
    using Slot = dyn::parameters::ParameterSlot;
    return loadParameter(rangeIndex, Slot::sidechainListen) >= 0.5f
        && juce::roundToInt(loadParameter(rangeIndex, Slot::sidechainChannel)) != inputChoice;
}

float DynModuleProcessor::loadParameter(size_t rangeIndex, dyn::parameters::ParameterSlot slot) const noexcept
{
    if (rangeIndex >= numRanges) return 0.0f;
    if (isParameterBypassed(rangeIndex, slot)) return dyn::parameters::bypassValue(slot);
    const auto* value = rawRangeParameters[rangeIndex][dyn::parameters::toIndex(slot)];
    return value != nullptr ? value->load(std::memory_order_relaxed) : 0.0f;
}

bool DynModuleProcessor::isParameterBypassed(size_t rangeIndex, dyn::parameters::ParameterSlot slot) const noexcept
{
    if (rangeIndex >= numRanges) return false;
    const auto* value = rawRangeBypasses[rangeIndex][dyn::parameters::toIndex(slot)];
    return value != nullptr && value->load(std::memory_order_relaxed) >= 0.5f;
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

bool DynModuleProcessor::isTransientSustainRange(size_t rangeIndex) const noexcept
{
    return ! isSpectralRange(rangeIndex)
        && loadParameter(rangeIndex, dyn::parameters::ParameterSlot::mode) >= 0.5f;
}

void* DynModuleProcessor::getDomainEditorIdentity(size_t rangeIndex) const noexcept
{
    if (isCorrelationRange(rangeIndex)) return const_cast<bool*>(&processingCorrelation[rangeIndex]);
    if (isSpectralRange(rangeIndex) && rangeIndex < numRanges)
        return spectralProcessors[rangeIndex].get();
    if (isTransientSustainRange(rangeIndex)) return const_cast<bool*>(&processingTransientSustain[rangeIndex]);
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
    settings.adaptiveTimeMs = p.adaptiveTime;
    settings.attackMs = p.attackMs;
    settings.holdMs = p.holdMs;
    settings.autoRelease = static_cast<float>(ava::modules::dsp::roundToParameterStep(p.autoRelease)) * 0.01f;
    settings.releaseMemoryMs = p.releaseMemoryMs;
    settings.releaseDepth = static_cast<float>(ava::modules::dsp::roundToParameterStep(p.releaseDepth)) * 0.01f;
    settings.releasePeakMs = p.releasePeakMs;
    settings.releaseRmsMs = p.releaseRmsMs;
    settings.attackCurve = p.attackCurve;
    settings.releaseCurve = p.releaseCurve;
    settings.tension = p.tension;
    settings.tensionFloorDb = p.tensionFloor;
    settings.tensionHysteresis = dyn_tension::scaleHysteresis(p.tensionHysteresis * 0.01f);
    settings.kneeDb = p.compKnee;
    settings.ratio = p.ratio >= 99.995f ? std::numeric_limits<float>::infinity() : p.ratio;
    settings.rangeDb = p.range;
    settings.rangeCurve = p.rangeCurve * 0.01f;
    settings.lookaheadMs = p.lookaheadMs;
    settings.lookaheadCurve = p.lookaheadCurve;
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

