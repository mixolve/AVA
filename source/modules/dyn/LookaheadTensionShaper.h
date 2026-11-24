#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace dyn::dsp
{
// Curve-weighted finite transition for TENSION's forecast gain in log space.
// The caller supplies a peak-window detector envelope (already previewing the
// delayed carrier), so this smoother must NOT delay its output again.
// Both upward expansion and downward attenuation are preserved.
// Lookahead=0 is an exact gain bypass.
// Processing is allocation-free and amortized O(1) per sample.
class LookaheadTensionShaper
{
public:
    void prepare(const double sampleRate)
    {
        history.assign(static_cast<size_t>(std::ceil(std::max(1.0, sampleRate) * 0.030)) + 2, 0.0);
        reset();
    }

    void reset() noexcept
    {
        std::fill(history.begin(), history.end(), 0.0);
        writeIndex = 0;
        window = 0;
        curve = 0.0;
        weightedSum = 0.0;
        sinceRebuild = 0;
    }

    double process(const double targetGain, const int lookaheadSamples,
                   const double lookCurve) noexcept
    {
        if (history.empty()) return targetGain;

        const auto lookahead = std::clamp(lookaheadSamples, 0, static_cast<int>(history.size()) - 2);
        const auto amount = std::clamp(lookCurve, -1.0, 1.0);
        // TENSION includes both downward attenuation and upward expansion.
        // Clamping to [0,1] (as the compressor lookahead does) loses boosts.
        const auto logGain = std::log(std::clamp(targetGain, 1.0e-12, 1.0e12));
        const auto length = lookahead + 1;

        if (lookahead > 0)
        {
            if (length != window || std::abs(amount - curve) > 1.0e-12)
            {
                window = length;
                curve = amount;
                constexpr double maximumExponent = 4.605170185988091;
                const auto exponent = 4.0 * std::atanh(std::abs(amount)
                    * std::tanh(maximumExponent * 0.25));
                weightRatio = std::exp((amount > 0.0 ? -exponent : exponent) / (window - 1));
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

            if (++sinceRebuild >= window)
            {
                weightedSum = logGain;
                auto weight = weightRatio;
                for (int age = 1; age < window; ++age)
                {
                    weightedSum += weight * readAge(age);
                    weight *= weightRatio;
                }
                sinceRebuild = 0;
            }
            else
                weightedSum = weightRatio * weightedSum + logGain - oldestWeight * readAge(window);
        }
        else
            window = 0;

        history[writeIndex] = logGain;
        writeIndex = (writeIndex + 1) % history.size();
        return lookahead == 0 ? targetGain : std::exp(weightedSum / normalisation);
    }

private:
    double readAge(const int age) const noexcept
    {
        return history[(writeIndex + history.size() - static_cast<size_t>(age)) % history.size()];
    }

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
