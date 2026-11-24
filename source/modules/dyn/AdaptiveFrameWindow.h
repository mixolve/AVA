#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

// Sliding arithmetic mean of frame energies. Correlation numerator and
// denominator use exactly the same window, but are returned as sums so that
// their ratio remains correctly weighted by the detector energy.
class DynAdaptiveFrameWindow
{
public:
    struct Frame
    {
        double power = 0.0;
        double correlationCross = 0.0;
        double correlationWeight = 0.0;
    };

    void prepare(const std::size_t maximumFrames)
    {
        frames.assign(std::max<std::size_t>(1, maximumFrames), {});
        reset();
    }

    void reset() noexcept
    {
        // After a reset, old ring entries cannot be read before they are
        // overwritten. No need to clear a 10-second buffer on the audio thread.
        writePosition = 0;
        storedFrames = 0;
        cumulative = {};
    }

    std::size_t capacity() const noexcept { return frames.size(); }

    Frame push(const Frame& frame, const std::size_t requestedWindowFrames) noexcept
    {
        if (frames.empty())
            return frame;

        // Prefix energies allow a variable-length sliding window in O(1).
        // Correlation numerator and denominator use the exact same window.
        const auto window = std::clamp(requestedWindowFrames, std::size_t(1), frames.size());
        const auto oldestPrefixIndex = (writePosition + frames.size() - window) % frames.size();
        const auto previous = storedFrames >= window ? frames[oldestPrefixIndex] : Frame{};

        cumulative.power += frame.power;
        cumulative.correlationCross += frame.correlationCross;
        cumulative.correlationWeight += frame.correlationWeight;
        frames[writePosition] = cumulative;
        writePosition = (writePosition + 1) % frames.size();
        storedFrames = std::min(storedFrames + 1, frames.size());

        const auto windowCount = static_cast<double>(std::min(storedFrames, window));
        return {
            std::max(0.0, cumulative.power - previous.power) / windowCount,
            cumulative.correlationCross - previous.correlationCross,
            cumulative.correlationWeight - previous.correlationWeight
        };
    }

    std::vector<Frame> frames;
    std::size_t writePosition = 0;
    std::size_t storedFrames = 0;
    Frame cumulative{};
};
