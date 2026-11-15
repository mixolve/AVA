#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void ShiftProcessor::setParameters(const ava::modules::shift::Parameters& p,bool) {
    const auto wasEnabled=enabled;
    const auto& options = p.shiftOptions;
    const bool soloActive = options.soloOrig != options.soloPitched;
    enabled = (std::abs(p.shiftPitch) > 1.e-6f || soloActive || std::abs(options.q) > 1.e-6f
        || (!options.origGainBypassed && std::abs(options.origGainDb) > 1.e-6f)
        || (!options.pitchedGainBypassed && std::abs(options.pitchedGainDb) > 1.e-6f)) && p.available();
    engine.setParameters(p.shiftPitch,p.shiftFrequency,p.shiftOptions);
    if(wasEnabled!=enabled)engine.reset();
}
void ShiftProcessor::process(double& left,double& right) noexcept {
    if(!enabled)return;
    const auto out=engine.process(left,right);left=out[0];right=out[1];
}
}
