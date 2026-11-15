#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void DelayProcessor::prepare(double rate) {
    sampleRate=rate>0?rate:44100.;
    const auto maxDelay=int(std::floor(200.*sampleRate/1000.+.5));
    bufferSize=1;while(bufferSize<maxDelay+2)bufferSize*=2;
    leftBuffer.assign(size_t(bufferSize),0.);
    rightBuffer.assign(size_t(bufferSize),0.);
    reset();
}
void DelayProcessor::reset() noexcept {
    writePosition=0;
    std::fill(leftBuffer.begin(),leftBuffer.end(),0.);
    std::fill(rightBuffer.begin(),rightBuffer.end(),0.);
    smoothed=target;ramp=0;
}
void DelayProcessor::setParameters(const ava::modules::delay::Parameters& p, bool processed) {
    using ava::modules::dsp::roundToParameterStep;
    const auto stereo=juce::jlimit(-100.,100.,roundToParameterStep(p.stereoDelayMs));
    const auto left=juce::jlimit(-100.,100.,roundToParameterStep(p.leftDelayMs));
    const auto right=juce::jlimit(-100.,100.,roundToParameterStep(p.rightDelayMs));
    const auto leftOffset=stereo+left,rightOffset=stereo+right;
    const auto lookaheadMs=juce::jmax(0.,-juce::jmin(leftOffset,rightOffset));
    const auto msToSamples=[this](double ms) {return int(std::floor(ms*sampleRate/1000.+.5));};
    enabled=p.available() && (std::abs(leftOffset)>1.e-9 || std::abs(rightOffset)>1.e-9);
    target={enabled?double(msToSamples(lookaheadMs+leftOffset)):0.,enabled?double(msToSamples(lookaheadMs+rightOffset)):0.};
    lookahead=enabled?msToSamples(lookaheadMs):0;
    if (!processed) {smoothed=target;ramp=0;}
    else ramp=juce::jmax(1,int(sampleRate*.005));
}
void DelayProcessor::process(double& left,double& right) noexcept {
    if (ramp>0) {
        smoothed[0]+=(target[0]-smoothed[0])/ramp;
        smoothed[1]+=(target[1]-smoothed[1])/ramp;
        --ramp;
    }
    if ((!enabled && ramp==0) || leftBuffer.empty() || rightBuffer.empty())return;
    leftBuffer[size_t(writePosition)]=left;rightBuffer[size_t(writePosition)]=right;
    const auto read=[this](const std::vector<double>& data,double delay) {
        const auto whole=int(std::floor(delay));const auto fraction=delay-whole;
        auto first=writePosition-whole;if(first<0)first+=bufferSize;
        auto second=first-1;if(second<0)second+=bufferSize;
        return data[size_t(first)]+fraction*(data[size_t(second)]-data[size_t(first)]);
    };
    left=read(leftBuffer,smoothed[0]);right=read(rightBuffer,smoothed[1]);
    if (++writePosition==bufferSize)writePosition=0;
}
}
