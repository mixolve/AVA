#include "Engine.h"
#include <CDSPResampler.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <SoundTouch.h>
#include <algorithm>
#include <cmath>
namespace ava::modules::chain::dsp {
namespace {
int rounded(double value) noexcept { return int(std::lround(value)); }
}
// A converter has a fixed ratio. Construction, filter-cache access and disposal
// happen outside process(): the audio thread exclusively owns the active bank.
class VarispeedEngine::Resamplers {
public:
    explicit Resamplers(double rate) {
        build(banks[0], rate);
        banks[0].state.store(inUse);
        requested.store(rate);
        worker = std::thread([this] {
            while (!stopping.load(std::memory_order_relaxed)) {
                const auto rateToBuild = requested.load(std::memory_order_acquire);
                bool found = false;
                for (auto& bank : banks)
                    if (bank.state.load(std::memory_order_acquire) != empty && std::abs(bank.rate.load() - rateToBuild) < 1.0e-12) found = true;
                if (!found) {
                    for (auto& bank : banks) {
                        auto expected = bank.state.load(std::memory_order_acquire);
                        if ((expected == empty || expected == ready)
                            && bank.state.compare_exchange_strong(expected, building, std::memory_order_acq_rel)) {
                            build(bank, rateToBuild);
                            bank.state.store(ready, std::memory_order_release);
                            break;
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }
    ~Resamplers() {
        stopping.store(true, std::memory_order_relaxed);
        if (worker.joinable()) worker.join();
    }
    void request(double rate) noexcept { requested.store(rate, std::memory_order_release); }
    double begin(double rate, bool offline) {
        request(rate);
        if (std::abs(banks[size_t(current)].rate.load() - rate) >= 1.0e-12) {
            for (size_t i = 0; i < banks.size(); ++i) {
                int expected = ready;
                if (!banks[i].state.compare_exchange_strong(expected, inUse, std::memory_order_acq_rel)) continue;
                if (std::abs(banks[i].rate.load() - rate) < 1.0e-12) {
                    banks[size_t(current)].state.store(ready, std::memory_order_release);
                    current = int(i);
                    break;
                }
                banks[i].state.store(ready, std::memory_order_release);
            }
        }
        // Offline rendering has no callback deadline. Honour every requested
        // ratio synchronously instead of depending on worker scheduling.
        if (offline && std::abs(banks[size_t(current)].rate.load() - rate) >= 1.0e-12) {
            for (size_t i = 0; i < banks.size(); ++i) {
                auto expected = banks[i].state.load(std::memory_order_acquire);
                if ((expected == empty || expected == ready)
                    && banks[i].state.compare_exchange_strong(expected, building, std::memory_order_acq_rel)) {
                    build(banks[i], rate);
                    banks[i].state.store(inUse, std::memory_order_release);
                    banks[size_t(current)].state.store(ready, std::memory_order_release);
                    current = int(i);
                    break;
                }
            }
        }
        clear();
        return banks[size_t(current)].rate.load();
    }
    void clear() noexcept { for (auto& channel : banks[size_t(current)].channels) channel->clear(); }
    int process(std::array<std::array<double, blockSize>, 2>& samplesIn, int count,
                std::array<double*, 2>& samplesOut) noexcept {
        auto& channels = banks[size_t(current)].channels;
        const auto frames = channels[0]->process(samplesIn[0].data(), count, samplesOut[0]);
        const auto rightFrames = channels[1]->process(samplesIn[1].data(), count, samplesOut[1]);
        return std::min(frames, rightFrames);
    }
private:
    enum { empty, building, ready, inUse };
    struct Bank {
        std::array<std::unique_ptr<r8b::CDSPResampler24>, 2> channels;
        std::atomic<int> state {empty};
        std::atomic<double> rate {0.0};
    };
    static void build(Bank& bank, double rate) {
        for (auto& channel : bank.channels) {
            channel = std::make_unique<r8b::CDSPResampler24>(rate, 1.0, blockSize, 2.0);
            std::array<double, blockSize> silence {};
            double* output = nullptr;
            for (int block = 0; block < 64; ++block) channel->process(silence.data(), blockSize, output);
            channel->clear();
        }
        bank.rate.store(rate);
    }
    std::array<Bank, 3> banks;
    int current = 0;
    std::atomic<double> requested {1.0};
    std::atomic<bool> stopping {false};
    std::thread worker;
};
VarispeedEngine::VarispeedEngine() = default;
VarispeedEngine::~VarispeedEngine() = default;
void VarispeedEngine::prepare(double rate) { prepare(rate, Parameters {}); }
void VarispeedEngine::prepare(double rate, Parameters initial)
{
    sampleRate = rate > 0.0 ? rate : 44100.0;
    maximumInputFrames = int(std::ceil(sampleRate * maximumCaptureMs / 1000.0));
    maximumOutputFrames = maximumInputFrames * 4 + 4;
    arenaFrames = maximumInputFrames * 32;
    arena.assign(size_t(arenaFrames) * 2, 0.0f);
    preRollFrames = std::max(1, rounded(sampleRate * .005));
    history.assign(size_t(preRollFrames) * 2, 0.0f);
    detectorReleaseCoefficient = std::exp(-1.0 / (sampleRate * .005));
    resamplers = std::make_unique<Resamplers>(std::clamp(double(initial.rate), .25, 4.0));
    struct Preset { int sequence, seek, overlap, taps; bool quick, antiAlias; };
    // DEFAULT, FAST, HIGH QUALITY: settings from Cockos' published wrapper.
    constexpr std::array<Preset, 3> presets {{{82, 14, 12, 32, false, true},
        {164, 7, 6, 32, true, false}, {41, 28, 12, 128, false, true}}};
    for (size_t index = 0; index < stretchers.size(); ++index) {
        auto& instance = stretchers[index];
        instance = std::make_unique<soundtouch::SoundTouch>();
        instance->setSampleRate(unsigned(sampleRate)); instance->setChannels(2);
        const auto& preset = presets[index];
        instance->setSetting(SETTING_SEQUENCE_MS, preset.sequence);
        instance->setSetting(SETTING_SEEKWINDOW_MS, preset.seek);
        instance->setSetting(SETTING_OVERLAP_MS, preset.overlap);
        instance->setSetting(SETTING_USE_QUICKSEEK, preset.quick ? 1 : 0);
        instance->setSetting(SETTING_AA_FILTER_LENGTH, preset.taps);
        instance->setSetting(SETTING_USE_AA_FILTER, preset.antiAlias ? 1 : 0);
        // Each preset owns its buffers. Reserve capacities at all rate extremes
        // before audio starts; selecting a preset only switches the active pointer.
        for (double tempo : {.25, .5, 1., 2., 4.}) {
            instance->clear(); instance->setTempo(tempo);
            const auto warmup = 2 * instance->getSetting(SETTING_INITIAL_LATENCY) + int(sampleRate * .35) + 4096;
            for (int frame = 0; frame < warmup; frame += blockSize) {
                instance->putSamples(silence.data(), blockSize);
                while (instance->numSamples() > 0) instance->receiveSamples(output.data(), blockSize);
            }
        }
        instance->clear();
    }
    stretcher = stretchers[size_t(std::clamp(initial.quality, 0, 2))].get();
    setParameters(initial);
    usingTransport = false; lastTransportPlaying.reset(); restartAt(0);
}
void VarispeedEngine::reset() noexcept { usingTransport = false; lastTransportPlaying.reset(); restartAt(0); }
void VarispeedEngine::restartAt(int64_t sample) noexcept
{
    position = sample;
    for (auto& voice : voices) { voice.ready = false; voice.stored = voice.playbackFrames = 0; }
    recording = -1; captured = inputCount = historyIndex = 0; arenaWrite = 0;
    detectorEnvelope = 0.0; wasAboveThreshold = false;
    lastOnset = sample - std::max(1, retriggerFrames);
    std::fill(history.begin(), history.end(), 0.0f);
    for (auto& instance : stretchers) if (instance) instance->clear();
    if (resamplers) resamplers->clear();
}
void VarispeedEngine::setParameters(Parameters next)
{
    next.rate = std::clamp(next.rate, .25f, 4.0f);
    next.quality = std::clamp(next.quality, 0, 2);
    next.thresholdDb = std::clamp(next.thresholdDb, -96.0f, 0.0f);
    next.retriggerMs = std::clamp(next.retriggerMs, 10.0f, 1000.0f);
    next.crossfadeMs = std::clamp(next.crossfadeMs, 0.0f, 100.0f);
    parameters = next;
    if (resamplers) resamplers->request(next.rate);
    thresholdLevel = std::pow(10.0, next.thresholdDb / 20.0);
    retriggerFrames = std::max(1, rounded(sampleRate * next.retriggerMs / 1000.0));

}
void VarispeedEngine::beginBlock(std::optional<int64_t> transportSample,
                                 std::optional<bool> transportPlaying, bool nonRealtime) noexcept
{
    offlineRendering = nonRealtime;
    if (!stretcher) return;
    // Play/Stop is independent of the optional sample clock. Hosts may omit
    // that clock, but stopping must still discard pending captured audio.
    if (transportPlaying) {
        if (lastTransportPlaying && *lastTransportPlaying != *transportPlaying) {
            restartAt(transportSample.value_or(0));
            usingTransport = false;
        }
        lastTransportPlaying = transportPlaying;
        if (!*transportPlaying) transportSample.reset();
    }
    if (transportSample) {
        if (!usingTransport || position != *transportSample) restartAt(*transportSample);
        usingTransport = true;
    } else if (usingTransport) {
        usingTransport = false; restartAt(0);
    }
}
void VarispeedEngine::startCapture() noexcept
{
    recording = -1;
    for (size_t i = 0; i < voices.size(); ++i)
        if (!voices[i].ready || position >= voices[i].start + voices[i].playbackFrames) {
            recording = int(i); break;
        }
    if (recording < 0) return;
    auto& voice = voices[size_t(recording)];
    voice.dataStart = arenaWrite;
    voice.ready = false; voice.inputFrames = voice.stored = voice.playbackFrames = 0;
    voice.preservePitch = parameters.preservePitch;
    voice.quality = parameters.quality;
    voice.rate = voice.preservePitch ? parameters.rate : resamplers->begin(parameters.rate, offlineRendering);
    voice.start = position - preRollFrames + getLatencySamples();
    voice.fade = rounded(sampleRate * parameters.crossfadeMs / 1000.0);
    captured = inputCount = 0;
    if (voice.preservePitch) {
        stretcher = stretchers[size_t(voice.quality)].get();
        stretcher->clear(); stretcher->setTempo(voice.rate);
    }
}
void VarispeedEngine::drain(int limit) noexcept
{
    auto& voice = voices[size_t(recording)];
    while (stretcher->numSamples() > 0) {
        const auto count = int(stretcher->receiveSamples(output.data(), blockSize));
        const auto keep = std::max(0, std::min(count, limit - voice.stored));
        for (int frame = 0; frame < keep; ++frame)
            append(output[size_t(frame * 2)], output[size_t(frame * 2 + 1)]);
        voice.stored += keep;
    }
}
void VarispeedEngine::feedInput() noexcept
{
    if (inputCount == 0) return;
    if (voices[size_t(recording)].preservePitch) {
        stretcher->putSamples(input.data(), unsigned(inputCount));
        drain(maximumOutputFrames);
    } else feedResampler(inputCount, maximumOutputFrames);
    inputCount = 0;
}
void VarispeedEngine::finishCapture() noexcept
{
    auto& voice = voices[size_t(recording)];
    voice.inputFrames = captured;
    voice.playbackFrames = std::max(1, int(std::ceil(captured / voice.rate)));
    feedInput();
    if (voice.preservePitch) {
        // SoundTouch::flush allocates a temporary buffer. Supply preallocated
        // silence ourselves and trim to this fragment's exact output length.
        const auto paddingLimit = int(std::ceil((stretcher->getSetting(SETTING_INITIAL_LATENCY) + sampleRate * .5) / blockSize)) + 32;
        for (int block = 0; voice.stored < voice.playbackFrames && block < paddingLimit; ++block) {
            stretcher->putSamples(silence.data(), blockSize);
            drain(voice.playbackFrames);
        }
        voice.stored = std::min(voice.stored, voice.playbackFrames);
        voice.playbackFrames = voice.stored;
    } else {
        for (auto& channel : resamplerInput) channel.fill(0.0);
        const auto paddingLimit = int(std::ceil(sampleRate * .5 / blockSize)) + 32;
        for (int block = 0; voice.stored < voice.playbackFrames && block < paddingLimit; ++block)
            feedResampler(blockSize, voice.playbackFrames);
        voice.stored = std::min(voice.stored, voice.playbackFrames);
        voice.playbackFrames = voice.stored;
    }
    voice.fade = std::min(voice.fade, voice.playbackFrames / 2);
    voice.ready = true;
    recording = -1;
}
void VarispeedEngine::feedResampler(int count, int limit) noexcept
{
    std::array<double*, 2> samples {};
    const auto frames = resamplers->process(resamplerInput, count, samples);
    auto& voice = voices[size_t(recording)];
    const auto keep = std::max(0, std::min(frames, limit - voice.stored));
    for (int frame = 0; frame < keep; ++frame) append(float(samples[0][frame]), float(samples[1][frame]));
    voice.stored += keep;
}
void VarispeedEngine::append(float left, float right) noexcept
{
    const auto index = size_t(arenaWrite++ % uint64_t(arenaFrames)) * 2;
    arena[index] = left; arena[index + 1] = right;
}
std::array<double, 2> VarispeedEngine::sampleAt(const Voice& voice, int frame) const noexcept
{
    const auto index = size_t((voice.dataStart + uint64_t(frame)) % uint64_t(arenaFrames)) * 2;
    return {arena[index], arena[index + 1]};
}
std::array<double, 2> VarispeedEngine::process(double left, double right) noexcept
{
    if (!stretcher) return {left, right};
    const auto peak = std::max(std::abs(left), std::abs(right));
    detectorEnvelope = peak >= detectorEnvelope ? peak
        : peak + (detectorEnvelope - peak) * detectorReleaseCoefficient;
    constexpr double hysteresisGain = 0.9885530946569389; // -0.1 dB
    const auto aboveThreshold = detectorEnvelope >= thresholdLevel
        || (wasAboveThreshold && detectorEnvelope >= thresholdLevel * hysteresisGain);
    const auto risingTrigger = aboveThreshold && !wasAboveThreshold;
    wasAboveThreshold = aboveThreshold;
    if (risingTrigger && position - lastOnset >= retriggerFrames) {
        if (recording >= 0) finishCapture();
        startCapture();
        lastOnset = position;
    }
    // Detection looks ahead of capture by 5 ms, preserving the attack and
    // keeping a new attack out of the previous fragment's WSOLA input.
    const auto index = size_t(historyIndex) * 2;
    const auto delayedLeft = history[index], delayedRight = history[index + 1];
    history[index] = float(left); history[index + 1] = float(right);
    historyIndex = (historyIndex + 1) % preRollFrames;
    bool continueCapture = false;
    if (recording >= 0) {
        auto& current = voices[size_t(recording)];
        if (current.preservePitch) {
            input[size_t(inputCount) * 2] = delayedLeft;
            input[size_t(inputCount) * 2 + 1] = delayedRight;
            if (++inputCount == blockSize) feedInput();
        } else {
            resamplerInput[0][size_t(inputCount)] = delayedLeft;
            resamplerInput[1][size_t(inputCount)] = delayedRight;
            if (++inputCount == blockSize) feedInput();
        }
        if (++captured >= maximumInputFrames) {
            finishCapture();
            continueCapture = true;
        }
    }
    std::array<double, 2> result {};
    for (auto& voice : voices) {
        if (!voice.ready) continue;
        const auto age = position - voice.start;
        if (age < 0) continue;
        if (age >= voice.playbackFrames) { voice.ready = false; continue; }
        auto samples = sampleAt(voice, int(age));
        const auto fade = voice.fade > 0 ? std::min({1.0, double(age) / voice.fade,
            double(voice.playbackFrames - 1 - age) / voice.fade}) : 1.0;
        for (size_t channel = 0; channel < 2; ++channel) result[channel] += samples[channel] * fade;
    }
    ++position;
    // A full internal buffer starts a continuation at the next source sample;
    // it must never turn the capture limit into a sustain gate.
    if (continueCapture) startCapture();
    return result;
}
}
