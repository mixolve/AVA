#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void ListenProcessor::setParameters(const ava::modules::listen::Parameters& p, bool) {
    mode=Mode::neutral;
    if (p.listenLc) mode=Mode::leftCenter;
    else if (p.listenRc) mode=Mode::rightCenter;
    else if (p.listenMc) mode=Mode::midCenter;
    else if (p.listenSc) mode=Mode::sideCenter;
    else if (p.listenLl) mode=Mode::leftLeft;
    else if (p.listenRr) mode=Mode::rightRight;
    else if (p.listenSs) mode=Mode::sideStereo;
    if (!p.available()) mode=Mode::neutral;
    msGain=p.listenMsPlus3 ? ava::modules::dsp::dbToAmp(3.0) : 1.0;
}
void ListenProcessor::process(double& left,double& right) noexcept {
    if (!isActive()) return;
    const auto inLeft=left,inRight=right;
    const auto mid=.5*(left+right),side=.5*(left-right);
    switch(mode) {
    case Mode::leftCenter: left=right=inLeft; break;
    case Mode::rightCenter: left=right=inRight; break;
    case Mode::midCenter: left=right=mid*msGain; break;
    case Mode::sideCenter: left=right=side*msGain; break;
    case Mode::leftLeft: right=0.; break;
    case Mode::rightRight: left=0.; break;
    case Mode::sideStereo: left=side*msGain; right=-left; break;
    case Mode::neutral: break;
    }
}
}
