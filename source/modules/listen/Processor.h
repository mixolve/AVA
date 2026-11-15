#pragma once
#include "Parameters.h"
#include <array>
#include <vector>
#include <optional>

namespace ava::modules::chain::dsp {
class ListenProcessor final {
public:
enum class Mode { leftCenter,rightCenter,midCenter,sideCenter,leftLeft,rightRight,neutral,sideStereo };
void prepare(double) {} void reset() noexcept {} void setParameters(const ava::modules::listen::Parameters& p, bool);
void process(double& left,double& right) noexcept;
bool isActive() const noexcept { return mode != Mode::neutral; }
int getLatencySamples() const noexcept { return 0; }
private: Mode mode=Mode::neutral; double msGain=1.0;
};
}
