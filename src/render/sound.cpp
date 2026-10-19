#include "sound.hpp"
#include <miniaudio.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace ac=albert::physics::acoustics;
// Spectral synthesis as NASA's auralizations do it (Rizzi and Sahai 2019): every hop, a frame of random phases whose
// magnitudes put each band's power on the bins inside it, inverse FFT, sine window, overlap-add at half the frame (the
// window's square sums to one, so the power holds). 8192 points at 48 kHz: 5.9 Hz bins, 85 ms hops.
namespace {
constexpr int first=12; // The band at 20 Hz (1000·10^(-17/10)); those below are infrasound.
constexpr int count=ac::bands-first;
constexpr std::size_t size=8192,hop=size/2;
constexpr double pi=3.14159265358979323846;
struct Random {std::uint64_t s=0x9e3779b97f4a7c15ull;double next(){s^=s<<13;s^=s>>7;s^=s<<17;return static_cast<double>(s>>11)*(1./9007199254740992.);}}; // [0, 1).
void inverse(std::vector<std::complex<double>>& a){ // In-place radix-2 inverse FFT, unscaled.
    const std::size_t n=a.size();
    for(std::size_t i=1,j=0;i<n;++i){std::size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(std::size_t len=2;len<=n;len<<=1){const std::complex<double> w=std::polar(1.,2*pi/static_cast<double>(len));
        for(std::size_t i=0;i<n;i+=len){std::complex<double> z=1;for(std::size_t k=0;k<len/2;++k){const auto u=a[i+k],v=a[i+k+len/2]*z;a[i+k]=u+v;a[i+k+len/2]=u-v;z*=w;}}}
}
}
struct albert::Sound::Impl {
    std::array<std::atomic<float>,count> powers{};std::atomic<float> panTarget{0}; // Band powers (digital units²) for the next hop.
    std::array<int,size/2> band{};std::array<int,count> bins{};std::array<double,size> window{};
    std::vector<std::complex<double>> spectrum=std::vector<std::complex<double>>(size);std::vector<double> overlap=std::vector<double>(hop),ready=std::vector<double>(hop);
    std::size_t position=hop;float pan=0;Random random;double adaptation=0;ma_device device{};bool open=false;std::atomic<float> volume{1};
    Impl(){
        for(auto& p:powers)p=0;
        // Each bin to the band holding its frequency; a band narrower than a bin takes the nearest one.
        for(std::size_t k=0;k<size/2;++k){band[k]=-1;const double f=static_cast<double>(k)*rate/size;
            for(int b=0;b<count;++b){const double c=ac::bandCentre(first+b);if(f>=c*std::pow(2.,-1./6)&&f<c*std::pow(2.,1./6)){band[k]=b;++bins[static_cast<std::size_t>(b)];break;}}}
        for(int b=0;b<count;++b)if(bins[static_cast<std::size_t>(b)]==0){const auto k=static_cast<std::size_t>(std::lround(ac::bandCentre(first+b)*size/rate));if(k>0&&k<size/2&&band[k]<0){band[k]=b;bins[static_cast<std::size_t>(b)]=1;}}
        for(std::size_t i=0;i<size;++i)window[i]=std::sin(pi*(static_cast<double>(i)+.5)/size);
    }
    void frame(){ // The next hop: this frame's first half over the last one's second.
        std::array<float,count> p;for(int b=0;b<count;++b)p[static_cast<std::size_t>(b)]=powers[static_cast<std::size_t>(b)].load(std::memory_order_relaxed);
        std::fill(spectrum.begin(),spectrum.end(),std::complex<double>{});
        for(std::size_t k=1;k<size/2;++k){const int b=band[k];if(b<0)continue;
            const double a=static_cast<double>(size)*std::sqrt(p[static_cast<std::size_t>(b)]/(2.*bins[static_cast<std::size_t>(b)]));
            spectrum[k]=std::polar(a,2*pi*random.next());spectrum[size-k]=std::conj(spectrum[k]);}
        inverse(spectrum);
        for(std::size_t i=0;i<hop;++i){ready[i]=overlap[i]+spectrum[i].real()/size*window[i];overlap[i]=spectrum[i+hop].real()/size*window[i+hop];}
        position=0;
    }
};
albert::Sound::Sound(bool device):impl_(std::make_unique<Impl>()){
    const char* off=std::getenv("ALBERT_AUDIO");if(!device||(off&&std::strcmp(off,"0")==0))return;
    ma_device_config c=ma_device_config_init(ma_device_type_playback);c.playback.format=ma_format_f32;c.playback.channels=2;c.sampleRate=rate;
    c.pUserData=this;c.dataCallback=[](ma_device* d,void* out,const void*,ma_uint32 frames){static_cast<Sound*>(d->pUserData)->render(static_cast<float*>(out),frames,2);};
    if(ma_device_init(nullptr,&c,&impl_->device)!=MA_SUCCESS)return;
    if(ma_device_start(&impl_->device)!=MA_SUCCESS){ma_device_uninit(&impl_->device);return;}
    impl_->open=true;
}
albert::Sound::~Sound(){if(impl_->open)ma_device_uninit(&impl_->device);}
bool albert::Sound::playing()const{return impl_->open;}
void albert::Sound::setVolume(float v){impl_->volume.store(std::clamp(v,0.f,1.f),std::memory_order_relaxed);}
double albert::Sound::adaptation()const{return impl_->adaptation;}
void albert::Sound::hear(const ac::Spectrum& levels,bool audible,float pan,double seconds){
    auto& d=*impl_;double sum=0;
    if(audible)for(int b=first;b<ac::bands;++b)sum+=std::pow(10.,levels[static_cast<std::size_t>(b)]/10);
    const double overall=sum>0?10*std::log10(sum):0,want=std::max(overall-95,0.);
    d.adaptation+=(want-d.adaptation)*(want>d.adaptation?-std::expm1(-seconds/1.5):-std::expm1(-seconds/4)); // Down fast, back slowly.
    for(int b=0;b<count;++b){const double l=levels[static_cast<std::size_t>(first+b)]; // 95 dB SPL (after adaptation) at -14 dBFS.
        d.powers[static_cast<std::size_t>(b)].store(audible&&l>-200?static_cast<float>(std::pow(10.,(l-d.adaptation-109)/10)):0.f,std::memory_order_relaxed);}
    d.panTarget.store(std::clamp(pan,-1.f,1.f),std::memory_order_relaxed);
}
void albert::Sound::render(float* out,unsigned frames,unsigned channels){
    auto& d=*impl_;constexpr float smooth=1.f/(.03f*rate);const float panTo=d.panTarget.load(std::memory_order_relaxed),volume=d.volume.load(std::memory_order_relaxed);
    for(unsigned i=0;i<frames;++i){
        if(d.position>=hop)d.frame();
        const float y=std::tanh(static_cast<float>(d.ready[d.position++])*volume); // Soft limit.
        d.pan+=(panTo-d.pan)*smooth;const float a=(d.pan+1)*static_cast<float>(pi/4),left=std::cos(a)*1.4142135f,right=std::sin(a)*1.4142135f; // Equal power, unity in the centre.
        for(unsigned c=0;c<channels;++c)out[i*channels+c]=channels==1?y:c==0?y*left:c==1?y*right:0.f;
    }
}
