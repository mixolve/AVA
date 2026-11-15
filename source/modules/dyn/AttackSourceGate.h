#pragma once

#include <algorithm>
#include <cmath>

namespace dyn::ballistics
{
// Source activity for compressor Attack interruption. Full half-wave peaks
// prevent ordinary zero crossings from ending an unfinished Attack.
class AttackSourceGate
{
public:
    void reset() noexcept
    {
        polarity = 0;
        halfWavePeak = 0.0;
        active = false;
        consecutiveZeros = 0;
    }

    bool isActive() const noexcept { return active; }

    bool advance(const double signedSample, const double threshold) noexcept
    {
        const auto magnitude = std::abs(signedSample);
        // Exact digital silence needs no peak-cycle estimate.  Two successive
        // zero samples distinguish it from an ordinary sine zero crossing.
        if (magnitude == 0.0)
        {
            if (++consecutiveZeros >= 2)
            {
                reset();
                return false;
            }
            return active;
        }
        consecutiveZeros = 0;
        const auto nextPolarity = signedSample > 0.0 ? 1 : -1;
        const auto gateThreshold = std::max(0.0, threshold);

        if (nextPolarity != polarity && polarity != 0)
        {
            // A complete below-threshold half-wave can interrupt Attack.
            if (active && halfWavePeak < gateThreshold) active = false;

            halfWavePeak = magnitude;
        }
        polarity = nextPolarity;
        halfWavePeak = std::max(halfWavePeak, magnitude);

        if (!active && magnitude >= gateThreshold)
        {
            active = true;
        }
        return active;
    }

private:
    int polarity = 0;
    double halfWavePeak = 0.0;
    bool active = false;
    int consecutiveZeros = 0;
};
}
