#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void OrthoProcessor::setParameters(const ava::modules::ortho::Parameters& p, bool) {
    const auto theta=ava::modules::dsp::roundToParameterStep(p.degree)*(juce::MathConstants<double>::pi/180.);
    const auto c=std::cos(theta),s=std::sin(theta);
    const auto flipRight = p.flipRight && !p.flipRightBypassed;
    m11=c;m12=flipRight ? s : -s;m21=s;m22=flipRight ? -c : c;
    constexpr double eps=1.e-9;
    enabled=p.available() && (std::abs(m11-1.)>eps || std::abs(m12)>eps || std::abs(m21)>eps || std::abs(m22-1.)>eps);
}
void OrthoProcessor::process(double& left,double& right) noexcept {
    if (!enabled) return;
    const auto l=m11*left+m12*right;
    const auto r=m21*left+m22*right;
    left=l;right=r;
}
}
