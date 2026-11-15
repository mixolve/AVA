#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dyn::dsp
{
// Exactly the user-requested HOLD interval: maximum of the actual detector
// samples inside the trailing HOLD window (with no waveform reconstruction).
// A monotone deque maintains candidates in amortized O(1) per sample.  The
// deque retains the maximum supported HOLD history so a change to HOLD takes
// effect immediately, without reallocations, rebuilding, or a frozen timer.
class HoldWindow
{
public:
    void prepare(const double sampleRate, const double maximumSeconds = 1.0)
    {
        const auto history = static_cast<size_t>(std::ceil(std::max(1.0, sampleRate)
            * std::max(0.0, maximumSeconds))) + 2;
        size_t capacity = 1;
        while (capacity < history) capacity *= 2;
        levels.assign(capacity, 0.0f);
        timestamps.assign(capacity, uint64_t { 0 });
        reset();
    }

    void reset() noexcept
    {
        front = 0;
        count = 0;
        sampleIndex = 0;
    }

    double process(const double sample, const int holdSamples) noexcept
    {
        const auto capacity = levels.size();
        if (capacity == 0) return sample;
        const auto mask = capacity - 1;

        const auto level = static_cast<float>(std::max(0.0, sample));
        // Discard peaks superseded by this sample. Ring buffers are allocated
        // only in prepare(); there is no heap activity on the audio thread.
        while (count > 0 && levels[(front + count - 1) & mask] <= level)
            --count;
        const auto position = (front + count) & mask;
        levels[position] = level;
        timestamps[position] = sampleIndex;
        ++count;

        // Expire only samples older than the *maximum* possible HOLD. A query
        // for a smaller HOLD ignores earlier candidates without deleting them.
        const auto oldest = sampleIndex >= capacity - 1 ? sampleIndex - (capacity - 1) : uint64_t { 0 };
        while (count > 0 && timestamps[front] < oldest)
        {
            front = (front + 1) & mask;
            --count;
        }
        ++sampleIndex;
        if (holdSamples <= 0) return sample;

        const auto requested = std::min(static_cast<uint64_t>(std::max(0, holdSamples)) + 1,
            std::min(sampleIndex, static_cast<uint64_t>(capacity)));
        const auto from = sampleIndex - requested;
        // The deque is ordered in time (older -> newer), with decreasing
        // amplitudes. First candidate inside the desired window is the max.
        size_t low = 0, high = count;
        while (low < high)
        {
            const auto mid = low + (high - low) / 2;
            if (timestamps[(front + mid) & mask] < from)
                low = mid + 1;
            else
                high = mid;
        }
        return low < count ? levels[(front + low) & mask] : sample;
    }

private:
    std::vector<float> levels;
    std::vector<uint64_t> timestamps;
    size_t front = 0, count = 0;
    uint64_t sampleIndex = 0;
};
}
