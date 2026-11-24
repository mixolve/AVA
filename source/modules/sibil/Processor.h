#pragma once
#include "Parameters.h"
#include <JuceHeader.h>
#include <vector>
#include <array>
#include <memory>
#include <cstdint>
#include <optional>
namespace ava::modules::chain::dsp {
// Analysis creates one event mask; the audio itself never passes through FFT.
class SibilProcessor final {
public:
    void prepare(double rate);
    void reset() noexcept;
    // Called at the start of each host audio block. Repetition of the same
    // timeline range must use the same detector state and FFT grid.
    void beginBlock(std::optional<int64_t> timelineSample,
                    std::optional<bool> playing, int blockSamples) noexcept;
    void setParameters(const ava::modules::sibil::Parameters&, bool);
    void process(double& left, double& right) noexcept;
    bool isActive() const noexcept { return ava::modules::sibil::requiresProcessing(parameters); }
    int getLatencySamples() const noexcept { return isActive() ? latency : 0; }
    double getSAmount() const noexcept { return lastMask; }
    uint64_t getDetectedEventCount() const noexcept { return detectedEventCount; }
    double getBrightness() const noexcept { return brightness; }
    double getNoisiness() const noexcept { return noisiness; }
private:
    void analyse() noexcept;
    void analyseZtw() noexcept;
    size_t at(int64_t sample) const noexcept;
    ava::modules::sibil::Parameters parameters;
    double sampleRate=44100., brightness=0., noisiness=0., levelDb=-120., lastMask=0.;
    double targetS=1., targetRest=1., gainS=1., gainRest=1., gainCoefficient=0.;
    int fftSize=1024, hop=256, analysisDelay=768, fadeSamples=221, latency=1319, edgeLookbackSamples=0;
    int ztwHop=88, ztwLength=441, ztwGridOffset=0;
    // Recognition and event identity are independent of both edge controls.
    bool eventActive=false;
    uint64_t detectedEventCount=0;
    int eventTailGap=0;
    int64_t eventCoreStart=0, eventCoreEnd=0, eventPeakPosition=0, eventMaskStart=0;
    // ZTW/HNGD tracks the dominant resonance of the fricative, not its HF amplitude.
    double previousZtwScore=0., ztwSmoothedScore=0., eventZtwPeak=0., eventZtwBackground=0.;
    // The ZTW contour alone is not an amplitude gate: on a voiced harmonic
    // it may remain high indefinitely. Keep an independent HF energy contour.
    double previousHfDb=-120., eventHfPeakDb=-120., eventHfBackgroundDb=-120.;
    double previousAperiodic=0., eventAperiodicPeak=0., eventAperiodicBackground=0.;
    bool havePreviousHf=false, havePreviousAperiodic=false;
    bool havePreviousZtw=false;
    int64_t position=0;
    // Analyse on a fixed grid in the host timeline rather than on the
    // arbitrary sample count since the plug-in was instantiated.
    int fftGridOffset=0;
    std::optional<int64_t> nextTimelineSample;
    std::optional<bool> lastTransportPlaying;
    struct FadeStage {
        std::vector<std::array<double,2>> history;
        std::array<double,2> sum {};
        int length=1, position=0;
        void reset() noexcept;
        std::array<double,2> process(std::array<double,2> input) noexcept;
    };
    std::array<FadeStage,3> fadeStages;
    std::array<std::vector<double>,2> audio;
    std::vector<unsigned char> rawMask, confirmedMask;
    std::vector<float> edgeZtwScore, edgeHfDb, edgeAperiodic;
    std::vector<float> spectrum, hann, correlation, ztwWindow;
    using Complex = juce::dsp::Complex<float>;
    std::vector<Complex> ztwTime, ztwTimeWeighted, ztwSpectrum, ztwSpectrumWeighted, ztwAnalyticInput, ztwAnalyticOutput;
    std::vector<float> ztwHngd;
    std::unique_ptr<juce::dsp::FFT> fft;
};
}
