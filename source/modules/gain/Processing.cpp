#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void GainProcessor::setParameters(const ava::modules::gain::Parameters& p, bool processed) {
    const auto effective=[](double value,bool muted) {
        using namespace ava::modules::dsp;
        const auto snapped=juce::jlimit(gainMinimumDb,gainMaximumDb,roundToParameterStep(value));
        return mutedGainToAmp(snapped,muted||isGainMuted(value));
    };
    targets={effective(p.gainLr,p.gainLrMute),effective(p.gainL,p.gainLMute),effective(p.gainR,p.gainRMute),effective(p.gainMid,p.gainMidMute),effective(p.gainSide,p.gainSideMute)};
    for(size_t i=0;i<targets.size();++i)
        if (!p.available() || p.gainBypass[i]) targets[i]=1.;
    enabled=false;
    for(auto target:targets) enabled=enabled||std::abs(target-1.)>1.e-9;
    if (!processed) { smoothed=targets; ramp=0; }
    else ramp=juce::jmax(1,int(sampleRate*.005));
}
void GainProcessor::process(double& left,double& right) noexcept {
    if (ramp>0) {
        for(size_t i=0;i<targets.size();++i) smoothed[i]+=(targets[i]-smoothed[i])/ramp;
        --ramp;
    }
    if (!enabled && ramp==0) return;
    for(auto stage:order) {
        switch(stage) {
        case gain::Id::stereo: left*=smoothed[0];right*=smoothed[0];break;
        case gain::Id::left: left*=smoothed[1];break;
        case gain::Id::right: right*=smoothed[2];break;
        case gain::Id::mid: { const auto mid=.5*(left+right)*smoothed[3];const auto side=.5*(left-right);left=mid+side;right=mid-side;break; }
        case gain::Id::side: { const auto mid=.5*(left+right);const auto side=.5*(left-right)*smoothed[4];left=mid+side;right=mid-side;break; }
        }
    }
}
}
