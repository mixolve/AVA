#pragma once
namespace ava::modules::sibil {
struct Parameters {
    bool hidden=true, bypassed=false, soloS=false, soloRest=false;
    float level=-50.f, brightness=50.f, noise=20.f, fade=5.f, edgeDetect=50.f;
    float gainS=0.f, gainRest=0.f;
};

// Both un-soloed branches at unity are an exact pass-through (S + TONAL = input).
// Do not run the look-ahead detector or charge the host its latency when there
// is no audible processing to perform. This is independent of whether the
// module's editor is shown in MODULES and of the BP button state.
constexpr bool requiresProcessing(const Parameters& p) noexcept {
    return !p.hidden && !p.bypassed
        && (p.gainS != 0.f || p.gainRest != 0.f || p.soloS != p.soloRest);
}
}
