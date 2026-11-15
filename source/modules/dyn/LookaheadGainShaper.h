#pragma once

#include "HoldWindow.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace dyn::dsp
{
// First find the strongest reduction in the lookahead window, then apply
// a finite curve-weighted transition. Shaping individual carrier samples
// instead modulates steady-tone gain and introduces harmonic distortion.
// Each peak stays in the window long enough for the transition to reach it
// by its carrier deadline. Recovery follows the window without an extra tail.
class LookaheadGainShaper
{
public:
    void prepare(const double sampleRate)
    {
        history.assign(static_cast<size_t>(std::ceil(std::max(1.0, sampleRate) * 0.030)) + 2, 0.0);
        futureReduction.prepare(std::max(1.0, sampleRate), 0.030);
        reset();
    }

    void reset() noexcept
    {
        std::fill(history.begin(), history.end(), 0.0);
        futureReduction.reset();
        writeIndex = 0;
        window = 0;
        curve = 0.0;
        weightedSum = 0.0;
        sinceRebuild = 0;
    }

    double process(const double currentGain, const int lookaheadSamples,
                   const double lookCurve) noexcept
    {
        const auto instant = std::clamp(currentGain, 0.0, 1.0);
        if (history.empty()) return instant;
        const auto length = std::clamp(lookaheadSamples, 0, static_cast<int>(history.size()) - 1);
        const auto amount = std::clamp(lookCurve, -1.0, 1.0);
        const auto strongest = futureReduction.process(1.0 - instant, length);
        if (length > 0)
        {
            if (length != window || std::abs(amount - curve) > 1.0e-12)
            {
                window = length;
                curve = amount;
                // Cumulative exponential weights reproduce the existing
                // LOOK-CURVE early / linear / late transition shapes.
                constexpr double maximumExponent = 4.605170185988091;
                const auto exponent = 4.0 * std::atanh(std::abs(amount)
                    * std::tanh(maximumExponent * 0.25));
                weightRatio = std::exp((amount > 0.0 ? -exponent : exponent) / window);
                oldestWeight = std::pow(weightRatio, window);
                normalisation = 0.0;
                auto weight = 1.0;
                for (int age = 0; age < window; ++age)
                {
                    normalisation += weight;
                    weight *= weightRatio;
                }
                sinceRebuild = window;
            }

            // Periodic exact recomputation bounds numerical drift even for
            // late curves (ratio > 1), with amortised constant work/sample.
            if (++sinceRebuild >= window)
            {
                weightedSum = strongest;
                auto weight = weightRatio;
                for (int age = 1; age < window; ++age)
                {
                    weightedSum += weight * readAge(age);
                    weight *= weightRatio;
                }
                sinceRebuild = 0;
            }
            else
                weightedSum = weightRatio * weightedSum + strongest - oldestWeight * readAge(window);
        }
        else
            window = 0;

        history[writeIndex] = strongest;
        writeIndex = (writeIndex + 1) % history.size();
        if (length == 0) return instant;

        // The finite transition anticipates increases in reduction. Once the
        // window recovers, do not extend the compressor's programmed RELEASE.
        return 1.0 - std::min(strongest, std::clamp(weightedSum / normalisation, 0.0, 1.0));
    }

private:
    double readAge(const int age) const noexcept
    {
        return history[(writeIndex + history.size() - static_cast<size_t>(age)) % history.size()];
    }

    HoldWindow futureReduction;
    std::vector<double> history;
    size_t writeIndex = 0;
    int window = 0;
    int sinceRebuild = 0;
    double curve = 0.0;
    double weightRatio = 1.0;
    double oldestWeight = 1.0;
    double normalisation = 1.0;
    double weightedSum = 0.0;
};
}
