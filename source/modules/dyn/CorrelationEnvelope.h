#pragma once

#include "AutoRelease.h"

#include <algorithm>
#include <cmath>

namespace dyn_spectral_envelope
{
// Envelopes the detector's correction amount, independently of the audio frame.
// This lets an external key or lookahead start the attack before correction is needed.
struct Envelope
{
    dyn::detector::ReleaseMemory releaseMemory;
    float releaseDurationMs = 0.0f;
    float value = 0.0f, start = 0.0f, elapsedMs = 0.0f;
    float delayRemainingMs = 0.0f, holdRemainingMs = 0.0f;
    enum class Stage
    {
        idle,
        attack,
        release
    } stage = Stage::idle;

    float advance(float desired, float frameMs, float delayMs, float attackMs, float holdMs, float releaseMs,
                  float attackCurve, float releaseCurve, float releaseType = 1.0f, float autoRelease = 0.0f, double memoryCoefficient = 0.0, float releaseDepth = 0.0f, float depthStrength = 1.0f) noexcept
    {
        desired = std::clamp(desired, 0.0f, 1.0f);
        releaseMemory.advance(desired > 1.0e-5f, memoryCoefficient, autoRelease, releaseDepth, depthStrength);
        auto elapsed = frameMs;
        if (desired > value + 1.0e-5f)
        {
            holdRemainingMs = holdMs;
            if (stage != Stage::attack)
            {
                start = value;
                elapsedMs = 0.0f;
                delayRemainingMs = delayMs;
            }
            stage = Stage::attack;
            const auto waiting = std::min(elapsed, delayRemainingMs);
            delayRemainingMs -= waiting;
            elapsed -= waiting;
            if (delayRemainingMs > 0.0f)
                return value;
            elapsedMs += elapsed;
            const auto position = attackMs <= 0.0f ? 1.0f : std::clamp(elapsedMs / attackMs, 0.0f, 1.0f);
            value = start + (desired - start) * std::pow(position, std::exp2(-attackCurve * 0.03f));
        }
        else if (desired < value - 1.0e-5f)
        {
            delayRemainingMs = 0.0f;
            if (stage != Stage::release)
            {
                releaseDurationMs = static_cast<float>(releaseMs * releaseMemory.multiplier(autoRelease));
                start = value;
                elapsedMs = 0.0f;
            }
            stage = Stage::release;
            const auto waiting = std::min(elapsed, holdRemainingMs);
            holdRemainingMs -= waiting;
            elapsed -= waiting;
            if (holdRemainingMs > 0.0f)
                return value;
            elapsedMs += elapsed;
            const auto duration = autoRelease > 0.0f ? releaseDurationMs : releaseMs;
            const auto position = duration <= 0.0f ? 1.0f : std::clamp(elapsedMs / duration, 0.0f, 1.0f);
            const auto destination = desired * std::clamp(releaseType, 0.0f, 1.0f);
            value =
                std::max(desired, start + (destination - start) * std::pow(position, std::exp2(-releaseCurve * 0.03f)));
        }
        else
        {
            value = desired;
            stage = Stage::idle;
            delayRemainingMs = 0.0f;
            holdRemainingMs = holdMs;
        }
        return value;
    }
};
}
