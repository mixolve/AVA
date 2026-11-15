#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void PanoramaProcessor::setParameters(const ava::modules::panorama::Parameters& p, bool) {
    using ava::modules::dsp::roundToParameterStep;
    const auto leftPosition=roundToParameterStep(p.left)*.01;
    const auto rightPosition=roundToParameterStep(p.right)*.01;
    const auto leftTheta=(leftPosition+1.)*juce::MathConstants<double>::pi*.25;
    const auto rightTheta=(rightPosition+1.)*juce::MathConstants<double>::pi*.25;
    const auto lawDb=juce::jlimit(0.f,6.f,p.law);
    const auto lawGainForPosition=[lawDb](double position) {
        const auto distance=juce::jlimit(0.,1.,std::abs(position));
        return ava::modules::dsp::dbToAmp(-lawDb*std::cos(distance*juce::MathConstants<double>::pi*.5));
    };
    const auto gl=lawGainForPosition(leftPosition),gr=lawGainForPosition(rightPosition);
    gLL=std::cos(leftTheta)*gl;gLR=std::sin(leftTheta)*gl;
    gRL=std::cos(rightTheta)*gr;gRR=std::sin(rightTheta)*gr;
    constexpr double eps=1.e-9;
    enabled=p.available() && (std::abs(gLL-1.)>eps || std::abs(gLR)>eps || std::abs(gRL)>eps || std::abs(gRR-1.)>eps);
}
void PanoramaProcessor::process(double& left,double& right) noexcept {
    if (!enabled) return;
    const auto l=left*gLL+right*gRL;
    const auto r=left*gLR+right*gRR;
    left=l;right=r;
}
}
