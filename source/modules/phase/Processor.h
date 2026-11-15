#pragma once
#include "Parameters.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class PhaseProcessor final {
public:
void prepare(double); void reset() noexcept; void setParameters(const ava::modules::phase::Parameters& p, bool);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return enabled; }
int getLatencySamples() const noexcept { return enabled ? filterLatency : 0; }
private: static constexpr int filterTaps=129, filterLatency=(filterTaps-1)/2, bufferSize=256;
void initialiseCoefficients(); int position=0; bool enabled=false;
std::array<double,bufferSize> leftBuffer {},rightBuffer {};
std::array<double,filterTaps> coefficients {};
double leftCos=1.,leftSin=0.,rightCos=1.,rightSin=0.;
};
}
