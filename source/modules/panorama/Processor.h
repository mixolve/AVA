#pragma once
#include "Parameters.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class PanoramaProcessor final {
public:
void prepare(double) {} void reset() noexcept {} void setParameters(const ava::modules::panorama::Parameters& p, bool);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return enabled; }
int getLatencySamples() const noexcept { return 0; }
private: double gLL=1.,gLR=0.,gRL=0.,gRR=1.; bool enabled=false;
};
}
