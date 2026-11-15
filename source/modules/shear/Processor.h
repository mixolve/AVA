#pragma once
#include "Parameters.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class ShearProcessor final {
public:
void prepare(double) {} void reset() noexcept {} void setParameters(const ava::modules::shear::Parameters& p, bool);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return enabled; }
int getLatencySamples() const noexcept { return 0; }
private: double amount=0.; bool toRight=false,enabled=false;
};
}
