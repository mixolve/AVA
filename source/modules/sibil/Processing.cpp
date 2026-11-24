#include "Processor.h"
#include <algorithm>
#include <array>
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
    rawMask.assign(capacity,0);confirmedMask.assign(capacity,0);
    edgeZtwScore.assign(capacity,0.f);edgeHfDb.assign(capacity,-120.f);edgeAperiodic.assign(capacity,0.f);
    const int maximumFade=std::max(1,int(std::ceil(sampleRate*.1)));
    const auto stageCapacity=size_t((maximumFade+1)/3+1);
    for(auto& stage:fadeStages)stage.history.assign(stageCapacity,{});
    spectrum.assign(size_t(fftSize*2),0.f);hann.resize(size_t(fftSize));
    correlation.resize(size_t(fftSize));
    // Original ZTW window (Prasad & Yegnanarayana, Interspeech 2018),
    // normalised by its first non-zero coefficient to avoid float overflow.
    ztwLength=std::min(fftSize/2,std::max(32,int(std::round(sampleRate*.010))));
    ztwHop=std::max(1,int(std::round(sampleRate*.002)));
    ztwWindow.assign(size_t(ztwLength),0.f);
    const auto pi=juce::MathConstants<double>::pi;
    const auto a1=pi/(2.*ztwLength);
    const auto scale=4.*std::pow(std::sin(a1),4)/std::pow(std::cos(a1),2);
    for(int i=1;i<ztwLength;++i){
        const auto a=pi*i/(2.*ztwLength);
        ztwWindow[size_t(i)]=float(scale*std::pow(std::cos(a),2)/(4.*std::pow(std::sin(a),4)));
    }
    ztwTime.assign(size_t(fftSize),{});ztwTimeWeighted.assign(size_t(fftSize),{});
    ztwSpectrum.assign(size_t(fftSize),{});ztwSpectrumWeighted.assign(size_t(fftSize),{});
    ztwAnalyticInput.assign(size_t(fftSize),{});ztwAnalyticOutput.assign(size_t(fftSize),{});
    ztwHngd.assign(size_t(fftSize/2+1),0.f);
    for(int i=0;i<fftSize;++i)hann[size_t(i)]=float(.5-.5*std::cos(juce::MathConstants<double>::twoPi*i/(fftSize-1)));
    gainCoefficient=1.-std::exp(-1./(.005*sampleRate));
    setParameters(parameters,false);reset();
}
void SibilProcessor::reset() noexcept {
    for(auto& channel:audio)std::fill(channel.begin(),channel.end(),0.);
    std::fill(rawMask.begin(),rawMask.end(),0);std::fill(confirmedMask.begin(),confirmedMask.end(),0);
    std::fill(edgeZtwScore.begin(),edgeZtwScore.end(),0.f);
    std::fill(edgeHfDb.begin(),edgeHfDb.end(),-120.f);
    std::fill(edgeAperiodic.begin(),edgeAperiodic.end(),0.f);
    previousAperiodic=eventAperiodicPeak=eventAperiodicBackground=0.;havePreviousAperiodic=false;
    previousHfDb=eventHfPeakDb=eventHfBackgroundDb=-120.;havePreviousHf=false;eventActive=false;detectedEventCount=0;eventCoreStart=eventCoreEnd=eventPeakPosition=eventMaskStart=0;eventZtwPeak=eventZtwBackground=previousZtwScore=ztwSmoothedScore=0.;havePreviousZtw=false;eventTailGap=0;
    position=0;lastMask=brightness=noisiness=0.;levelDb=-120.;
    fftGridOffset=ztwGridOffset=0;nextTimelineSample.reset();lastTransportPlaying.reset();
    for(auto& stage:fadeStages)stage.reset();
    gainS=targetS;gainRest=targetRest;
}
void SibilProcessor::beginBlock(std::optional<int64_t> timelineSample,
                                std::optional<bool> playing, int blockSamples) noexcept {
    if(blockSamples<=0)return;
    // Playback start/stop and edits/seeks invalidate the prior FFT history,
    // confirmation run, event masks and release/edge state. DAWs do not
    // necessarily call AudioProcessor::reset() when rewinding a playhead.
    const bool transportChanged=playing && (!lastTransportPlaying || *lastTransportPlaying!=*playing);
    const bool jumped=timelineSample && nextTimelineSample && *timelineSample!=*nextTimelineSample;
    // If the host stops reporting a position or starts reporting one mid-run,
    // do not silently inherit a detector history with a different FFT grid.
    const bool trackingChanged=(timelineSample.has_value()!=nextTimelineSample.has_value())
        && position>0 && (!playing || *playing);
    if(transportChanged || jumped || trackingChanged)reset();
    if(playing)lastTransportPlaying=*playing;
    // A stopped playhead can report the same sample position every block.
    // It must not look like a seek or advance the expected running position.
    if(playing && !*playing)timelineSample.reset();
    if(timelineSample){
        // Align the first FFT window to the project's sample grid. This
        // removes differences caused by varying previous buffer lengths.
        if(!nextTimelineSample){
            const auto remainder=*timelineSample%hop;
            fftGridOffset=int(remainder<0?remainder+hop:remainder);
            const auto ztwRemainder=*timelineSample%ztwHop;
            ztwGridOffset=int(ztwRemainder<0?ztwRemainder+ztwHop:ztwRemainder);
        }
        nextTimelineSample=*timelineSample+blockSamples;
    }else nextTimelineSample.reset();
}
void SibilProcessor::setParameters(const ava::modules::sibil::Parameters& p,bool processed) {
    const auto wasActive=isActive();const auto oldFade=fadeSamples;parameters=p;
    fadeSamples=std::max(1,int(std::round(std::clamp(double(p.fade),0.,100.)*.001*sampleRate)));
    // Partition the existing finite fade span between three FIR stages.
    // Their supports add to FADE exactly, preserving the carrier alignment.
    const int support=fadeSamples-1;
    for(size_t i=0;i<fadeStages.size();++i)
        fadeStages[i].length=support/3+(int(i)<support%3?1:0)+1;
    // An S event can have a quiet onset long before the strict detector
    // confirms its bright core. Keep that onset available for retrospective
    // HF edge detection. This is real latency and is reported to the host.
    edgeLookbackSamples=std::max(2*fftSize,int(std::ceil(sampleRate*.180)));
    latency=analysisDelay+edgeLookbackSamples+(fadeSamples-1)/2;
    const bool anySolo=p.soloS||p.soloRest;
    targetS=(!anySolo||p.soloS)?juce::Decibels::decibelsToGain(std::clamp(double(p.gainS),-48.,48.),-96.):0.;
    targetRest=(!anySolo||p.soloRest)?juce::Decibels::decibelsToGain(std::clamp(double(p.gainRest),-48.,48.),-96.):0.;
    if(!processed){gainS=targetS;gainRest=targetRest;}
    if(!rawMask.empty()&&(wasActive!=isActive()||fadeSamples!=oldFade))reset();
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
    // Strict recognition decides *whether* a sibilant exists. It is never
    // consulted for the end of an already recognised event.
    const auto detected=levelDb>=parameters.level && brightness>=parameters.brightness && noisiness>=parameters.noise;
    // A separate HF contour is used to *bound* the phonetic event.  ZTW/DRF
    // describes resonance distribution, not the presence of an audible HF
    // component; using it alone can produce arbitrarily long S masks.
    const auto hfDb=std::max(-120.,levelDb+10.*std::log10(std::max(high/std::max(total,1.e-20),1.e-12)));
    const auto centre=position-fftSize/2;
    for(int i=0;i<hop;++i){
        const auto frame=centre-i;
        rawMask[at(frame)]=detected?1:0;
        const auto blend=double(hop-i)/hop;
        edgeHfDb[at(frame)]=float(havePreviousHf
            ?previousHfDb+(hfDb-previousHfDb)*blend:hfDb);
        edgeAperiodic[at(frame)]=float(havePreviousAperiodic
            ?previousAperiodic+(noisiness-previousAperiodic)*blend:noisiness);
    }
    previousHfDb=hfDb;havePreviousHf=true;
    previousAperiodic=noisiness;havePreviousAperiodic=true;
}
// Computes the ZTW Hilbert envelope of the differenced numerator of group
// delay (HNGD), then tracks the dominant resonance (DRF). The method is
// fundamentally different from gating the STFT high-frequency amplitude.
void SibilProcessor::analyseZtw() noexcept {
    const auto anchor=position-ztwLength;
    double energy[2]{};
    // Use the stronger channel for a stable DRF without stereo cancellation.
    for(int ch=0;ch<2;++ch)
        for(int n=0;n<ztwLength;++n){auto a=audio[size_t(ch)][at(anchor+n)];energy[ch]+=a*a;}
    const auto channel=energy[0]>=energy[1]?0:1;
    std::fill(ztwTime.begin(),ztwTime.end(),Complex{});
    std::fill(ztwTimeWeighted.begin(),ztwTimeWeighted.end(),Complex{});
    for(int n=1;n<ztwLength;++n) {
        const auto x=float(audio[size_t(channel)][at(anchor+n)])*ztwWindow[size_t(n)];
        ztwTime[size_t(n)]=Complex{x,0.f};
        ztwTimeWeighted[size_t(n)]=Complex{x*float(n),0.f};
    }
    fft->perform(ztwTime.data(),ztwSpectrum.data(),false);
    fft->perform(ztwTimeWeighted.data(),ztwSpectrumWeighted.data(),false);
    // g(omega) = Re{ conj(X(omega)) * FFT(n*x[n]) }.
    const auto& lastX=ztwSpectrum[size_t(fftSize-1)];
    const auto& lastY=ztwSpectrumWeighted[size_t(fftSize-1)];
    float lastG=lastX.real()*lastY.real()+lastX.imag()*lastY.imag();
    for(int k=0;k<fftSize;++k){
        const auto& a=ztwSpectrum[size_t(k)];
        const auto& b=ztwSpectrumWeighted[size_t(k)];
        const auto g=a.real()*b.real()+a.imag()*b.imag();
        ztwAnalyticInput[size_t(k)]=Complex{g-lastG,0.f};
        lastG=g;
    }
    // Hilbert envelope of differentiated NGD: analytic signal in the
    // frequency-bin domain (DFT/positive-quefrency selection/inverse DFT).
    fft->perform(ztwAnalyticInput.data(),ztwAnalyticOutput.data(),false);
    for(int k=1;k<fftSize/2;++k)ztwAnalyticOutput[size_t(k)]*=2.f;
    for(int k=fftSize/2+1;k<fftSize;++k)ztwAnalyticOutput[size_t(k)]={};
    fft->perform(ztwAnalyticOutput.data(),ztwAnalyticInput.data(),true);
    for(int k=0;k<=fftSize/2;++k){
        // A short spectral-bin average suppresses isolated HNGD spikes.
        double sum=0.;int count=0;
        for(int j=std::max(0,k-2);j<=std::min(fftSize/2,k+2);++j){
            sum+=std::abs(ztwAnalyticInput[size_t(j)]);++count;
        }
        ztwHngd[size_t(k)]=float(sum/count);
    }
    double lowPeak=0., highPeak=0.;int strongestBin=0;double strongest=0.;
    const int nyquistBin=fftSize/2;
    for(int k=1;k<nyquistBin;++k){
        const auto freq=k*sampleRate/fftSize;
        if(freq<100.||freq>std::min(9000.,sampleRate*.47))continue;
        const auto h=double(ztwHngd[size_t(k)]);
        if(h>strongest){strongest=h;strongestBin=k;}
        if(freq<1500.)lowPeak=std::max(lowPeak,h);
        else highPeak=std::max(highPeak,h);
    }
    // The DRF is the strongest resonance in the HNGD spectrum. Strength
    // against low resonances supplies a continuous boundary score.
    const auto drfHz=strongestBin*sampleRate/fftSize;
    const auto highRatio=highPeak/(highPeak+lowPeak+1.e-20);
    const auto drfHigh=drfHz>=1500.?1.:0.;
    double score=.65*highRatio+.35*drfHigh;
    // Silence has no meaningful DRF; numerical background must not become S.
    if(energy[channel]/ztwLength<1.e-12||strongest<1.e-20)score=0.;
    score=std::clamp(score,0.,1.);
    // Track the DRF contour over one ZTW observation window. Individual
    // DRF estimates jitter under frication noise; the contour does not.
    const auto alpha=1.-std::exp(-double(ztwHop)/std::max(1,ztwLength));
    ztwSmoothedScore=havePreviousZtw ? ztwSmoothedScore+alpha*(score-ztwSmoothedScore) : score;
    score=ztwSmoothedScore;
    // Interpolate at the 2 ms analysis hop. This retains temporal resolution
    // without constructing an expensive HNGD spectrum at every audio sample.
    for(int i=0;i<ztwHop;++i){
        const auto sample=anchor-i;
        if(sample<0)break;
        const auto blend=double(ztwHop-i)/ztwHop;
        edgeZtwScore[at(sample)]=float(havePreviousZtw
            ?previousZtwScore+(score-previousZtwScore)*blend:score);
    }
    previousZtwScore=score;
    havePreviousZtw=true;
}

void SibilProcessor::process(double& left,double& right) noexcept {
    if(!isActive()||rawMask.empty())return;
    const auto now=at(position);
    audio[0][now]=left;audio[1][now]=right;
    rawMask[now]=0;confirmedMask[now]=0;edgeZtwScore[now]=0.f;edgeHfDb[now]=-120.f;edgeAperiodic[now]=0.f;
    if(position+1>=fftSize && (position+1+fftGridOffset)%hop==0)analyse();
    if(position>=ztwLength+ztwHop && (position+1+ztwGridOffset)%ztwHop==0)analyseZtw();
    const auto classified=position-analysisDelay;
    if(classified>=0){
        const bool strict=rawMask[at(classified)]!=0;
        // Recognition is independent of the acoustic boundary sensitivity.
        const double edgeSensitivity=std::clamp(double(parameters.edgeDetect)*.01,0.,1.);
        const auto eventPresent=[&](int64_t sample) noexcept {
            const double floorAmp=std::pow(10.,eventHfBackgroundDb/20.);
            const double peakAmp=std::pow(10.,eventHfPeakDb/20.);
            const double span=std::max(0.,peakAmp-floorAmp);
            const double lowEnd=1.-edgeSensitivity;
            const double loweredBackground=floorAmp*std::pow(10.,(-18.*lowEnd*lowEnd)/20.);
            const double hfLimit=20.*std::log10(std::max(1.e-8,
                loweredBackground+(.001*lowEnd+.70*edgeSensitivity)*span));
            const double zSpan=std::max(.1,eventZtwPeak-eventZtwBackground);
            const double resonance=std::clamp((double(edgeZtwScore[at(sample)])
                -eventZtwBackground)/zSpan,0.,1.);
            const double aSpan=std::max(0.,eventAperiodicPeak-eventAperiodicBackground);
            const double weakNoiseLimit=.001*aSpan+.05*eventAperiodicBackground;
            const double strongNoiseLimit=eventAperiodicBackground+.50*aSpan;
            const double noiseLimit=weakNoiseLimit*(1.-edgeSensitivity)
                +strongNoiseLimit*edgeSensitivity;
            return double(edgeHfDb[at(sample)])>hfLimit+1.5*edgeSensitivity*(1.-resonance)
                && double(edgeAperiodic[at(sample)])>noiseLimit;
        };
        // Each complete FFT classification frame provides one recognition
        // decision. No additional duration gate is applied.
        if(!eventActive && strict){
            eventActive=true;eventTailGap=0;
            ++detectedEventCount;
            eventCoreStart=eventCoreEnd=classified;
            eventHfPeakDb=-120.;eventZtwPeak=0.;eventAperiodicPeak=0.;
            int64_t peakPosition=classified;
            for(int i=0;i<hop;++i){
                const auto sample=classified-i;
                const auto value=double(edgeHfDb[at(sample)]);
                if(value>=eventHfPeakDb){eventHfPeakDb=value;peakPosition=sample;}
                eventZtwPeak=std::max(eventZtwPeak,double(edgeZtwScore[at(sample)]));
                eventAperiodicPeak=std::max(eventAperiodicPeak,double(edgeAperiodic[at(sample)]));
            }
            eventPeakPosition=peakPosition;
            std::array<float,512> background{};
            std::array<float,512> backgroundZtw{};
            std::array<float,512> backgroundAperiodic{};
            size_t count=0;
            const auto start=std::max<int64_t>(0,peakPosition-edgeLookbackSamples);
            const auto countSpan=std::max<int64_t>(1,peakPosition-start);
            const auto step=std::max<int64_t>(1,(countSpan+510)/511);
            for(auto frame=start;frame<peakPosition&&count<background.size();frame+=step){
                background[count]=edgeHfDb[at(frame)];
                backgroundZtw[count]=edgeZtwScore[at(frame)];
                backgroundAperiodic[count]=edgeAperiodic[at(frame)];++count;
            }
            if(count){
                const auto rank=(count-1)/5;
                std::nth_element(background.begin(),background.begin()+rank,background.begin()+count);
                std::nth_element(backgroundZtw.begin(),backgroundZtw.begin()+rank,backgroundZtw.begin()+count);
                std::nth_element(backgroundAperiodic.begin(),backgroundAperiodic.begin()+rank,backgroundAperiodic.begin()+count);
                eventHfBackgroundDb=std::min(eventHfPeakDb,double(background[rank]));
                eventZtwBackground=std::min(eventZtwPeak,double(backgroundZtw[rank]));
                eventAperiodicBackground=std::min(eventAperiodicPeak,double(backgroundAperiodic[rank]));
            }else{eventHfBackgroundDb=-120.;eventZtwBackground=eventAperiodicBackground=0.;}
            const int bridge=std::max(1,std::max(3*ztwHop,fftSize));
            int misses=0;int64_t acousticOnset=peakPosition;
            for(auto frame=peakPosition;frame>=start;--frame){
                if(eventPresent(frame)){acousticOnset=frame;misses=0;}
                else if(++misses>=bridge)break;
            }
            // EDGE DETECT alone defines the acoustically supported onset.
            // The first strict FFT frame confirms the event; its peak can still
            // be rising. Future frames refine this onset within the *existing*
            // lookback so that removing WINDOW does not truncate soft attacks.
            eventMaskStart=acousticOnset;
            for(auto frame=acousticOnset;frame<=classified;++frame)confirmedMask[at(frame)]=1;
        }else if(eventActive){
            // The recognised event stays open while strict samples recur.
            // Short detector dropouts belong to the existing event.
            if(strict){
                for(auto frame=eventCoreEnd+1;frame<=classified;++frame)confirmedMask[at(frame)]=1;
                eventCoreEnd=classified;eventTailGap=0;
                const double currentHf=edgeHfDb[at(classified)];
                const bool newPeak=currentHf>eventHfPeakDb;
                if(newPeak){eventHfPeakDb=currentHf;eventPeakPosition=classified;}
                eventZtwPeak=std::max(eventZtwPeak,double(edgeZtwScore[at(classified)]));
                eventAperiodicPeak=std::max(eventAperiodicPeak,double(edgeAperiodic[at(classified)]));
                // Refine only when an FFT estimate brings a stronger HF peak.
                // Earlier event samples are still buffered, and no new event
                // confirmation or user-visible timing parameter is required.
                if(newPeak && (classified+fftGridOffset)%hop==0){
                    const auto earliest=std::max<int64_t>(0,eventCoreStart-edgeLookbackSamples);
                    const int bridge=std::max(1,std::max(3*ztwHop,fftSize));
                    int misses=0;int64_t onset=eventPeakPosition;
                    for(auto frame=eventPeakPosition;frame>=earliest;--frame){
                        if(eventPresent(frame)){onset=frame;misses=0;}
                        else if(++misses>=bridge)break;
                    }
                    if(onset<eventMaskStart){
                        for(auto frame=onset;frame<eventMaskStart;++frame)confirmedMask[at(frame)]=1;
                        eventMaskStart=onset;
                    }
                }
            }else if(++eventTailGap>=edgeLookbackSamples){
                eventActive=false;
                // Recognition is already complete. Inspect the buffered tail
                // and locate its acoustic end without changing event identity.
                int64_t acousticEnd=eventCoreEnd;
                int misses=0;
                const int bridge=std::max(1,std::max(3*ztwHop,fftSize));
                for(auto frame=eventCoreEnd+1;frame<=classified;++frame){
                    if(eventPresent(frame)){acousticEnd=frame;misses=0;}
                    else if(++misses>=bridge)break;
                }
                for(auto frame=eventCoreEnd+1;frame<=acousticEnd;++frame)confirmedMask[at(frame)]=1;
            }
        }
    }
    const auto source=classified-edgeLookbackSamples;
    // The S mask uses a local, event-relative HF boundary, qualified by ZTW.
    // No PRE/POST dilation is used.
    std::array<double,2> filtered {source>=0?double(confirmedMask[at(source)]):0.,0.};
    for(auto& stage:fadeStages)filtered=stage.process(filtered);
    const double fraction=filtered[0];
    const double cosine=.5-.5*std::cos(juce::MathConstants<double>::pi*fraction);
    lastMask=std::clamp(cosine,0.,1.);
    const auto carrier=at(position-latency);
    gainS+=(targetS-gainS)*gainCoefficient;gainRest+=(targetRest-gainRest)*gainCoefficient;
    const auto split=[&](double x){const double s=x*lastMask,rest=x-s;return s*gainS+rest*gainRest;};
    left=split(audio[0][carrier]);right=split(audio[1][carrier]);++position;
}
}
