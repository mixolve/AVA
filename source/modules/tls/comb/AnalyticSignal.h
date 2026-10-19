#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

// Ported from Orbit Stereo 0.7.2.
namespace tls::dsp::comb
{

// Uniform partitioned FIR Hilbert transformer. No audio-thread allocations.
// Real path is an exactly matched delay; positive-frequency quadrature is -j.
class AnalyticSignal
{
    using Complex=std::complex<double>;
public:
    void prepare(double rate)
    {
        hop=128;while(double(hop)<rate*(128.0/48000.0))hop*=2;
        size=hop*2;
        half=std::max(2,int(std::lround(rate*(2048.0/48000.0))));
        half+=(half%2); // odd Hilbert coefficients, even center
        const int length=half*2+1;
        partitions=(length+hop-1)/hop;
        kernels.assign(size_t(partitions),std::vector<Complex>(size_t(size)));
        spectra=kernels;work.assign(size_t(size),{});sum=work;
        input.assign(size_t(hop),0);output=input;overlap=input;
        dry.assign(size_t(half+hop),0);
        roots.resize(size_t(size/2));
        for(int k=0;k<size/2;++k)roots[size_t(k)]=std::polar(1.0,-2*pi*k/size);
        const double norm=i0(5.0);
        for(int n=0;n<length;++n)
        {
            const int offset=n-half;
            if(offset%2==0)continue;
            const double p=double(offset)/half;
            const double h=2/(pi*offset)*i0(5*std::sqrt(std::max(0.,1-p*p)))/norm;
            kernels[size_t(n/hop)][size_t(n%hop)]=h;
        }
        for(auto& kernel:kernels)fft(kernel,false);
        reset();
    }
    int latency() const{return half+hop;}
    void reset()
    {
        for(auto& spectrum:spectra)std::fill(spectrum.begin(),spectrum.end(),Complex{});
        std::fill(input.begin(),input.end(),0);std::fill(output.begin(),output.end(),0);
        std::fill(overlap.begin(),overlap.end(),0);std::fill(dry.begin(),dry.end(),0);
        position=0;dryPosition=0;latest=0;
    }
    Complex process(double x)
    {
        const double real=dry[dryPosition];dry[dryPosition]=x;
        if(++dryPosition==dry.size())dryPosition=0;
        const double imaginary=output[position];input[position]=x;
        if(++position==input.size())
        {
            position=0;
            std::fill(work.begin(),work.end(),Complex{});
            for(int n=0;n<hop;++n)work[size_t(n)]=input[size_t(n)];
            fft(work,false);spectra[latest]=work;
            std::fill(sum.begin(),sum.end(),Complex{});
            size_t index=latest;
            for(size_t p=0;p<kernels.size();++p)
            {
                for(size_t k=0;k<sum.size();++k)sum[k]+=spectra[index][k]*kernels[p][k];
                if(index==0)index=spectra.size();--index;
            }
            fft(sum,true);
            for(int n=0;n<hop;++n)
            {output[size_t(n)]=sum[size_t(n)].real()+overlap[size_t(n)];overlap[size_t(n)]=sum[size_t(n+hop)].real();}
            if(++latest==spectra.size())latest=0;
        }
        return {real,imaginary};
    }
private:
    static constexpr double pi=3.14159265358979323846;
    static double i0(double x)
    {double total=1,term=1;for(int k=1;k<30;++k){term*=x*x/(4*k*k);total+=term;if(term<total*1e-16)break;}return total;}
    void fft(std::vector<Complex>& data,bool inverse)
    {
        for(size_t i=1,j=0;i<data.size();++i)
        {size_t bit=data.size()>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(data[i],data[j]);}
        for(size_t length=2;length<=data.size();length*=2)
        {
            const size_t stride=data.size()/length;
            for(size_t start=0;start<data.size();start+=length)
                for(size_t j=0;j<length/2;++j)
                {
                    const auto w=inverse?std::conj(roots[j*stride]):roots[j*stride];
                    const auto a=data[start+j],b=data[start+j+length/2]*w;
                    data[start+j]=a+b;data[start+j+length/2]=a-b;
                }
        }
        if(inverse)for(auto& x:data)x/=double(data.size());
    }
    int hop=128,size=256,half=2048,partitions=33;
    size_t position=0,dryPosition=0,latest=0;
    std::vector<std::vector<Complex>> kernels,spectra;
    std::vector<Complex> roots,work,sum;
    std::vector<double> input,output,overlap,dry;
};

}
