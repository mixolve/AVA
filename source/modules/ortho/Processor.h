#pragma once
#include "Parameters.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class OrthoProcessor final {
public:
void prepare(double) {} void reset() noexcept {} void setParameters(const ava::modules::ortho::Parameters& p, bool);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return enabled; }
int getLatencySamples() const noexcept { return 0; }
private: double m11=1.,m12=0.,m21=0.,m22=1.; bool enabled=false;
};
}
