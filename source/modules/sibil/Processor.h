#pragma once

#include "Parameters.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace ava::modules::chain::dsp
{
class SibilProcessor final
{
public:
    void prepare(double rate);
    void reset() noexcept;
    void beginBlock(std::optional<int64_t> timelineSample,
                    std::optional<bool> playing, int blockSamples) noexcept;
    void setParameters(const ava::modules::sibil::Parameters&, bool processed);
    void process(double& left, double& right) noexcept;
    bool isActive() const noexcept { return ava::modules::sibil::requiresProcessing(parameters); }
    int getLatencySamples() const noexcept { return isActive() ? lookaheadSamples : 0; }

private:
    using Coefficients = std::array<double, 6>;

    struct Filter
    {
        double z1 = 0.0, z2 = 0.0;
        double process(double sample, const Coefficients&) noexcept;
    };
    struct Detector
    {
        struct Prediction
        {
            double power = 0.0, referencePower = 0.0, crossPower = 0.0;
        };
        static constexpr unsigned historySize = 256;
        Filter presence, high;
        std::array<double, historySize> history {};
        unsigned position = 0;
        double levelPower = 0.0, presencePower = 0.0, highPower = 0.0;
        std::array<Prediction, 2> predictions {};
        double backgroundPower = 0.0;
    };

    void updateDetector(Detector&, double sample) noexcept;
    bool detectsSibilance(Detector&) const noexcept;

    ava::modules::sibil::Parameters parameters;
    std::array<Detector, 2> detectors;
    Coefficients presenceCoefficients {}, highCoefficients {};
    double sampleRate = 44100.0;
    std::array<unsigned, 2> predictionLags {2, 20};
    int decisionInterval = 1, samplesToDecision = 1;
    double envelopeCoefficient = 1.0, fadeStep = 1.0, gainCoefficient = 1.0;
    double levelThreshold = 0.0;
    double brightnessThreshold = 0.37, noiseThreshold = 0.22;
    double backgroundRise = 1.0, backgroundFall = 1.0;
    double minimumContrast = 2.0;
    bool detectorOpen = false;
    int holdSamples = 0, holdRemaining = 0;
    double fadePosition = 0.0;
    double targetS = 1.0, targetRest = 1.0, gainS = 1.0, gainRest = 1.0;
    std::vector<std::array<double, 2>> audioDelay;
    size_t delayWrite = 0;
    int lookaheadSamples = 0;
    std::optional<int64_t> nextTimelineSample;
    std::optional<bool> lastTransportPlaying;
};
}
