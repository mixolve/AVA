#include "ShiftEngine.h"
#include "../eql/CutDesign.h"
#include <rubberband/RubberBandStretcher.h>
#include <algorithm>
#include <cmath>
namespace ava::modules::chain::dsp {
namespace {
// Adapter to EQL's shared coefficient designer, without a parameter processor.
struct CutCoefficients {
    struct Section {
        std::array<double, 3> b {1.0, 0.0, 0.0};
        std::array<double, 2> a {};
        void setIdentity() noexcept { b = {1.0, 0.0, 0.0}; a = {}; }
    };
    std::array<Section, 4> sections;
    int stageCount = 0;
    void setIdentity() noexcept { for (auto& section : sections) section.setIdentity(); stageCount = 0; }
    void reset() noexcept {}
};
}
using Stretcher = RubberBand::RubberBandStretcher;
namespace {
constexpr std::array<int, 3> windows {Stretcher::OptionWindowShort, Stretcher::OptionWindowStandard, Stretcher::OptionWindowLong};
constexpr std::array<int, 3> pitchModes {Stretcher::OptionPitchHighSpeed, Stretcher::OptionPitchHighQuality, Stretcher::OptionPitchHighConsistency};
constexpr std::array<int, 3> detectors {Stretcher::OptionDetectorCompound, Stretcher::OptionDetectorPercussive, Stretcher::OptionDetectorSoft};
constexpr std::array<int, 3> transients {Stretcher::OptionTransientsCrisp, Stretcher::OptionTransientsMixed, Stretcher::OptionTransientsSmooth};
}
ShiftEngine::FilterBank ShiftEngine::designFilters(int order, double q, double frequency, double sampleRate) noexcept
{
    FilterBank result;
    frequency = juce::jlimit(2.0, sampleRate * .5 * nyquistSafetyFactor, frequency);
    result.stages = order == 1 ? 2 : order / 2;
    result.highPolarity = order > 1 && (order / 2) % 2 != 0 ? -1.0 : 1.0;
    const auto fromArray = [](const auto& values) {
        Section section;
        for (size_t i = 0; i < 3; ++i) section.b[i] = double(values[i]) / double(values[3]);
        section.a = {double(values[4]) / double(values[3]), double(values[5]) / double(values[3])};
        return section;
    };
    for (size_t branch = 0; branch < 2; ++branch) {
        const auto type = branch == 0 ? EqlModuleProcessor::FilterType::highCut : EqlModuleProcessor::FilterType::lowCut;
        CutCoefficients base;
        const auto baseOrder = order == 1 ? 1 : order / 2;
        ava::eql::designCutOrderFilter(base, type, baseOrder, frequency, sampleRate, false);
        if (order == 1) {
            result.coefficients[branch][0].b = base.sections[0].b;
            result.coefficients[branch][0].a = base.sections[0].a;
            // A first-order cut has no resonant Q. Keep its 6 dB/oct slope and
            // apply the same signed resonance correction to both branches.
            if (q != 0.0)
                result.coefficients[branch][1] = fromArray(juce::dsp::IIR::ArrayCoefficients<double>::makePeakFilter(
                    sampleRate, frequency, std::sqrt(.5), std::exp2(q)));
            continue;
        }
        const auto qScale = std::exp2(q / result.stages);
        const auto makeCut = [&](double stageQ) {
            return fromArray(branch == 0
                ? juce::dsp::IIR::ArrayCoefficients<double>::makeLowPass(sampleRate, frequency, stageQ)
                : juce::dsp::IIR::ArrayCoefficients<double>::makeHighPass(sampleRate, frequency, stageQ));
        };
        int write = 0;
        for (int section = 0; section < baseOrder / 2; ++section) {
            Section coefficients;
            if (q == 0.0) {
                coefficients.b = base.sections[size_t(section)].b;
                coefficients.a = base.sections[size_t(section)].a;
            } else {
                const auto index = baseOrder / 2 - 1 - section;
                coefficients = makeCut(computeButterworthStageQ(index, baseOrder) * qScale);
            }
            result.coefficients[branch][size_t(write++)] = coefficients;
            result.coefficients[branch][size_t(write++)] = coefficients;
        }
        // The two first-order Butterworth sections combine into one biquad.
        if ((baseOrder & 1) != 0)
            result.coefficients[branch][size_t(write)] = makeCut(.5 * qScale);
    }
    return result;
}
std::array<double, 2> ShiftEngine::FilterBank::process(double dry, double wet, size_t channel) noexcept
{
    std::array<double, 2> result {dry, wet};
    for (size_t branch = 0; branch < 2; ++branch)
        for (int stage = 0; stage < stages; ++stage) {
            const auto& c = coefficients[branch][size_t(stage)];
            auto& delay = state[branch][channel][size_t(stage)];
            const auto sample = result[branch];
            const auto filtered = c.b[0] * sample + delay[0];
            delay[0] = c.b[1] * sample - c.a[0] * filtered + delay[1];
            delay[1] = c.b[2] * sample - c.a[1] * filtered;
            result[branch] = filtered;
        }
    result[1] *= highPolarity;
    return result;
}
ShiftEngine::ShiftEngine() = default;
ShiftEngine::~ShiftEngine() = default;
void ShiftEngine::prepare(double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    input = {}; output = {}; latency = 0;
    const float* in[] {input[0].data(), input[1].data()};
    float* out[] {output[0].data(), output[1].data()};
    for (size_t window = 0; window < shifters.size(); ++window) {
        auto& engine = shifters[window];
        engine = std::make_unique<Stretcher>(size_t(rate), 2,
            Stretcher::OptionProcessRealTime | Stretcher::OptionEngineFaster |
            Stretcher::OptionThreadingNever | Stretcher::OptionChannelsApart |
            windows[window] | Stretcher::OptionPitchHighConsistency);
        engine->setMaxProcessSize(blockSize);
        // R2 preallocates the common FFT sizes. Visit all supported ratios and
        // modes here as well, including smaller resampling windows, so option
        // automation never has to construct an FFT or grow a buffer.
        for (const auto mode : pitchModes) {
            engine->setPitchOption(mode);
            for (int semi = -24; semi <= 24; ++semi) {
                engine->setPitchScale(std::exp2(double(semi) / 12.0));
                latency = std::max(latency, int(engine->getPreferredStartPad()
                    + engine->getStartDelay() + engine->getSamplesRequired()) + 2 * blockSize);
                engine->process(in, blockSize, false);
                while (engine->available() > 0)
                    engine->retrieve(out, size_t(std::min(blockSize, engine->available())));
            }
        }
        // The built-in resampler has two alternating phase tables. Fractional
        // ratios near 4 need up to 96003 entries (R2 FastestTolerable), more
        // than its default reserve. Warm both tables at their largest size.
        engine->setPitchOption(Stretcher::OptionPitchHighConsistency);
        for (const auto denominator : {24001.0, 24002.0}) {
            engine->setPitchScale(denominator / 96003.0);
            for (int block = 0; block < 32; ++block) {
                engine->process(in, blockSize, false);
                while (engine->available() > 0)
                    engine->retrieve(out, size_t(std::min(blockSize, engine->available())));
            }
        }
    }
    latency = ((latency + blockSize - 1) / blockSize) * blockSize;
    for (auto& channel : dryHistory) channel.assign(size_t(latency + 1), 0.0);
    for (auto& channel : wetQueue) channel.assign(size_t(latency * 4 + blockSize), 0.0f);
    pitch = currentPitch = 0.0; cutoff = -1.0; options = {};
    gains = targetGains = {1.0, 1.0}; gainRamp = 0;
    setParameters(0.0f, 1000.0f, options);
    filters.coefficients = targetCoefficients;
    reset();
}
void ShiftEngine::applyOptions() noexcept
{
    auto& engine = *shifters[size_t(options.window)];
    engine.setPitchOption(pitchModes[size_t(options.pitchMode)]);
    engine.setDetectorOption(detectors[size_t(options.detector)]);
    engine.setTransientsOption(transients[size_t(options.transients)]);
    engine.setPhaseOption(options.independentPhase ? Stretcher::OptionPhaseIndependent : Stretcher::OptionPhaseLaminar);
}
void ShiftEngine::drainOutput() noexcept
{
    auto& engine = *shifters[size_t(options.window)];
    float* out[] {output[0].data(), output[1].data()};
    while (engine.available() > 0) {
        const auto count = engine.retrieve(out, size_t(std::min(blockSize, engine.available())));
        const auto skip = std::min(discard, count); discard -= skip;
        for (size_t sample = skip; sample < count; ++sample) {
            jassert(wetCount < wetQueue[0].size());
            if (wetCount >= wetQueue[0].size()) break;
            for (size_t channel = 0; channel < 2; ++channel) wetQueue[channel][wetWrite] = output[channel][sample];
            wetWrite = (wetWrite + 1) % wetQueue[0].size(); ++wetCount;
        }
    }
}
void ShiftEngine::reset() noexcept
{
    if (!shifters[0]) return;
    for (auto& channel : dryHistory) std::fill(channel.begin(), channel.end(), 0.0);
    filters.state = {}; dryPosition = 0;
    coefficientRamp = 0; filters.coefficients = targetCoefficients;
    previousFilters = filters; filterFade = 0;
    gains = targetGains; gainRamp = 0;
    restartWet();
}
void ShiftEngine::restartWet() noexcept
{
    input = {}; output = {};
    inputCount = 0; wetRead = wetWrite = wetCount = 0;
    warmup = latency; wetMix = 0.0; currentPitch = pitch;
    auto& engine = *shifters[size_t(options.window)];
    applyOptions();
    engine.setPitchScale(std::exp2(currentPitch / 12.0));
    engine.reset();
    engine.setMaxProcessSize(blockSize);
    discard = engine.getStartDelay();
    auto padding = engine.getPreferredStartPad();
    const float* in[] {input[0].data(), input[1].data()};
    while (padding > 0) {
        const auto count = std::min(padding, size_t(blockSize));
        engine.process(in, count, false); drainOutput(); padding -= count;
    }
}
void ShiftEngine::setParameters(float semitones, float frequency, Options next)
{
    next.window = juce::jlimit(0, 2, next.window);
    next.pitchMode = juce::jlimit(0, 2, next.pitchMode);
    next.detector = juce::jlimit(0, 2, next.detector);
    next.transients = juce::jlimit(0, 2, next.transients);
    const auto restart = next.window != options.window || next.pitchMode != options.pitchMode;
    if (next.order != 1 && next.order != 2 && next.order != 4 && next.order != 6 && next.order != 8) next.order = 1;
    next.q = juce::jlimit(-1.0f, 1.0f, next.q);
    next.origGainDb = juce::jlimit(-96.0f, 96.0f, next.origGainDb);
    next.pitchedGainDb = juce::jlimit(-96.0f, 96.0f, next.pitchedGainDb);
    // BP bypasses the gain stage (unity) without muting its audio branch.
    const std::array<double, 2> nextGains {
        next.origGainBypassed ? 1.0 : std::pow(10.0, double(next.origGainDb) / 20.0),
        next.pitchedGainBypassed ? 1.0 : std::pow(10.0, double(next.pitchedGainDb) / 20.0)
    };
    if (nextGains != targetGains) {
        targetGains = nextGains;
        gainRamp = juce::jmax(1, int(rate * .005));
    }
    const auto changed = restart || next.detector != options.detector || next.transients != options.transients
        || next.independentPhase != options.independentPhase;
    const auto orderChanged = next.order != options.order;
    const auto qChanged = std::abs(next.q - options.q) > 1.0e-6f;
    options = next;
    pitch = double(juce::jlimit(-24.0f, 24.0f, semitones));
    const auto nextCutoff = double(juce::jlimit(20.0f, 20000.0f, frequency));
    if (std::abs(cutoff - nextCutoff) > 1.0e-6 || orderChanged || qChanged) {
        const auto initialise = cutoff < 0.0;
        cutoff = nextCutoff;
        auto designed = designFilters(options.order, options.q, cutoff, rate);
        targetCoefficients = designed.coefficients;
        filterFadeLength = juce::jmax(1, int(rate * .005));
        if (initialise || orderChanged) {
            previousFilters = filters;
            filters = designed;
            filterFade = initialise ? 0 : filterFadeLength;
            coefficientRamp = 0;
        } else {
            coefficientRamp = filterFadeLength;
        }
    }
    if (!shifters[0]) return;
    if (restart) restartWet();
    else if (changed) applyOptions();
}
std::array<double, 2> ShiftEngine::process(double left, double right) noexcept
{
    if (!shifters[0] || dryHistory[0].empty()) return {left, right};
    input[0][size_t(inputCount)] = float(left); input[1][size_t(inputCount)] = float(right);
    if (++inputCount == blockSize) {
        currentPitch += std::clamp(pitch - currentPitch, -2.0, 2.0);
        auto& engine = *shifters[size_t(options.window)];
        engine.setPitchScale(std::exp2(currentPitch / 12.0));
        const float* in[] {input[0].data(), input[1].data()};
        engine.process(in, blockSize, false); drainOutput(); inputCount = 0;
    }
    if (coefficientRamp > 0) {
        for (size_t branch = 0; branch < 2; ++branch)
            for (size_t stage = 0; stage < 4; ++stage) {
                auto& current = filters.coefficients[branch][stage];
                const auto& target = targetCoefficients[branch][stage];
                for (size_t i = 0; i < 3; ++i) current.b[i] += (target.b[i] - current.b[i]) / coefficientRamp;
                for (size_t i = 0; i < 2; ++i) current.a[i] += (target.a[i] - current.a[i]) / coefficientRamp;
            }
        --coefficientRamp;
    }
    if (gainRamp > 0) {
        for (size_t branch = 0; branch < 2; ++branch)
            gains[branch] += (targetGains[branch] - gains[branch]) / gainRamp;
        --gainRamp;
    }
    const auto ready = warmup == 0 && wetCount > 0;
    wetMix = std::clamp(wetMix + (ready ? 1.0 : -1.0) / std::max(1.0, rate * .005), 0.0, 1.0);
    const std::array<double, 2> source {left, right}; std::array<double, 2> result {};
    for (size_t channel = 0; channel < 2; ++channel) {
        auto& dry = dryHistory[channel];
        dry[dryPosition] = source[channel];
        const auto drySample = dry[(dryPosition + dry.size() - size_t(latency)) % dry.size()];
        // At unity, use identical samples for an exact complementary split.
        // Retain the delayed original during a window/mode restart, then fade
        // back to the newly primed wet stream without clearing the dry path.
        const auto shifted = ready ? double(wetQueue[channel][wetRead]) : drySample;
        const auto wetSample = std::abs(pitch) < 1.0e-6 ? drySample
            : drySample + wetMix * (shifted - drySample);
        auto branches = filters.process(drySample, wetSample, channel);
        if (filterFade > 0) {
            const auto previous = previousFilters.process(drySample, wetSample, channel);
            const auto mix = 1.0 - double(filterFade) / filterFadeLength;
            for (size_t branch = 0; branch < 2; ++branch)
                branches[branch] = previous[branch] + mix * (branches[branch] - previous[branch]);
        }
        for (size_t branch = 0; branch < 2; ++branch) branches[branch] *= gains[branch];
        // Both SOLO switches enabled means both branches are audible.
        if (options.soloOrig && options.soloPitched)
            result[channel] = branches[0] + branches[1];
        else if (options.soloOrig)
            result[channel] = branches[0];
        else if (options.soloPitched)
            result[channel] = branches[1];
        else
            result[channel] = branches[0] + branches[1];
    }
    if (filterFade > 0) --filterFade;
    if (ready) { wetRead = (wetRead + 1) % wetQueue[0].size(); --wetCount; }
    if (++dryPosition == dryHistory[0].size()) dryPosition = 0;
    if (warmup > 0) --warmup;
    return result;
}
}
