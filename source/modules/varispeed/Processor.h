#pragma once
#include "Parameters.h"
#include "VarispeedEngine.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class VarispeedProcessor final {
public:
void prepare(double rate) { engine.prepare(rate, initial); } void reset() noexcept { engine.reset(); }
void setParameters(const ava::modules::varispeed::Parameters& p, bool);
void process(double& left,double& right) noexcept;
void beginBlock(std::optional<int64_t> position,std::optional<bool> playing,bool nonRealtime) noexcept {
if (enabled) engine.beginBlock(position,playing,nonRealtime); }
bool isActive() const noexcept { return enabled; }
int getLatencySamples() const noexcept { return enabled ? engine.getLatencySamples() : 0; }
int getMaximumLatencySamples() const noexcept { return engine.getMaximumLatencySamples(); }
private: VarispeedEngine engine; VarispeedEngine::Parameters initial; bool enabled=false;
};
}
