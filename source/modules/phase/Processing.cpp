#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void PhaseProcessor::prepare(double) { initialiseCoefficients();reset(); }
void PhaseProcessor::reset() noexcept { position=0;leftBuffer.fill(0.);rightBuffer.fill(0.); }
void PhaseProcessor::setParameters(const ava::modules::phase::Parameters& p,bool) {
    using ava::modules::dsp::roundToParameterStep;
    const auto stereo=juce::jlimit(-180.,180.,roundToParameterStep(p.stereoPhase));
    const auto phaseL=std::remainder(stereo+juce::jlimit(-180.,180.,roundToParameterStep(p.leftPhase)),360.)*(juce::MathConstants<double>::pi/180.);
    const auto phaseR=std::remainder(stereo+juce::jlimit(-180.,180.,roundToParameterStep(p.rightPhase)),360.)*(juce::MathConstants<double>::pi/180.);
    enabled=p.available()&&(std::abs(phaseL)>1.e-9||std::abs(phaseR)>1.e-9);
    leftCos=std::cos(phaseL);leftSin=std::sin(phaseL);rightCos=std::cos(phaseR);rightSin=std::sin(phaseR);
}
void PhaseProcessor::process(double& left,double& right) noexcept {
    if (!enabled)return;
    using ava::modules::dsp::wrapIndex;
    leftBuffer[size_t(position)]=left;rightBuffer[size_t(position)]=right;
    const auto dl=leftBuffer[size_t(wrapIndex(position-filterLatency,bufferSize))];
    const auto dr=rightBuffer[size_t(wrapIndex(position-filterLatency,bufferSize))];
    double hl=0.,hr=0.;
    for(int i=0;i<filterTaps;++i) {
        const auto read=size_t(wrapIndex(position-i,bufferSize));const auto coef=coefficients[size_t(i)];
        hl+=coef*leftBuffer[read];hr+=coef*rightBuffer[read];
    }
    left=dl*leftCos+hl*leftSin;right=dr*rightCos+hr*rightSin;
    position=wrapIndex(position+1,bufferSize);
}
void PhaseProcessor::initialiseCoefficients() {
    constexpr auto pi=juce::MathConstants<double>::pi;
    for(int i=0;i<filterTaps;++i) {
        const auto k=i-filterLatency;
        double coef=0.;
        if(k!=0&&(std::abs(k)&1)!=0)coef=2./(pi*double(k));
        const auto window=.42-.5*std::cos(2.*pi*i/(filterTaps-1))+.08*std::cos(4.*pi*i/(filterTaps-1));
        coefficients[size_t(i)]=coef*window;
    }
}
}
