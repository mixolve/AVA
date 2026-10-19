#pragma once

#include <array>

namespace crossover_ui
{
inline constexpr int dynControlGroupGap = 4;
inline constexpr int dynControlGroupOutset = dynControlGroupGap + 1;

struct DynControlGroupSpec
{
    const char* key;
    const char* firstLabel;
    const char* lastLabel;
    const char* definingLabel;
    const char* secondDefiningLabel = "";
    const char* optionalLastLabel = "";
};

inline constexpr auto dynControlGroups = std::to_array<DynControlGroupSpec>({
    { "exclusive-comp", "MORPH", "CLIP-KNEE", "MORPH" },
    { "exclusive-trans", "TRANS-GAIN", "ONE-SHOT", "TRANS-GAIN", "SUST-GAIN" },
    { "exclusive-spectral", "WIN-SIZE", "SLOPE", "WIN-SIZE", "", "IMPACT" },
    { "ratio", "RATIO", "COMP-KNEE", "RATIO" },
    { "tension", "TENSION", "TENSION-HYST", "TENSION" },
    { "detector", "PEAK/RMS", "RMS-TIME", "PEAK/RMS" },
    { "threshold", "THRESHOLD", "ADAP-RELEASE", "THRESHOLD" },
    { "attack", "LOOKAHEAD", "ATTACK-CURVE", "ATTACK" },
    { "release", "HOLD", "REL-DEPTH", "RELEASE-PEAK" },
    { "output", "MAKE-UP", "OUTPUT", "OUTPUT" },
});
}
