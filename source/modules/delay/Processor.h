#pragma once
#include "Parameters.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class DelayProcessor final {
public:
void prepare(double rate); void reset() noexcept; void setParameters(const ava::modules::delay::Parameters& p, bool processed);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return enabled || ramp > 0; }
int getLatencySamples() const noexcept { return enabled ? lookahead : 0; }
private: double sampleRate=44100.; std::array<double,2> target {}, smoothed {};
std::vector<double> leftBuffer, rightBuffer; int bufferSize=1, writePosition=0, lookahead=0, ramp=0; bool enabled=false;
};
}
