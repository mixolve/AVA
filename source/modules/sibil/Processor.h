#pragma once
#include "Parameters.h"
#include <JuceHeader.h>
#include <vector>
#include <array>
#include <memory>
#include <cstdint>
namespace ava::modules::chain::dsp {
// Analysis creates one event mask; the audio itself never passes through FFT.
class SibilProcessor final {
public:
    void prepare(double rate);
    void reset() noexcept;
    void setParameters(const ava::modules::sibil::Parameters&, bool);
    void process(double& left, double& right) noexcept;
    bool isActive() const noexcept { return !parameters.hidden && !parameters.bypassed; }
    int getLatencySamples() const noexcept { return isActive() ? latency : 0; }
    double getSAmount() const noexcept { return lastMask; }
    double getBrightness() const noexcept { return brightness; }
    double getNoisiness() const noexcept { return noisiness; }
private:
    void analyse() noexcept;
    void finishRangeEvent(int64_t lastSample) noexcept;
    size_t at(int64_t sample) const noexcept;
    ava::modules::sibil::Parameters parameters;
    double sampleRate=44100., brightness=0., noisiness=0., levelDb=-120., lastMask=0.;
    double targetS=1., targetRest=1., gainS=1., gainRest=1., gainCoefficient=0.;
    int fftSize=1024, hop=256, analysisDelay=768, windowSamples=441, fadeSamples=221, latency=1319, run=0, rangeSamples=0, eventLength=0, preSamples=0, postSamples=0, expandedCount=0;
    int64_t position=0, eventStart=0;
    bool eventActive=false, eventResolved=false, eventAccepted=false;
    double eventPeak=0., eventGain=1.;
    struct FadeStage {
        std::vector<std::array<double,2>> history;
        std::array<double,2> sum {};
        int length=1, position=0;
        void reset() noexcept;
        std::array<double,2> process(std::array<double,2> input) noexcept;
    };
    std::array<FadeStage,3> fadeStages;
    std::array<std::vector<double>,2> audio;
    std::vector<unsigned char> rawMask, confirmedMask, expandedMask, rangeMask;
    std::vector<double> rangeScale;
    std::vector<float> spectrum, hann, correlation;
    std::unique_ptr<juce::dsp::FFT> fft;
};
}
