#pragma once
#include "Parameters.h"
#include "ShiftEngine.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class ShiftProcessor final {
public:
void prepare(double rate) { engine.prepare(rate); } void reset() noexcept { engine.reset(); }
void setParameters(const ava::modules::shift::Parameters& p, bool);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return enabled; }
int getLatencySamples() const noexcept { return enabled ? engine.getLatencySamples() : 0; }
int getShiftLatencySamples() const noexcept { return engine.getLatencySamples(); }
private: ShiftEngine engine; bool enabled=false;
};
}
