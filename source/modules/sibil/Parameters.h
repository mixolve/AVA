#pragma once
namespace ava::modules::sibil {
inline constexpr float maximumLookaheadMs = 30.0f;
inline constexpr float defaultLookaheadMs = 10.0f;
inline constexpr auto lookaheadId = "sibil.lookahead";
inline constexpr float maximumHoldMs = 100.0f;
inline constexpr float defaultHoldMs = 10.0f;
inline constexpr auto holdId = "sibil.hold";
inline constexpr float defaultFadeMs = 0.0f;

struct Parameters {
    bool hidden=true, bypassed=false, soloS=false, soloRest=false;
    float level=-50.f, sensitivity=50.f, noise=20.f, fade=defaultFadeMs;
    float gainS=0.f, gainRest=0.f;
    float lookaheadMs=defaultLookaheadMs;
    float holdMs=defaultHoldMs;
};

// Both un-soloed branches at unity are an exact pass-through (S + TONAL = input).
// Skip the detector when both branches are at unity.
constexpr bool requiresProcessing(const Parameters& p) noexcept {
    return !p.hidden && !p.bypassed
        && (p.gainS != 0.f || p.gainRest != 0.f || p.soloS != p.soloRest);
}
}
