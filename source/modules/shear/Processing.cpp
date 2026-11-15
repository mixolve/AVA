#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void ShearProcessor::setParameters(const ava::modules::shear::Parameters& p, bool) {
    amount=ava::modules::dsp::roundToParameterStep(p.impact)*.01;
    toRight=p.impactToRight;
    enabled=p.available() && std::abs(amount)>1.e-9;
}
void ShearProcessor::process(double& left,double& right) noexcept {
    if (!enabled) return;
    if (toRight) right+=amount*left;
    else left+=amount*right;
}
}
