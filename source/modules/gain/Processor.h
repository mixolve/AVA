#pragma once
#include "Parameters.h"
#include "Order.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class GainProcessor final {
public:
void prepare(double rate) { sampleRate=rate; } void reset() noexcept { smoothed=targets; ramp=0; }
void setParameters(const ava::modules::gain::Parameters& p, bool processed);
void process(double& left,double& right) noexcept;
void setOrder(const gain::Order& next) noexcept { order=gain::valid(next)?next:gain::defaultOrder; }
bool isActive() const noexcept { return enabled || ramp > 0; }
int getLatencySamples() const noexcept { return 0; }
private: double sampleRate=44100.; std::array<double,5> targets {1.,1.,1.,1.,1.}, smoothed {1.,1.,1.,1.,1.};
gain::Order order=gain::defaultOrder; int ramp=0; bool enabled=false;
};
}
