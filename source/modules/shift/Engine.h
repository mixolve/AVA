#pragma once
#include <array>
#include <memory>
#include <vector>
namespace RubberBand { class RubberBandStretcher; }
namespace ava::modules::chain::dsp {
// Complementary EQL cuts around a stereo Rubber Band pitch-shifted duplicate.
class ShiftEngine final {
public:
    struct Options {
        int window = 1, pitchMode = 2, detector = 0, transients = 0;
        int order = 1;
        float q = 0.0f, origGainDb = 0.0f, pitchedGainDb = 0.0f;
        bool independentPhase = false;
        bool soloOrig = false, soloPitched = false;
        bool origGainBypassed = false, pitchedGainBypassed = false;
        bool operator==(const Options&) const = default;
    };
    ShiftEngine();
    ~ShiftEngine();
    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(float semitones, float frequency, Options options);
    std::array<double, 2> process(double left, double right) noexcept;
    int getLatencySamples() const noexcept { return latency; }
private:
    // Window size is construction-only. All three engines are prepared outside
    // the audio callback; only the selected window processes audio.
    std::array<std::unique_ptr<RubberBand::RubberBandStretcher>, 3> shifters;
    static constexpr int blockSize = 512;
    std::array<std::array<float, blockSize>, 2> input {}, output {};
    std::array<std::vector<double>, 2> dryHistory;
    std::array<std::vector<float>, 2> wetQueue;
    struct Section {
        std::array<double, 3> b {1.0, 0.0, 0.0};
        std::array<double, 2> a {};
    };
    using FilterCoefficients = std::array<std::array<Section, 4>, 2>;
    struct FilterBank {
        FilterCoefficients coefficients;
        // branch, channel, section, transposed direct-form II state
        std::array<std::array<std::array<std::array<double, 2>, 4>, 2>, 2> state {};
        int stages = 2;
        double highPolarity = 1.0;
        std::array<double, 2> process(double dry, double wet, size_t channel) noexcept;
    };
    FilterBank filters, previousFilters;
    FilterCoefficients targetCoefficients;
    static FilterBank designFilters(int order, double q, double frequency, double sampleRate) noexcept;
    Options options;
    std::array<double, 2> gains {1.0, 1.0}, targetGains {1.0, 1.0};
    int gainRamp = 0;
    double rate = 44100.0, pitch = 0.0, currentPitch = 0.0, cutoff = -1.0, wetMix = 0.0;
    int inputCount = 0, latency = 0, coefficientRamp = 0, warmup = 0, filterFade = 0, filterFadeLength = 1;
    size_t dryPosition = 0, wetRead = 0, wetWrite = 0, wetCount = 0, discard = 0;
    void restartWet() noexcept;
    void applyOptions() noexcept;
    void drainOutput() noexcept;
};
}
