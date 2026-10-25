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
constexpr int first=16; // Resolve the deep bands separately; FFT bins would merge or drop them.
constexpr int count=ac::bands-first;
constexpr std::size_t size=8192,hop=size/2;
constexpr double pi=3.14159265358979323846;
constexpr double shockRate=800;
const double shockDecay=std::exp(-1/(.4e-3*albert::Sound::rate));
struct Random {std::uint64_t s=0x9e3779b97f4a7c15ull;double next(){s^=s<<13;s^=s>>7;s^=s<<17;return static_cast<double>(s>>11)*(1./9007199254740992.);}}; // [0, 1).
void inverse(std::vector<std::complex<double>>& a){ // In-place radix-2 inverse FFT, unscaled.
    const std::size_t n=a.size();
    for(std::size_t i=1,j=0;i<n;++i){std::size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(std::size_t len=2;len<=n;len<<=1){const std::complex<double> w=std::polar(1.,2*pi/static_cast<double>(len));
        for(std::size_t i=0;i<n;i+=len){std::complex<double> z=1;for(std::size_t k=0;k<len/2;++k){const auto u=a[i+k],v=a[i+k+len/2]*z;a[i+k]=u+v;a[i+k+len/2]=u-v;z*=w;}}}
}
struct Synth {
    static constexpr unsigned rate=albert::Sound::rate;
    std::atomic<std::uint64_t> key{0};std::uint64_t seen=0;
    std::array<std::atomic<float>,ac::bands> powers{};std::atomic<float> panTarget{0}; // Band powers (digital units²) for the next hop.
    std::array<int,size/2> band{};std::array<int,count> bins{};std::array<double,size> window{};
    std::vector<std::complex<double>> spectrum=std::vector<std::complex<double>>(size);std::vector<double> overlap=std::vector<double>(hop),ready=std::vector<double>(hop);
    struct Low {double b=0,a1=0,a2=0,z1=0,z2=0,gain=0,target=0;};std::array<Low,first> low{};
    std::size_t position=hop;float pan=0;Random random,lowRandom{0xd1b54a32d192ed03ull},shockRandom{0x94d049bb133111ebull};
    std::atomic<float> square{0}; // The crackle's mean squared shock amplitude (digital units²; 0: none).
    std::atomic<float> shockPole{0},shockBlend{0};std::atomic<unsigned> starts{0};unsigned started=0;
    double shock=0,filtered=0,filtered2=0,still=0,passed=0,burst=1,lastShockPower=0;
    Synth(){
        for(auto& p:powers)p=0;
        const double q=1/(std::pow(2.,1./6)-std::pow(2.,-1./6));
        for(int b=0;b<first;++b){auto& l=low[static_cast<std::size_t>(b)];const double w=2*pi*ac::bandCentre(b)/rate,a=std::sin(w)/(2*q);
            l.b=std::sqrt(a/(1+a));l.a1=-2*std::cos(w)/(1+a);l.a2=(1-a)/(1+a);} // Unit noise power, including sub-bin infrasound.
        // Each bin to the band holding its frequency; a band narrower than a bin takes the nearest one.
        for(std::size_t k=0;k<size/2;++k){band[k]=-1;const double f=static_cast<double>(k)*rate/size;
            for(int b=0;b<count;++b){const double c=ac::bandCentre(first+b);if(f>=c*std::pow(2.,-1./6)&&f<c*std::pow(2.,1./6)){band[k]=b;++bins[static_cast<std::size_t>(b)];break;}}}
        for(int b=0;b<count;++b)if(bins[static_cast<std::size_t>(b)]==0){const auto k=static_cast<std::size_t>(std::lround(ac::bandCentre(first+b)*size/rate));if(k>0&&k<size/2&&band[k]<0){band[k]=b;bins[static_cast<std::size_t>(b)]=1;}}
        for(std::size_t i=0;i<size;++i)window[i]=std::sin(pi*(static_cast<double>(i)+.5)/size);
    }
    void frame(){ // The next hop: this frame's first half over the last one's second.
        std::array<float,count> p;for(int b=0;b<count;++b)p[static_cast<std::size_t>(b)]=powers[static_cast<std::size_t>(first+b)].load(std::memory_order_relaxed);
        for(int b=0;b<first;++b)low[static_cast<std::size_t>(b)].target=std::sqrt(powers[static_cast<std::size_t>(b)].load(std::memory_order_relaxed));
        burst+=.16*(.25+1.5*shockRandom.next()-burst); // Slow clustering keeps the jet from sounding like steady static.
        std::fill(spectrum.begin(),spectrum.end(),std::complex<double>{});
        for(std::size_t k=1;k<size/2;++k){const int b=band[k];if(b<0)continue;
            const double a=static_cast<double>(size)*std::sqrt(p[static_cast<std::size_t>(b)]/(2.*bins[static_cast<std::size_t>(b)]));
            spectrum[k]=std::polar(a,2*pi*random.next());spectrum[size-k]=std::conj(spectrum[k]);}
        inverse(spectrum);
        for(std::size_t i=0;i<hop;++i){ready[i]=overlap[i]+spectrum[i].real()/size*window[i];overlap[i]=spectrum[i+hop].real()/size*window[i+hop];}
        position=0;
    }
    void hear(const ac::Spectrum& levels,bool audible,float side,double adaptation,float crackle);
    void reset(){
        std::fill(overlap.begin(),overlap.end(),0);std::fill(ready.begin(),ready.end(),0);position=hop;
        for(auto& l:low){l.z1=l.z2=l.gain=l.target=0;}shock=filtered=filtered2=still=passed=0;
        started=starts.load(std::memory_order_acquire)-(square.load(std::memory_order_relaxed)>0?1u:0u);pan=panTarget.load(std::memory_order_relaxed);
    }
};
}
struct albert::Sound::Impl {
    std::array<Synth,voices> synth;std::array<std::uint64_t,voices> identities{};double adaptation=0; // Fixed FFT buffers bound callback work without allocation.
    ma_device device{};bool open=false;std::atomic<float> volume{1};
    const double rmsStep=-std::expm1(-1/(.03*rate)),compressAttack=-std::expm1(-1/(.015*rate)),compressRelease=-std::expm1(-1/(.25*rate)),limitRelease=-std::expm1(-1/(.08*rate));
    double rmsPower=0,compressGain=1,limitGain=1;
    struct Boom {float amplitude=0,length=0,pan=0;std::size_t age=0;std::uint64_t voice=0;};std::array<Boom,8> pending{},booms{};std::atomic<unsigned> written{0},read{0};
    Impl(){for(unsigned i=1;i<voices;++i){auto& v=synth[i];v.random.s^=0x632be59bd9b4e019ull*i;v.lowRandom.s^=0xa0761d6478bd642full*i;v.shockRandom.s^=0xe7037ed1a0b428dbull*i;}} // Independent phases keep coincident bands from cancelling.
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
void albert::Sound::boom(double overpressure,double duration,float pan,std::uint64_t voice){
    auto& d=*impl_;const unsigned n=d.written.load(std::memory_order_relaxed);if(n-d.read.load(std::memory_order_acquire)>=d.pending.size())return; // The ring full: dropped.
    d.pending[n%d.pending.size()]={static_cast<float>(overpressure*.1774*std::pow(10.,-d.adaptation/20)),static_cast<float>(std::clamp(duration,.01,2.)*rate),std::clamp(pan,-1.f,1.f),0,voice};
    d.written.store(n+1,std::memory_order_release); // 95 dB SPL (1.12 Pa rms) at -14 dBFS: 0.1774 a pascal.
}
void albert::Sound::hear(const ac::Spectrum& levels,bool audible,float pan,double seconds,float crackle){
    std::array<Voice,voices> sources{};sources[0]={1,levels,audible,pan,crackle};hear(sources,seconds);
}
void albert::Sound::hear(const std::array<Voice,voices>& sources,double seconds){
    auto& d=*impl_;double sum=0;for(const auto& v:sources)if(v.id&&v.audible)for(double l:v.levels)sum+=std::pow(10.,l/10);
    const double overall=sum>0?10*std::log10(sum):0,want=std::max(overall-95,0.);
    d.adaptation+=(want-d.adaptation)*(want>d.adaptation?-std::expm1(-seconds/1.5):-std::expm1(-seconds/4)); // Down fast, back slowly.
    std::array<const Voice*,voices> slots{};std::array<bool,voices> placed{};
    for(unsigned i=0;i<voices;++i)if(sources[i].id)for(unsigned k=0;k<voices;++k)if(d.identities[k]==sources[i].id&&!slots[k]){slots[k]=&sources[i];placed[i]=true;break;}
    for(unsigned i=0;i<voices;++i)if(sources[i].id&&!placed[i])for(auto& slot:slots)if(!slot){slot=&sources[i];break;}
    const Voice silent{};for(unsigned k=0;k<voices;++k){const auto& v=slots[k]?*slots[k]:silent;auto& s=d.synth[k];
        if(v.id!=d.identities[k])s.lastShockPower=0;
        s.hear(v.levels,v.audible,v.pan,d.adaptation,v.crackle);d.identities[k]=v.id;s.key.store(v.id,std::memory_order_release);}
}
void Synth::hear(const ac::Spectrum& levels,bool audible,float pan,double adaptation,float crackle){
    auto& d=*this;
    const float share=std::clamp(crackle,0.f,1.f);double shockPower=0,frequencyPower=0;
    for(int b=0;b<ac::bands;++b){const auto k=static_cast<std::size_t>(b);const double l=levels[k],f=ac::bandCentre(b),p=audible&&l>-200?std::pow(10.,(l-adaptation-109)/10):0.;
        const double take=p*share*std::clamp((f-40)/120,0.,1.); // Leave the deepest thrust roar intact; shocks share the rest's power.
        shockPower+=take;frequencyPower+=take*f*f;d.powers[k].store(static_cast<float>(p-take),std::memory_order_relaxed);}
    const double cutoff=shockPower>0?std::clamp(4*std::sqrt(frequencyPower/shockPower),80.,16000.):16000.;
    const double a=std::exp(-2*pi*cutoff/rate),q=shockDecay,b=1-a,mix=std::clamp((2000-cutoff)/1500,0.,1.);
    // Analytic pulse energies keep the borrowed power after causal filtering; a second pole removes distant fizz.
    const double vs=1/(1-q*q),sl=b*vs/(1-q*a),st=b*sl/(1-q*a);
    const double e1=b*b*vs*(1+q*a)/((1-a*a)*(1-q*a)),lt=(a*b*q*st+b*e1)/(1-a*a);
    const double e2=(b*b*e1+2*a*a*b*lt+2*a*b*b*q*st)/(1-a*a),energy=(1-mix)*(1-mix)*e1+mix*mix*e2+2*mix*(1-mix)*lt;
    d.shockPole.store(static_cast<float>(a),std::memory_order_relaxed);d.shockBlend.store(static_cast<float>(mix),std::memory_order_relaxed);
    d.square.store(static_cast<float>(shockPower*rate/(shockRate*energy)),std::memory_order_relaxed);
    if(shockPower>0&&d.lastShockPower<=0)d.starts.fetch_add(1,std::memory_order_release); // Only an arrived onset can start a shock.
    d.lastShockPower=shockPower;
    d.panTarget.store(std::clamp(pan,-1.f,1.f),std::memory_order_relaxed);
}
void albert::Sound::render(float* out,unsigned frames,unsigned channels){
    auto& d=*impl_;constexpr float smooth=1.f/(.03f*rate);const float volume=d.volume.load(std::memory_order_relaxed);
    struct Block {bool active=false;float pan=0,square=0,scale=0;double pole=0,blend=0;};std::array<Block,voices> block{};
    for(unsigned k=0;k<voices;++k){auto& s=d.synth[k];const auto key=s.key.load(std::memory_order_acquire);
        if(key!=s.seen){s.reset();s.seen=key;}if(!key)continue;auto& b=block[k];b.active=true;
        b.pan=s.panTarget.load(std::memory_order_relaxed);b.square=s.square.load(std::memory_order_relaxed);b.scale=std::sqrt(b.square/2.8f); // Capped Pareto (α 3): E[A²] = 2.8 scale².
        b.pole=s.shockPole.load(std::memory_order_relaxed);b.blend=s.shockBlend.load(std::memory_order_relaxed);
        const unsigned starts=s.starts.load(std::memory_order_acquire);if(starts!=s.started){s.shock+=3*b.scale;s.started=starts;}}
    for(unsigned k=d.read.load(std::memory_order_relaxed);k!=d.written.load(std::memory_order_acquire);d.read.store(++k,std::memory_order_release)) // New booms into a free place.
        for(auto& b:d.booms)if(b.length<=0){b=d.pending[k%d.pending.size()];break;}
    auto gains=[](float pan,float& l,float& r){const float a=(pan+1)*static_cast<float>(pi/4);l=std::cos(a)*1.4142135f;r=std::sin(a)*1.4142135f;}; // Equal power, unity in the centre.
    for(unsigned i=0;i<frames;++i){
        double mixLeft=0,mixRight=0,mixMono=0;
        for(unsigned k=0;k<voices;++k){if(!block[k].active)continue;auto& s=d.synth[k];const auto& b=block[k];
        if(s.position>=hop)s.frame();
        float x=static_cast<float>(s.ready[s.position++]);
        for(auto& l:s.low){const double input=l.b*(s.lowRandom.next()-.5)*3.4641016151377546,y=input+l.z1;
            l.z1=l.z2-l.a1*y;l.z2=-input-l.a2*y;l.gain+=(l.target-l.gain)/(rate*.01);x+=static_cast<float>(y*l.gain);}
        s.shock*=shockDecay;
        if(b.square>0&&s.shockRandom.next()<shockRate*s.burst/rate)s.shock+=b.scale*std::pow(std::max(s.shockRandom.next(),1e-3),-1./3);
        s.filtered=b.pole*s.filtered+(1-b.pole)*s.shock;s.filtered2=b.pole*s.filtered2+(1-b.pole)*s.filtered;
        const double shocks=(1-b.blend)*s.filtered+b.blend*s.filtered2;
        const double blocked=shocks-s.passed+.9974*s.still;s.passed=shocks;s.still=blocked;x+=static_cast<float>(blocked); // Remove shock bias, not the separate infrasound.
        s.pan+=(b.pan-s.pan)*smooth;float left,right;gains(s.pan,left,right);mixLeft+=static_cast<double>(x)*left;mixRight+=static_cast<double>(x)*right;mixMono+=x;}
        float boomLeft=0,boomRight=0;
        for(auto& b:d.booms)if(b.length>0){ // The N: up in 2 ms, down through zero to its negative peak, back up in 2 ms.
            const float t=static_cast<float>(b.age++),rise=.002f*rate,v=t<rise?b.amplitude*(t/rise)*(1-2*t/b.length):t<=b.length?b.amplitude*(1-2*t/b.length):b.amplitude*-(1-(t-b.length)/rise);
            if(b.voice)for(const auto& s:d.synth)if(s.seen==b.voice){b.pan=s.pan;break;}
            float l,r;gains(b.pan,l,r);boomLeft+=v*l;boomRight+=v*r;if(t>=b.length+rise)b.length=0;}
        const double l=(mixLeft+boomLeft)*volume,r=(mixRight+boomRight)*volume,m=(mixMono+.5*(boomLeft+boomRight))*volume;
        d.rmsPower+=((channels==1?m*m:.5*(l*l+r*r))-d.rmsPower)*d.rmsStep; // Average power keeps panning from driving the compressor.
        const double over=10*std::log10(std::max(d.rmsPower,1e-20))+18;
        const double reduction=over<=-3?0:over>=3?.5*over:(over+3)*(over+3)/24; // A 6 dB knee eases into 2:1 compression.
        const double want=std::pow(10.,(1-reduction)/20); // 1 dB make-up preserves the usual roar level.
        d.compressGain+=(want-d.compressGain)*(want<d.compressGain?d.compressAttack:d.compressRelease);
        const double peak=(channels==1?std::abs(m):std::max(std::abs(l),std::abs(r)))*d.compressGain;
        const double limit=peak>.891?.891/peak:1.;
        d.limitGain=limit<d.limitGain?limit:d.limitGain+(limit-d.limitGain)*d.limitRelease; // Catch N-wave peaks immediately; recover gently.
        const double gain=d.compressGain*d.limitGain;
        const float outLeft=std::tanh(l*gain),outRight=std::tanh(r*gain),outMono=std::tanh(m*gain); // Last-resort soft clip after the linked limiter.
        for(unsigned c=0;c<channels;++c)out[i*channels+c]=channels==1?outMono:c==0?outLeft:c==1?outRight:0.f;
    }
}
