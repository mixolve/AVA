#include "Processor.h"
#include <algorithm>
#include <cmath>
namespace ava::modules::chain::dsp {
void SibilProcessor::FadeStage::reset() noexcept {
    std::fill(history.begin(),history.end(),std::array<double,2>{});
    sum={};position=0;
}
std::array<double,2> SibilProcessor::FadeStage::process(const std::array<double,2> input) noexcept {
    auto& leaving=history[size_t(position)];
    std::array<double,2> output {};
    for(size_t channel=0;channel<output.size();++channel) {
        sum[channel]+=input[channel]-leaving[channel];
        output[channel]=std::clamp(sum[channel]/length,0.,1.);
    }
    leaving=input;
    if(++position==length)position=0;
    return output;
}
size_t SibilProcessor::at(int64_t sample) const noexcept {
    const auto n=int64_t(rawMask.size()); return size_t((sample%n+n)%n);
}
void SibilProcessor::prepare(double rate) {
    sampleRate=std::max(8000.,rate);
    int order=8;while((1<<order)<sampleRate*.02 && order<14)++order;
    fftSize=1<<order;hop=fftSize/4;analysisDelay=fftSize/2+hop;
    fft=std::make_unique<juce::dsp::FFT>(order);
    const auto capacity=size_t(fftSize*4+std::ceil(sampleRate*.5)+16);
    for(auto& channel:audio)channel.assign(capacity,0.);
    rawMask.assign(capacity,0);confirmedMask.assign(capacity,0);expandedMask.assign(capacity,0);
    rangeMask.assign(capacity,0);rangeScale.assign(capacity,0.);
    const int maximumFade=std::max(1,int(std::ceil(sampleRate*.1)));
    const auto stageCapacity=size_t((maximumFade+1)/3+1);
    for(auto& stage:fadeStages)stage.history.assign(stageCapacity,{});
    spectrum.assign(size_t(fftSize*2),0.f);hann.resize(size_t(fftSize));
    correlation.resize(size_t(fftSize));
    for(int i=0;i<fftSize;++i)hann[size_t(i)]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(fftSize-1)));
    gainCoefficient=1.-std::exp(-1./(.005*sampleRate));
    setParameters(parameters,false);reset();
}
void SibilProcessor::reset() noexcept {
    for(auto& channel:audio)std::fill(channel.begin(),channel.end(),0.);
    std::fill(rawMask.begin(),rawMask.end(),0);std::fill(confirmedMask.begin(),confirmedMask.end(),0);
    std::fill(expandedMask.begin(),expandedMask.end(),0);expandedCount=0;
    std::fill(rangeMask.begin(),rangeMask.end(),0);std::fill(rangeScale.begin(),rangeScale.end(),0.);
    position=0;run=0;lastMask=brightness=noisiness=0.;levelDb=-120.;
    eventActive=eventResolved=eventAccepted=false;eventStart=0;eventLength=0;
    eventPeak=0.;eventGain=1.;
    for(auto& stage:fadeStages)stage.reset();
    gainS=targetS;gainRest=targetRest;
}
void SibilProcessor::setParameters(const ava::modules::sibil::Parameters& p,bool processed) {
    const auto wasActive=isActive();const auto oldWindow=windowSamples, oldFade=fadeSamples, oldRange=rangeSamples, oldPre=preSamples, oldPost=postSamples;parameters=p;
    windowSamples=std::max(1,int(std::round(std::clamp(double(p.window),0.,100.)*.001*sampleRate)));
    fadeSamples=std::max(1,int(std::round(std::clamp(double(p.fade),0.,100.)*.001*sampleRate)));
    // Partition the existing finite fade span between three FIR stages.
    // Their supports add to FADE exactly, preserving the carrier alignment.
    const int support=fadeSamples-1;
    for(size_t i=0;i<fadeStages.size();++i)
        fadeStages[i].length=support/3+(int(i)<support%3?1:0)+1;
    rangeSamples=p.rangeEnabled?std::max(1,int(std::round(.1*sampleRate))):0;
    preSamples=std::max(0,int(std::round(std::clamp(double(p.pre),0.,100.)*.001*sampleRate)));
    postSamples=std::max(0,int(std::round(std::clamp(double(p.post),0.,100.)*.001*sampleRate)));
    latency=analysisDelay+windowSamples+preSamples+(fadeSamples-1)/2+rangeSamples;
    const bool anySolo=p.soloS||p.soloRest;
    targetS=(!anySolo||p.soloS)?juce::Decibels::decibelsToGain(std::clamp(double(p.gainS),-48.,48.),-96.):0.;
    targetRest=(!anySolo||p.soloRest)?juce::Decibels::decibelsToGain(std::clamp(double(p.gainRest),-48.,48.),-96.):0.;
    if(!processed){gainS=targetS;gainRest=targetRest;}
    if(!rawMask.empty()&&(wasActive!=isActive()||windowSamples!=oldWindow||fadeSamples!=oldFade||rangeSamples!=oldRange||preSamples!=oldPre||postSamples!=oldPost))reset();
}
void SibilProcessor::analyse() noexcept {
    // Pick the stronger channel for analysis; opposite-polarity stereo never cancels.
    double powers[2]{};
    for(int i=0;i<fftSize;++i)for(int ch=0;ch<2;++ch){auto x=audio[size_t(ch)][at(position-fftSize+1+i)];powers[ch]+=x*x;}
    const int channel=powers[0]>=powers[1]?0:1;
    levelDb=juce::Decibels::gainToDecibels(std::sqrt(powers[channel]/fftSize),-120.);
    std::fill(spectrum.begin(),spectrum.end(),0.f);
    for(int i=0;i<fftSize;++i)spectrum[size_t(i)]=float(audio[size_t(channel)][at(position-fftSize+1+i)])*hann[size_t(i)];
    fft->performFrequencyOnlyForwardTransform(spectrum.data());
    double total=0.,high=0.;
    for(int bin=1;bin<=fftSize/2;++bin){double power=double(spectrum[size_t(bin)])*spectrum[size_t(bin)];double frequency=bin*sampleRate/fftSize;
        total+=power;high+=power*std::clamp((frequency-1500.)/3000.,0.,1.);}
    brightness=total>1e-20?100.*high/total:0.;
    // Normalised autocorrelation measures periodicity, independent of level.
    // Decimate with a box low-pass so lag work stays bounded at high sample rates.
    const int stride=std::max(1,int(sampleRate/12000.));const int n=fftSize/stride;
    double mean=0.;
    for(int i=0;i<n;++i){double x=0.;for(int j=0;j<stride;++j)x+=audio[size_t(channel)][at(position-fftSize+1+i*stride+j)];correlation[size_t(i)]=float(x/stride);mean+=x/stride;}
    mean/=n;for(int i=0;i<n;++i)correlation[size_t(i)]-=float(mean);
    const double analysisRate=sampleRate/stride;
    const int first=std::max(2,int(analysisRate/1200.));const int last=std::min(n*2/3,int(analysisRate/70.));
    double periodicity=0.;
    for(int lag=first;lag<=last;++lag){double cross=0.,a=0.,b=0.;for(int i=0;i<n-lag;++i){double x=correlation[size_t(i)],y=correlation[size_t(i+lag)];cross+=x*y;a+=x*x;b+=y*y;}if(a*b>1e-24)periodicity=std::max(periodicity,cross/std::sqrt(a*b));}
    noisiness=100.*(1.-std::clamp(periodicity,0.,1.));
    const auto detected=levelDb>=parameters.level && brightness>=parameters.brightness && noisiness>=parameters.noise;
    // Label the centre of the analysed frame, not its trailing edge.
    const auto centre=position-fftSize/2;
    for(int i=0;i<hop;++i)rawMask[at(centre-i)]=detected?1:0;
}
void SibilProcessor::finishRangeEvent(const int64_t lastSample) noexcept {
    const double minimum=juce::Decibels::decibelsToGain(double(parameters.minDb),-120.);
    const double maximum=juce::Decibels::decibelsToGain(double(parameters.maxDb),-120.);
    eventAccepted=eventPeak>=minimum;
    eventGain=eventPeak>0.?std::min(1.,maximum/eventPeak):1.;
    // Commit the decision to the entire event, including its beginning.
    // The additional 100 ms keeps these samples ahead of the output reader.
    for(auto sample=eventStart;sample<=lastSample;++sample) {
        rangeMask[at(sample)]=eventAccepted?1:0;
        rangeScale[at(sample)]=eventAccepted?eventGain:0.;
    }
    eventResolved=true;
}
void SibilProcessor::process(double& left,double& right) noexcept {
    if(!isActive()||rawMask.empty())return;
    const auto now=at(position);audio[0][now]=left;audio[1][now]=right;rawMask[now]=0;confirmedMask[now]=0;expandedMask[now]=0;rangeMask[now]=0;rangeScale[now]=0.;
    if(position+1>=fftSize&&(position+1)%hop==0)analyse();
    const auto classified=position-analysisDelay;
    if(classified>=0&&rawMask[at(classified)]){
        run=std::min(run+1,windowSamples);
        if(run==windowSamples){
            // The whole event is retained only after its minimum duration is confirmed.
            if(!confirmedMask[at(classified-1)])for(int i=0;i<windowSamples;++i)confirmedMask[at(classified-i)]=1;
            else confirmedMask[at(classified)]=1;
        }
    }else run=0;
    const auto source=classified-windowSamples;
    // Dilate only confirmed events. Reading PRE samples ahead expands the
    // beginning; retaining POST samples behind expands the end, without
    // changing FADE or shifting the event as a whole. O(1) work per sample.
    if(source>=0)expandedCount+=confirmedMask[at(source)];
    const auto leaving=source-preSamples-postSamples-1;
    if(leaving>=0)expandedCount-=confirmedMask[at(leaving)];
    const auto confirmed=source-preSamples;
    if(confirmed>=0)expandedMask[at(confirmed)]=expandedCount>0?1:0;
    if(rangeSamples>0 && confirmed>=0) {
        if(expandedMask[at(confirmed)]) {
            if(!eventActive) {
                eventActive=true;eventResolved=false;eventStart=confirmed;eventLength=0;eventPeak=0.;
            }
            if(!eventResolved) {
                eventPeak=std::max({eventPeak,std::abs(audio[0][at(confirmed)]),std::abs(audio[1][at(confirmed)])});
                if(++eventLength>=rangeSamples)finishRangeEvent(confirmed);
            } else {
                rangeMask[at(confirmed)]=eventAccepted?1:0;
                rangeScale[at(confirmed)]=eventAccepted?eventGain:0.;
            }
        } else {
            // Shorter events use their complete peak instead of borrowing
            // samples from the following tonal passage or the next event.
            if(eventActive&&!eventResolved)finishRangeEvent(confirmed-1);
            eventActive=false;
            rangeMask[at(confirmed)]=0;rangeScale[at(confirmed)]=0.;
        }
    }
    const auto newest=confirmed-rangeSamples;
    const auto& mask=rangeSamples>0?rangeMask:expandedMask;
    // Smooth the complete event stream, including partial rises and reversals.
    // The range-gain contribution follows the identical filter so S-RANGE
    // cannot introduce a separate sharp gain transition inside a smooth mask.
    std::array<double,2> filtered {double(mask[at(newest)]),rangeSamples>0?rangeScale[at(newest)]:0.};
    for(auto& stage:fadeStages)filtered=stage.process(filtered);
    const double fraction=filtered[0];
    const double cosine=.5-.5*std::cos(juce::MathConstants<double>::pi*fraction);
    lastMask=std::clamp(cosine,0.,1.);
    const double eventScale=rangeSamples>0&&fraction>0.?std::clamp(filtered[1]/fraction,0.,1.):1.;
    const auto carrier=at(position-latency);
    gainS+=(targetS-gainS)*gainCoefficient;gainRest+=(targetRest-gainRest)*gainCoefficient;
    const auto split=[&](double x){const double s=x*lastMask,rest=x-s;return s*eventScale*gainS+rest*gainRest;};
    left=split(audio[0][carrier]);right=split(audio[1][carrier]);++position;
}
}
