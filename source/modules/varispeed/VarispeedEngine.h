#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
namespace soundtouch { class SoundTouch; }
namespace ava::modules::chain::dsp {
// Onset-triggered sample playback. Each captured fragment keeps its own rate,
// preserve-pitch mode and fade until its playback has finished.
class VarispeedEngine final {
public:
    static constexpr double maximumCaptureMs = 500.0;
    struct Parameters {
        float rate = 1.0f, thresholdDb = -48.0f, retriggerMs = 60.0f, crossfadeMs = 2.0f;
        bool preservePitch = false;
        int quality = 0;
    };
    VarispeedEngine();
    ~VarispeedEngine();
    void prepare(double sampleRate);
    void prepare(double sampleRate, Parameters initial);
    void reset() noexcept;
    void setParameters(Parameters next);
    void beginBlock(std::optional<int64_t> transportSample,
                    std::optional<bool> transportPlaying = std::nullopt, bool nonRealtime = false) noexcept;
    std::array<double, 2> process(double left, double right) noexcept;
    int getLatencySamples() const noexcept { return getMaximumLatencySamples(); }
    int getMaximumLatencySamples() const noexcept { return maximumInputFrames + preRollFrames + 1; }
private:
    static constexpr int blockSize = 256, voiceCount = 64;
    struct Voice {
        uint64_t dataStart = 0;
        int64_t start = 0;
        int inputFrames = 0, stored = 0, playbackFrames = 0, fade = 0;
        double rate = 1.0;
        int quality = 0;
        bool preservePitch = false, ready = false;
    };
    std::array<Voice, voiceCount> voices;
    std::array<std::unique_ptr<soundtouch::SoundTouch>, 3> stretchers;
    soundtouch::SoundTouch* stretcher = nullptr;
    class Resamplers;
    std::unique_ptr<Resamplers> resamplers;
    std::array<std::array<double, blockSize>, 2> resamplerInput {};
    std::array<float, blockSize * 2> input {}, output {}, silence {};
    Parameters parameters;
    std::vector<float> arena, history;
    uint64_t arenaWrite = 0;
    int arenaFrames = 0, historyIndex = 0, preRollFrames = 0;
    double detectorEnvelope = 0.0, detectorReleaseCoefficient = 0.0, thresholdLevel = 0.0;
    int retriggerFrames = 0;
    int64_t lastOnset = 0;
    bool wasAboveThreshold = false;
    double sampleRate = 44100.0;
    int maximumInputFrames = 0, maximumOutputFrames = 0;
    int recording = -1, captured = 0, inputCount = 0;
    int64_t position = 0;
    bool usingTransport = false, offlineRendering = false;
    std::optional<bool> lastTransportPlaying;
    void restartAt(int64_t sample) noexcept;
    void startCapture() noexcept;
    void feedInput() noexcept;
    void drain(int limit) noexcept;
    void finishCapture() noexcept;
    void append(float left, float right) noexcept;
    std::array<double, 2> sampleAt(const Voice& voice, int frame) const noexcept;
    void feedResampler(int count, int limit) noexcept;
};
}
