#pragma once
#include "Parameters.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class RectProcessor final {
public:
void prepare(double) {} void reset() noexcept {} void setParameters(const ava::modules::rect::Parameters& p, bool);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return mode != 0; }
int getLatencySamples() const noexcept { return 0; }
private: int mode=0;
};
}
