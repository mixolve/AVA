#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace dyn::dsp
{
class TruePeakLimiter
{
public:
private:
    static constexpr int radius = 48;
    static constexpr int maxFactor = 16;
    static constexpr double ceiling = 1.0; // 0 dBTP
    static constexpr std::size_t tapCount = 2 * radius + 1;
    using PhaseTable = std::array<std::array<double, tapCount>, maxFactor>;

public:
    static constexpr int delaySamples = 2 * radius;
    using Stereo = std::array<double, 2>;

    void reset() noexcept
    {
        history = {};
        peaks = {};
        position = 0;
    }

    Stereo process(Stereo audio, int factor) noexcept
    {
        history[position] = history[position + tapCount] = audio;
        const auto first = (position + 1) % tapCount;
        const auto& aligned = history[(position + tapCount - radius) % tapCount];
        const auto& previous = history[(position + tapCount - radius - 1) % tapCount];
        auto peak = std::max({std::abs(aligned[0]), std::abs(aligned[1]),
                              std::abs(previous[0]), std::abs(previous[1])});
        const auto phaseStep = maxFactor / factor;
        for (int phase = phaseStep; phase < maxFactor; phase += phaseStep)
        {
            Stereo interpolated {};
            const auto& coefficients = phases[std::size_t(phase)];
            for (std::size_t tap = 0; tap < tapCount; ++tap)
            {
                const auto& sample = history[first + tap];
                interpolated[0] += sample[0] * coefficients[tap];
                interpolated[1] += sample[1] * coefficients[tap];
            }
            peak = std::max({peak, std::abs(interpolated[0]), std::abs(interpolated[1])});
        }

        // Use a common gain for both channels. Keep every interval whose FIR
        // footprint overlaps the sample being emitted in the peak search.
        peaks[position] = peak;
        const auto gain = ceiling / std::max(ceiling, *std::max_element(peaks.begin(), peaks.end()));
        const auto& delayed = history[first];
        position = first;
        return {delayed[0] * gain, delayed[1] * gain};
    }

private:
    static PhaseTable makePhases()
    {
        const auto sinc = [](double x)
        {
            if (std::abs(x) < 1.0e-12) return 1.0;
            const auto angle = std::numbers::pi * x;
            return std::sin(angle) / angle;
        };
        PhaseTable table {};
        for (int phase = 1; phase < maxFactor; ++phase)
        {
            const auto point = double(phase) / maxFactor - 1.0;
            auto& coefficients = table[std::size_t(phase)];
            double sum = 0.0;
            for (std::size_t i = 0; i < tapCount; ++i)
            {
                const auto distance = point - (double(i) - radius);
                coefficients[i] = std::abs(distance) < radius
                    ? sinc(distance) * sinc(distance / radius) : 0.0;
                sum += coefficients[i];
            }
            for (auto& value : coefficients) value /= sum;
        }
        return table;
    }

    inline static const PhaseTable phases = makePhases();
    std::array<Stereo, 2 * tapCount> history {};
    std::array<double, tapCount> peaks {};
    std::size_t position = 0;
};
}
