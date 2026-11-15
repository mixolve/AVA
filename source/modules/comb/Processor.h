#pragma once
#include "Parameters.h"
#include "StereoEngine.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class CombProcessor final {
public:
void prepare(double rate); void reset(); void setParameters(const ava::modules::comb::Parameters& p, bool processed);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return enabled || ramp > 0; }
int getLatencySamples() const noexcept { return enabled ? engine.getLatencySamples() : 0; }
private: void resetComb() noexcept;
comb::StereoEngine engine; std::array<std::vector<double>,2> dryBuffer;
size_t position=0; double sampleRate=44100.,targetMix=1.,smoothedMix=1.; int ramp=0; bool enabled=false;
};
}
