#pragma once
#include "AnalyticSignal.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

// Ported from Orbit Stereo 0.7.2.
namespace tls::dsp::comb
{

// S(f) = A(f) * translatedDelay(f). The reference allpass A stays fixed.
// Shift moves the comb interference without translating the DC phase bends.
// At Count=62, Shift=0 the response matches measured Wider 100%.
class StereoEngine
{
    using Complex=std::complex<double>;
public:
    static constexpr int minimumComb=2,maximumComb=100,referenceComb=62;
    static constexpr float minimumShift=-50,maximumShift=50;
    static double periodHz(double rate,int count)
    {
        const double delay=1+double(std::clamp(count,minimumComb,maximumComb)-minimumComb)*(rate*.003-1)/double(referenceComb-minimumComb);
        return rate/delay;
    }
    int getLatencySamples() const{return analytic.latency();}
    void prepare(double rate)
    {
        sampleRate=rate;slew=1-std::exp(-1/(.025*rate));fadeSamples=std::max(1,int(rate*.025));
        analytic.prepare(rate);
        history.assign(size_t(std::ceil(rate*.006))+8,{});
        targetTap=makeTap(targetCount);reset();
    }
    void reset()
    {
        analytic.reset();sections={};std::fill(history.begin(),history.end(),Complex{});position=0;
        oscillator={1,0};oscillatorCounter=0;shift=targetShift*.01*periodHz(sampleRate,targetCount);currentCount=targetCount;currentTap=targetTap;fadeRemaining=0;
    }
    void setComb(float count,bool immediate=false)
    {
        const int requested=std::clamp(int(std::lround(count)),minimumComb,maximumComb);
        if(requested!=targetCount){targetCount=requested;targetTap=makeTap(targetCount);}
        if(immediate)reset();
    }
    void setShift(float percent,bool immediate=false)
    {
        targetShift=std::clamp(std::round(double(percent)*100)/100,double(minimumShift),double(maximumShift));
        if(immediate)shift=targetShift*.01*periodHz(sampleRate,targetCount);
    }
    std::array<float,2> process(float mono)
    {
        shift+=slew*(targetShift*.01*periodHz(sampleRate,targetCount)-shift);
        const auto signal=analytic.process(mono);
        Complex shaped=signal;
        constexpr std::array<double,4> coefficients {.6923878,.9360654322959,.9882295226860,.9987488452737};
        for(size_t k=0;k<sections.size();++k)shaped=sections[k].process(shaped,coefficients[k]);
        Complex phased=shaped*std::conj(oscillator);
        // Only the delay lies inside down/up modulation. The reference allpass
        // above it never sees the modulated signal, so its poles do not move.
        history[position]=phased;
        if(fadeRemaining==0 && currentCount!=targetCount)
        {nextCount=targetCount;nextTap=targetTap;fadeRemaining=fadeSamples;}
        Complex side=read(currentTap);
        if(fadeRemaining>0)
        {
            const double blend=double(fadeSamples-fadeRemaining+1)/fadeSamples;
            side+=blend*(read(nextTap)-side);
            if(--fadeRemaining==0){currentCount=nextCount;currentTap=nextTap;}
        }
        if(++position==history.size())position=0;
        const double translated=(side*oscillator).real();
        // Integrate the smoothed frequency: changing Shift never resets phase.
        oscillator*=std::polar(1.0,2*3.14159265358979323846*shift/sampleRate);
        // Prevent oscillator drift over arbitrarily long sessions.
        if(++oscillatorCounter==1024){oscillator/=std::abs(oscillator);oscillatorCounter=0;}
        return {float(signal.real()+translated),float(signal.real()-translated)};
    }
private:
    struct Section
    {
        std::array<Complex,2> state {};size_t index=0;
        Complex process(Complex x,double a)
        {const auto y=a*x+state[index];state[index]=a*y-x;index^=1;return y;}
    };
    struct Tap
    {
        std::array<size_t,4> offsets {};std::array<double,4> weights {};
        size_t whole=0;double coefficient=0;Complex state {};
        bool reference=false,fractional=false;
    };
    Tap makeTap(int count) const
    {
        const double delay=1+double(count-minimumComb)*(sampleRate*.003-1)/double(referenceComb-minimumComb);
        const auto whole=size_t(std::floor(delay));const double m=delay-double(whole);
        Tap tap;tap.whole=whole;
        if(count==referenceComb)
        {
            // Preserve the independently measured Wider transfer exactly,
            // including its own fractional-delay response at rates such as 44.1k.
            tap.reference=true;tap.offsets={whole-1,whole,whole+1,whole+2};
            tap.weights={-m*(m-1)*(m-2)/6,(m+1)*(m-1)*(m-2)/2,-(m+1)*m*(m-2)/2,(m+1)*m*(m-1)/6};
        }
        else
        {
            // First-order Thiran allpass: desired DC delay m, unity magnitude.
            // Integer delays bypass it to avoid the degenerate pole at -1.
            tap.fractional=m>1e-9;tap.coefficient=(1-m)/(1+m);
        }
        return tap;
    }
    Complex read(Tap& tap)
    {
        if(tap.reference)
        {
            Complex value{};
            for(size_t k=0;k<4;++k)value+=tap.weights[k]*history[(position+history.size()-tap.offsets[k])%history.size()];
            return value;
        }
        const auto x=history[(position+history.size()-tap.whole)%history.size()];
        if(!tap.fractional)return x;
        const auto y=tap.coefficient*x+tap.state;
        tap.state=x-tap.coefficient*y;
        return y;
    }
    AnalyticSignal analytic;
    std::array<Section,4> sections {};
    std::vector<Complex> history;
    Complex oscillator {1,0};int oscillatorCounter=0;
    size_t position=0;
    int currentCount=62,targetCount=62,nextCount=62,fadeSamples=1200,fadeRemaining=0;
    Tap currentTap {},targetTap {},nextTap {};
    double sampleRate=48000,slew=.001,shift=0,targetShift=0;
};

}
