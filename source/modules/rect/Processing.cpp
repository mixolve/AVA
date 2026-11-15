#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void RectProcessor::setParameters(const ava::modules::rect::Parameters& p, bool) {
    mode=0;
    if (!p.available()) return;
    if (p.halfPositive) mode=1;
    else if (p.halfNegative) mode=2;
    else if (p.fullPositive) mode=3;
    else if (p.fullNegative) mode=4;
}
void RectProcessor::process(double& left,double& right) noexcept {
    switch(mode) {
    case 1: left=juce::jmax(left,0.);right=juce::jmax(right,0.);break;
    case 2: left=juce::jmin(left,0.);right=juce::jmin(right,0.);break;
    case 3: left=std::abs(left);right=std::abs(right);break;
    case 4: left=-std::abs(left);right=-std::abs(right);break;
    default: break;
    }
}
}
