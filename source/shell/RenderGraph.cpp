#include "Processor.h"
#include "../modules/shared/BlockChainProcessor.h"
#include "../modules/dyn/Processor.h"
#include "../modules/eql/RangeBank.h"
#include "../routing/Runtime.h"

thread_local AvaAudioProcessor* AvaAudioProcessor::renderingOwner = nullptr;
thread_local AvaAudioProcessor::RenderGraph* AvaAudioProcessor::renderingGraph = nullptr;
thread_local bool AvaAudioProcessor::renderingControlsFrozen = false;

void AvaAudioProcessor::AudioBuffers::prepare(double rate, int size, int latency)
{
    blockSize = size;
    maximumLatency = latency;
    crossoverRouter.prepare(rate, size, int(maxSupportedChannels), latency);
    routingChannels.setSize(routingChannelCount, size);
    selectedInputBuffer.setSize(int(maxSupportedChannels), size);
    routingInputSnapshot.setSize(routingChannelCount, size);
    unselectedDelay.setSize(routingChannelCount, latency + 1);
    unselectedDelay.clear();
}

AvaAudioProcessor::RenderingScope::RenderingScope(AvaAudioProcessor& p, RenderGraph* retained)
    : owner(p), previousOwner(renderingOwner), previousGraph(renderingGraph)
{
    previousFrozen = renderingControlsFrozen;
    if (previousOwner == &owner) { graph = previousGraph; return; }
    if (retained != nullptr) graph = retained;
    else {
        RenderGraph* candidate = nullptr;
        do {
            candidate = owner.activeRenderGraph.load();
            owner.candidateHazard.store(candidate);
        } while (candidate != owner.activeRenderGraph.load());
        acquired = true;
        auto& transition = owner.renderTransition;
        if (candidate != nullptr && candidate->buffers != nullptr && !owner.restoringModel.load(std::memory_order_acquire)) {
            const auto signature = owner.getRenderControlSignature(*candidate);
            if (transition.current == nullptr) {
                transition.current = candidate;
                transition.signature = signature;
            } else if (transition.phase == RenderTransition::Phase::fadeOut && transition.remaining == 0) {
                transition.warmup = -1;
                transition.current = candidate;
                transition.signature = signature;
                transition.phase = RenderTransition::Phase::fadeIn;
                transition.remaining = transition.rampSamples;
                transition.step = (1.0f - transition.gain) / float(transition.remaining);
            } else if (transition.phase != RenderTransition::Phase::fadeOut
                       && (transition.current != candidate || transition.signature != signature)) {
                transition.previousLatency = owner.requestedLatencySamples.load(std::memory_order_acquire);
                transition.warmup = 0;
                transition.phase = RenderTransition::Phase::fadeOut;
                transition.remaining = transition.rampSamples;
                transition.step = -transition.gain / float(transition.remaining);
            }
        }
        graph = transition.current;
        // Candidate must stay protected until the current hazard is installed.
        owner.renderHazard.store(graph);
        owner.candidateHazard.store(nullptr);
        renderingControlsFrozen = transition.phase == RenderTransition::Phase::fadeOut;
    }
    if (graph != nullptr && graph->buffers != nullptr && !renderingControlsFrozen
        && !owner.restoringModel.load(std::memory_order_acquire)) {
        auto& b = *graph->buffers;
        b.settings = owner.getCrossoverSettings();
        b.inputChoice = owner.processingInputParam != nullptr ? juce::roundToInt(owner.processingInputParam->load()) : 0;
        b.instanceMuted = owner.instanceMuteParam != nullptr && owner.instanceMuteParam->load() >= 0.5f;
        b.globalBypass = owner.globalBypassParam != nullptr && owner.globalBypassParam->load() >= 0.5f;
        b.instanceBypass = graph->routing != nullptr && graph->routing->isRootBypassed();
        b.bypassMask = owner.moduleBypasses.load(std::memory_order_acquire);
        b.order = owner.getModuleOrder();
    }
    renderingOwner = &owner;
    renderingGraph = graph;
}
AvaAudioProcessor::RenderingScope::~RenderingScope()
{
    renderingOwner = previousOwner;
    renderingGraph = previousGraph;
    renderingControlsFrozen = previousFrozen;
    if (acquired && owner.renderReclamationPending.exchange(false)) owner.triggerAsyncUpdate();
}

void AvaAudioProcessor::resetRenderTransition() noexcept
{
    renderTransition = {};
    renderTransition.rampSamples = juce::jmax(1, juce::roundToInt(currentSampleRate * 0.0025));
    candidateHazard.store(nullptr);
    renderHazard.store(nullptr);
}

uint64_t AvaAudioProcessor::getRenderControlSignature(const RenderGraph& graph) const noexcept
{
    uint64_t result = uint64_t(moduleBypasses.load()) << 60;
    const auto mix = [&result](uint64_t value) { result ^= value + 0x9e3779b97f4a7c15ULL + (result << 6) + (result >> 2); };
    for (size_t band = 0; band < bandModuleOrders.size(); ++band)
    {
        mix(bandModuleOrders[band].load(std::memory_order_acquire));
        mix(bandHiddenMasks[band].load(std::memory_order_acquire));
        for (const auto& slot : graph.moduleStacks[band])
            mix(uint64_t(slot.uid + 1) << 32 | uint32_t(slot.enabled));
    }
    mix(globalBypassParam != nullptr && globalBypassParam->load() >= 0.5f ? 1u : 0u);
    mix(instanceMuteParam != nullptr && instanceMuteParam->load() >= 0.5f ? 1u : 0u);
    mix(processingInputParam != nullptr ? uint64_t(juce::roundToInt(processingInputParam->load())) : 0u);
    mix(crossoverActiveSplitCountParam != nullptr ? uint64_t(juce::roundToInt(crossoverActiveSplitCountParam->load())) : 0u);
    if (graph.routing != nullptr) mix(graph.routing->getRenderControlSignature());
    return result;
}

void AvaAudioProcessor::applyRenderTransition(juce::AudioBuffer<float>& buffer) noexcept
{
    auto& transition = renderTransition;
    if (transition.phase == RenderTransition::Phase::stable) return;
    if (transition.warmup < 0) transition.warmup = juce::jmax(transition.previousLatency, requestedLatencySamples.load(std::memory_order_acquire));
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
        if (transition.warmup > 0) --transition.warmup;
        else if (transition.remaining > 0) {
            transition.gain = juce::jlimit(0.0f, 1.0f, transition.gain + transition.step);
            if (--transition.remaining == 0) {
                transition.gain = transition.phase == RenderTransition::Phase::fadeOut ? 0.0f : 1.0f;
                if (transition.phase == RenderTransition::Phase::fadeIn) transition.phase = RenderTransition::Phase::stable;
            }
        }
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, sample, buffer.getSample(channel, sample) * transition.gain);
    }
}

bool AvaAudioProcessor::isInstanceBypassed() const noexcept
{
    if (renderingOwner == this && renderingGraph != nullptr && renderingGraph->buffers != nullptr)
        return audioBuffers().instanceBypass;
    return getRenderRouting() != nullptr && getRenderRouting()->isRootBypassed();
}

AvaAudioProcessor::AudioBuffers& AvaAudioProcessor::audioBuffers() const noexcept
{
    jassert(renderingOwner == this && renderingGraph != nullptr && renderingGraph->buffers != nullptr);
    return *renderingGraph->buffers;
}
ava::routing::Runtime* AvaAudioProcessor::getRenderRouting() const noexcept
{
    return renderingOwner == this && renderingGraph != nullptr ? renderingGraph->routing.get() : routingRuntime.get();
}
bool AvaAudioProcessor::isGlobalBypassed() const noexcept
{
    return renderingOwner == this && renderingGraph != nullptr && renderingGraph->buffers != nullptr
        ? audioBuffers().globalBypass : globalBypassParam != nullptr && globalBypassParam->load() >= 0.5f;
}

int AvaAudioProcessor::getMaximumChainLatencySamples() const noexcept
{
    if (renderingOwner == this && renderingGraph != nullptr) return renderingGraph->maximumLatency;
    if (currentSampleRate <= 0.0) return 0;
    // Block chain: negative delay, phase/COMB, Rubber Band and the largest VARISPEED fragment.
    int maximum = 0;
    if (const auto* chain = getBlockChainProcessor())
        maximum = int(std::ceil(currentSampleRate * 0.25)) + 576 + chain->getShiftLatencySamples() + chain->getMaximumVarispeedLatencySamples() + int(std::ceil(currentSampleRate * 0.25)) + 32768;
    if (getDynModuleProcessor() != nullptr) maximum += 16383 + int(std::ceil(currentSampleRate * 0.03));
    if (const auto* bank = getEqlProcessorBank()) {
        int filters = 0;
        for (size_t band = 0; band < bank->getCreatedRangeCount(); ++band)
            filters = juce::jmax(filters, bank->getProcessor(band)->getActiveFilterCount());
        maximum += filters * 4096;
    }
    // A copied chain processor runs exactly ONE block, not all twelve.
    // Reserve an upper bound for the longest individual BAND, not the sum of
    // every copy across all bands (which can allocate enormous audio buffers).
    int worstCopiedPath = 0;
    for (size_t band = 0; band < moduleStacks.size(); ++band) {
        int path = 0;
        for (const auto& slot : moduleStacks[band]) {
            if (!slot.enabled || slot.uid < ava::module_stack::originalCount) continue;
            const auto it = moduleCopies.find(slot.uid);
            if (it == moduleCopies.end()) continue;
            const auto& copy = *it->second;
            if (copy.blocks) {
                switch (slot.type) {
                    case 2: path += int(std::ceil(currentSampleRate * 0.25)); break; // DELAY
                    case 3: case 9: path += 576; break; // PHASE / COMB
                    case 10: path += copy.blocks->getShiftLatencySamples(); break;
                    case 11: path += copy.blocks->getMaximumVarispeedLatencySamples(); break;
                    case module_order::sibil: path += int(std::ceil(currentSampleRate * 0.25)) + 32768; break;
                    default: break;
                }
            } else if (copy.dyn) {
                path += 16383 + int(std::ceil(currentSampleRate * 0.03));
            } else if (copy.eql && band < copy.eql->getCreatedRangeCount()) {
                const auto* eql = copy.eql->getProcessor(band);
                if (eql != nullptr) path += eql->getActiveFilterCount() * 4096;
            }
        }
        worstCopiedPath = juce::jmax(worstCopiedPath, path);
    }
    maximum += worstCopiedPath;
    return maximum;
}

void AvaAudioProcessor::publishRenderGraph(bool newBuffers)
{
    const juce::ScopedLock lock(processingLock); // serializes writers only, never taken by render
    if (publishingGraph.exchange(true)) return;
    const juce::ScopeGuard publicationFinished {[this] { publishingGraph.store(false); }};
    auto graph = std::make_shared<RenderGraph>();
    graph->blocks = blockChainModuleProcessor; graph->dyn = dynModuleProcessor; graph->eql = eqlProcessorBank;
    graph->moduleStacks = moduleStacks;
    graph->moduleCopies = moduleCopies;
    if (routingRuntime != nullptr) {
        auto next = std::make_shared<ava::routing::Runtime>(*this, routingRuntime.get());
        next->synchronize(parameters.state);
        if (currentSampleRate > 0.0) next->prepare(currentSampleRate, preparedBlockSize, routingChannelCount);
        routingRuntime = std::move(next);
    }
    graph->routing = routingRuntime;
    if (graph->eql != nullptr)
        for (size_t band = 0; band < graph->eql->getCreatedRangeCount(); ++band)
            graph->filterLimits[band] = graph->eql->getProcessor(band)->getActiveFilterCount();
    for (const auto& [uid, copy] : graph->moduleCopies)
        if (copy->eql != nullptr)
        {
            auto& limits = graph->copiedEqlFilterLimits[uid];
            for (size_t band = 0; band < copy->eql->getCreatedRangeCount(); ++band)
                if (const auto* processor = copy->eql->getProcessor(band))
                    limits[band] = processor->getActiveFilterCount();
        }
    const auto maximum = getMaximumChainLatencySamples();
    graph->maximumLatency = maximum;
    const auto pathMaximum = graph->routing != nullptr ? juce::jmax(maximum, graph->routing->getMaximumLatencySamples()) : maximum;
    if (currentSampleRate > 0.0) tailLengthSeconds.store(2.0 * double(pathMaximum) / currentSampleRate, std::memory_order_relaxed);
    if (publishedRenderGraph != nullptr && !newBuffers && publishedRenderGraph->buffers != nullptr
        && publishedRenderGraph->buffers->maximumLatency >= maximum)
        graph->buffers = publishedRenderGraph->buffers;
    else if (currentSampleRate > 0.0 && preparedBlockSize > 0) {
        graph->buffers = std::make_shared<AudioBuffers>();
        graph->buffers->prepare(currentSampleRate, preparedBlockSize, maximum);
    }
    auto previous = std::move(publishedRenderGraph);
    publishedRenderGraph = std::move(graph);
    activeRenderGraph.store(publishedRenderGraph.get());
    if (previous != nullptr) retiredRenderGraphs.push_back(std::move(previous));
    if (routingOwner != nullptr && !restoringModel.load() && !routingOwner->restoringModel.load() && !routingOwner->publishingGraph.load()) routingOwner->publishRenderGraph();
    collectRetiredRenderGraphs();
}

void AvaAudioProcessor::collectRetiredRenderGraphs()
{
    // Read the candidate first: transfer installs the current hazard before clearing it.
    const auto candidate = candidateHazard.load();
    const auto hazard = renderHazard.load();
    std::erase_if(retiredRenderGraphs, [hazard, candidate](const auto& retired) {
        return retired.get() != hazard && retired.get() != candidate;
    });
    renderReclamationPending.store(!retiredRenderGraphs.empty());
}

void AvaAudioProcessor::syncAudioModuleParameters()
{
    if (!renderingControlsFrozen) {
        if (auto* p = getBlockChainProcessor()) p->syncParameters();
        if (auto* p = getDynModuleProcessor()) {
            p->setProcessingInputChannel(audioBuffers().inputChoice);
            p->syncParameters(false, true);
        }
    }
    if (auto* p = getEqlProcessorBank()) p->syncParameters(renderingGraph->filterLimits, !renderingControlsFrozen);
    if (renderingGraph != nullptr)
        for (const auto& [uid, copy] : renderingGraph->moduleCopies) {
            juce::ignoreUnused(uid);
            if (copy->blocks != nullptr && !renderingControlsFrozen) copy->blocks->syncParameters();
            if (copy->dyn != nullptr && !renderingControlsFrozen) {
                copy->dyn->setProcessingInputChannel(audioBuffers().inputChoice);
                copy->dyn->syncParameters(false,true);
            }
            if (copy->eql != nullptr) {
                const auto limits = renderingGraph->copiedEqlFilterLimits.find(uid);
                if (limits != renderingGraph->copiedEqlFilterLimits.end())
                    copy->eql->syncParameters(limits->second, !renderingControlsFrozen);
            }
        }
}
