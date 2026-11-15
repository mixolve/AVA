#include "Processor.h"
#include "../shared/DspUtilities.h"
#include <cmath>
#include <algorithm>
namespace ava::modules::chain::dsp {
void CombProcessor::prepare(double rate) {
    sampleRate=rate>0?rate:44100.;
    engine.prepare(sampleRate);
    for(auto& channel:dryBuffer)channel.assign(size_t(engine.getLatencySamples()),0.);
    resetComb();smoothedMix=targetMix;ramp=0;
}
void CombProcessor::reset() {resetComb();smoothedMix=targetMix;ramp=0;}
void CombProcessor::resetComb() noexcept {
    engine.reset();
    for(auto& channel:dryBuffer)std::fill(channel.begin(),channel.end(),0.);
    position=0;
}
void CombProcessor::setParameters(const ava::modules::comb::Parameters& p,bool processed) {
    const auto wasEnabled=enabled;
    enabled=p.combCount>0&&!p.combCountBypass&&p.available();
    targetMix=double(juce::jlimit(0.f,100.f,p.combMix))*.01;
    if(enabled)engine.setComb(float(juce::jlimit(2,100,p.combCount)));
    engine.setShift(p.combShiftBypass?0.f:juce::jlimit(-50.f,50.f,p.combShift));
    if(!processed) {smoothedMix=targetMix;ramp=0;}
    else ramp=juce::jmax(1,int(sampleRate*.005));
    if(wasEnabled!=enabled)resetComb();
}
void CombProcessor::process(double& left,double& right) noexcept {
    if(ramp>0) {smoothedMix+=(targetMix-smoothedMix)/ramp;--ramp;}
    if(!enabled||dryBuffer[0].empty()||dryBuffer[1].empty())return;
    const auto dryLeft=dryBuffer[0][position],dryRight=dryBuffer[1][position];
    dryBuffer[0][position]=left;dryBuffer[1][position]=right;
    if(++position==dryBuffer[0].size())position=0;
    const auto output=engine.process(float(.5*(left+right)));
    left=dryLeft*(1.-smoothedMix)+double(output[0])*smoothedMix;
    right=dryRight*(1.-smoothedMix)+double(output[1])*smoothedMix;
}
}
