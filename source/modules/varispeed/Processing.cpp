#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void VarispeedProcessor::setParameters(const ava::modules::varispeed::Parameters& p,bool) {
    const auto wasEnabled=enabled;
    initial=p.varispeed;
    engine.setParameters(initial);
    enabled=std::abs(initial.rate-1.f)>1.e-6f&&p.available();
    if(wasEnabled!=enabled)engine.reset();
}
void VarispeedProcessor::process(double& left,double& right) noexcept {
    if(!enabled)return;
    const auto out=engine.process(left,right);left=out[0];right=out[1];
}
}
