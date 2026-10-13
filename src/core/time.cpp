#include "time.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#ifdef __linux__
#include <time.h>
#endif

namespace albert::time {
double monotonicSeconds(){
#ifdef __linux__
    timespec ts{};
    if(clock_gettime(CLOCK_BOOTTIME,&ts)==0)return static_cast<double>(ts.tv_sec)+ts.tv_nsec*1e-9;
#endif
    // MSVC/MinGW steady_clock uses QPC, which includes standby/hibernate.
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
const char* qualityName(Quality q){switch(q){case Quality::Local:return "local";case Quality::Synchronized:return "synchronized";case Quality::Holdover:return "holdover";case Quality::Expired:return "expired";}return "local";}
Discipline::Discipline(double localUtc,double monotonic):anchorUtc_(localUtc),anchorMono_(monotonic){}
double Discipline::utcSeconds(double mono)const{
    const double elapsed=std::max(0.,mono-anchorMono_);
    return anchorUtc_+elapsed+std::clamp(correction_,-elapsed*.01,elapsed*.01);
}
Status Discipline::read(double mono)const{
    const double elapsed=std::max(0.,mono-anchorMono_);
    const double applied=std::clamp(correction_,-elapsed*.01,elapsed*.01);
    Status s;s.utcSeconds=anchorUtc_+elapsed+applied;s.source=source_;s.generation=generation_;s.error=error_;
    s.lastSyncUtc=lastSyncUtc_;s.delaySeconds=delay_;s.correctionSeconds=correction_-applied;
    if(external_){s.ageSeconds=std::max(0.,mono-lastSyncMono_);s.uncertaintySeconds=uncertainty_+s.ageSeconds*50e-6+std::abs(s.correctionSeconds);
        s.quality=s.ageSeconds>3600 ? Quality::Expired : (failed_||s.ageSeconds>600 ? Quality::Holdover:Quality::Synchronized);}
    return s;
}
void Discipline::failed(std::string reason){failed_=true;error_=std::move(reason);}
bool Discipline::synchronize(std::span<const Observation> input,double mono){
    // Three independent providers; duplicate names never count twice.
    std::vector<Observation> samples;std::set<std::string> sources;
    for(const auto& v:input){
        if(samples.size()==3)break;
        if(v.source.empty()||!sources.insert(v.source).second||!std::isfinite(v.utcSeconds)||!std::isfinite(v.monotonicSeconds)||
            !std::isfinite(v.uncertaintySeconds)||!std::isfinite(v.delaySeconds)||v.uncertaintySeconds<0||v.uncertaintySeconds>1.5||
            v.delaySeconds<0||v.delaySeconds>1||v.monotonicSeconds>mono||mono-v.monotonicSeconds>30||v.stratum<1||v.stratum>15)continue;
        samples.push_back(v);
    }
    unsigned best=0;double low=0,high=0,delay=0;std::string selected;bool ambiguous=false;
    for(unsigned mask=1;mask<(1u<<samples.size());++mask){
        unsigned count=0;double a=-std::numeric_limits<double>::infinity(),b=-a,rtt=0;std::string names;
        for(unsigned i=0;i<samples.size();++i)if(mask&(1u<<i)){
            const auto& v=samples[i];const double utc=v.utcSeconds+(mono-v.monotonicSeconds),error=v.uncertaintySeconds+(mono-v.monotonicSeconds)*50e-6;
            a=std::max(a,utc-error);b=std::min(b,utc+error);rtt=std::max(rtt,v.delaySeconds);++count;
            if(!names.empty())names+=", ";names+=v.source;
        }
        if(a>b||count<2||count<best)continue;
        if(count>best){best=count;low=a;high=b;delay=rtt;selected=names;ambiguous=false;}
        else if(b<low||a>high)ambiguous=true;
    }
    if(best<2||ambiguous){failed("NTP: no unambiguous agreement between independent sources");return false;}
    const double measured=low+(high-low)*.5;const auto previous=read(mono);
    correction_=measured-previous.utcSeconds;
    if(!external_||std::abs(correction_)>1.){anchorUtc_=measured;correction_=0;++generation_;}else anchorUtc_=previous.utcSeconds;
    anchorMono_=mono;uncertainty_=(high-low)*.5;lastSyncMono_=mono;lastSyncUtc_=measured;delay_=delay;
    source_=selected;external_=true;failed_=false;error_.clear();return true;
}
namespace {
std::uint32_t word(std::span<const unsigned char> p,size_t i){return (std::uint32_t(p[i])<<24)|(std::uint32_t(p[i+1])<<16)|(std::uint32_t(p[i+2])<<8)|p[i+3];}
double timestamp(std::span<const unsigned char> p,size_t i){
    if(word(p,i)==0&&word(p,i+4)==0)throw std::runtime_error("NTP: missing timestamp");
    double seconds=static_cast<double>(word(p,i))-2208988800.;
    // One unambiguous era in [2020,2100); never infer the era from the OS date.
    if(seconds<1577836800.)seconds+=4294967296.;
    if(seconds>=4102444800.)throw std::runtime_error("NTP: timestamp outside supported 2020-2100 era window");
    return seconds+std::ldexp(static_cast<double>(word(p,i+4)),-32);
}
}
Packet request(std::uint64_t nonce){Packet p{};p[0]=0x23;p[2]=8;for(int i=0;i<8;++i)p[40+i]=static_cast<unsigned char>(nonce>>(56-8*i));return p;}
Observation decode(std::span<const unsigned char> p,const Packet& sent,double start,double end,std::string source){
    if(p.size()!=48||!std::isfinite(start)||!std::isfinite(end)||end<start||end-start>1.)throw std::runtime_error("NTP: invalid packet or excessive round trip");
    if((p[0]&7)!=4||((p[0]>>3)&7)<3||((p[0]>>3)&7)>4)throw std::runtime_error("NTP: invalid mode/version");
    if(!std::equal(sent.begin()+40,sent.end(),p.begin()+24))throw std::runtime_error("NTP: originate nonce mismatch");
    if(p[1]==0){
        for(size_t i=12;i<16;++i)if(p[i]<'A'||p[i]>'Z')throw std::runtime_error("NTP: invalid KoD code");
        throw std::runtime_error("NTP KoD: "+std::string(reinterpret_cast<const char*>(p.data()+12),4));
    }
    if(p[1]>15)throw std::runtime_error("NTP: unsynchronized stratum");
    if((p[0]>>6)!=0)throw std::runtime_error("NTP: unsynchronized or leap-second announcement (unsupported window)");
    const double received=timestamp(p,32),transmitted=timestamp(p,40),reference=timestamp(p,16);
    const double process=transmitted-received,roundTrip=end-start;
    if(process<0||process>roundTrip+.002||reference>transmitted+.002||transmitted-reference>86400.)throw std::runtime_error("NTP: inconsistent/stale server timestamps");
    const int precision=static_cast<std::int8_t>(p[3]);if(precision>0||precision< -50)throw std::runtime_error("NTP: invalid precision");
    const double rootDelay=static_cast<std::int32_t>(word(p,4))/65536.,dispersion=word(p,8)/65536.;
    const double network=std::max(0.,roundTrip-process),uncertainty=network*.5+std::max(0.,rootDelay)*.5+dispersion+std::ldexp(1.,precision)+.001;
    if(uncertainty>1.5)throw std::runtime_error("NTP: excessive root distance");
    return {std::move(source),transmitted+network*.5,end,uncertainty,roundTrip,p[1]};
}
}
