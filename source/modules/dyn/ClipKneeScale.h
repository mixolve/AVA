#pragma once

#include <algorithm>

namespace dyn_clip_knee
{
// EQL's peak-reference compensation makes a linear 0..96 dB knee control
// strongly front-loaded. Calibrate its perceptual response at -6 dB relative
// to the clipping threshold, with the EQL reference at the threshold.
//
// At that level, the EQL boost in dB is:
//   knee / 8                         when knee <= 12 dB
//   3 - 18 / knee                    when knee >= 12 dB.
// Invert this function so equal knob travel gives equal dB change. This only
// remaps the existing CLIP-KNEE parameter; it does not change the transfer,
// add gain, or introduce state/latency. 0/96 dB remain exact endpoints.
inline double scaleEqualizedKneeDb(double knobDb) noexcept
{
    constexpr double maxKneeDb = 96.0;
    constexpr double referenceBelowThresholdDb = 6.0;
    constexpr double pivotGainDb = referenceBelowThresholdDb * 0.25;
    constexpr double maxGainDb = referenceBelowThresholdDb * 0.5
        - referenceBelowThresholdDb * referenceBelowThresholdDb / (2.0 * maxKneeDb);

    const auto position = std::clamp(knobDb / maxKneeDb, 0.0, 1.0);
    if (position <= 0.0) return 0.0;
    if (position >= 1.0) return maxKneeDb;

    const auto targetGainDb = position * maxGainDb;
    if (targetGainDb <= pivotGainDb)
        return 8.0 * targetGainDb;
    return referenceBelowThresholdDb * referenceBelowThresholdDb
        / (referenceBelowThresholdDb - 2.0 * targetGainDb);
}
}
