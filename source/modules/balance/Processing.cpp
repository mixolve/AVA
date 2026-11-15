#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void BalanceProcessor::setParameters(const ava::modules::balance::Parameters& p, bool) {
    midAmount=ava::modules::dsp::roundToParameterStep(p.mid)*.01;
    sideAmount=ava::modules::dsp::roundToParameterStep(p.side)*.01;
    enabled=p.available() && (std::abs(midAmount)>1.e-9 || std::abs(sideAmount)>1.e-9);
}
void BalanceProcessor::process(double& left,double& right) noexcept {
    if (!enabled) return;
    const auto mid=(left+right)*.7071067811865476;
    const auto side=(left-right)*.7071067811865476;
    const auto ml=mid*(midAmount>0. ? 1.-midAmount : 1.);
    const auto mr=mid*(midAmount<0. ? 1.+midAmount : 1.);
    const auto sl=side*(sideAmount>0. ? 1.-sideAmount : 1.);
    const auto sr=side*(sideAmount<0. ? 1.+sideAmount : 1.);
    left=(ml+sl)*.7071067811865476;
    right=(mr-sr)*.7071067811865476;
}
}
