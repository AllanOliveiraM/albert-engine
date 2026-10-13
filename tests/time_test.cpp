#include "core/time.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
using namespace albert::time;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}require(rejected,"Malformed NTP response accepted");}
void stamp(Packet& packet,size_t offset,double utc){
    const double ntp=utc+2208988800.;const auto seconds=static_cast<std::uint64_t>(std::floor(ntp));
    const auto fraction=static_cast<std::uint32_t>((ntp-std::floor(ntp))*4294967296.);
    for(unsigned i=0;i<4;++i){packet[offset+i]=static_cast<unsigned char>(seconds>>(24-i*8));packet[offset+4+i]=static_cast<unsigned char>(fraction>>(24-i*8));}
}
Packet response(const Packet& sent,double utc){Packet p{};p[0]=0x24;p[1]=2;p[3]=static_cast<unsigned char>(-20);std::copy(sent.begin()+40,sent.end(),p.begin()+24);stamp(p,16,utc-1);stamp(p,32,utc);stamp(p,40,utc+.005);return p;}
int main(int argc,char** argv){try{
    if(argc>1&&std::string(argv[1])=="--network"){
        Service service;const auto deadline=monotonicSeconds()+12.;Status s;
        do{s=service.status();if(!s.synchronizing)break;std::this_thread::sleep_for(std::chrono::milliseconds(50));}while(monotonicSeconds()<deadline);
        std::cout<<qualityName(s.quality)<<" source="<<s.source<<" UTC="<<std::fixed<<s.utcSeconds<<" uncertainty="<<s.uncertaintySeconds<<" RTT="<<s.delaySeconds<<" error="<<s.error<<'\n';
        return s.quality==Quality::Synchronized?0:1;
    }
    constexpr double utc=1791886400.;const auto sent=request(0x123456789abcdef0ULL);auto packet=response(sent,utc);
    const auto sample=decode(packet,sent,10,10.205,"one");
    require(std::abs(sample.utcSeconds-(utc+.105))<2e-6,"Round trip compensation failed");
    require(sample.uncertaintySeconds>.1&&sample.uncertaintySeconds<.102,"Delay uncertainty missing");
    auto invalid=packet;invalid[24]^=1;rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;invalid[0]=0xe4;rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;invalid[0]=0x64;rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;invalid[1]=0;rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;invalid[1]=16;rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;invalid[0]=0x23;rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;stamp(invalid,16,utc-90000);rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;stamp(invalid,40,utc+1);rejects([&]{decode(invalid,sent,10,10.205,"one");});
    invalid=packet;invalid[8]=255;rejects([&]{decode(invalid,sent,10,10.205,"one");});
    rejects([&]{decode(std::span(packet.data(),47),sent,10,10.205,"one");});
    rejects([&]{decode(packet,sent,10,12,"one");});
    rejects([&]{decode(packet,sent,12,10,"one");});
    for(double date:{utc,2085978498.,2200000000.}){const auto era=response(sent,date);require(std::abs(decode(era,sent,0,.205,"era").utcSeconds-(date+.105))<2e-6,"NTP era rollover depends on OS clock");}
    for(double wrong:{utc-86400.*3650.,utc+86400.*3650.}){
        Discipline clock(wrong,0);require(clock.read(1).quality==Quality::Local&&clock.read(1).uncertaintySeconds<0,"Local time presented as externally verified");
        std::array<Observation,3> sources{{{"one",utc,10,.01,.01,2},{"two",utc+.002,10,.02,.03,1},{"outlier",utc+100,10,.01,.01,2}}};
        require(clock.synchronize(sources,10),"Good sources rejected with incorrect machine clock");
        auto s=clock.read(11);require(s.quality==Quality::Synchronized&&std::abs(s.utcSeconds-(utc+1))<.011&&s.generation==1,"External time not authoritative");
        require(s.source.find("outlier")==std::string::npos&&!s.authenticated,"Outlier accepted or NTP presented as authenticated");
        // Civil time is never supplied again: changing it cannot move this clock.
        require(std::abs(clock.read(301).utcSeconds-clock.read(11).utcSeconds-290)<1e-6,"Monotonic holdover drifted");
        clock.failed("timeout");s=clock.read(301);require(s.quality==Quality::Holdover&&s.uncertaintySeconds>.02,"Network loss not marked degraded");
        require(clock.read(4011).quality==Quality::Expired,"Expired synchronization called fresh");
        sources[0].utcSeconds=utc+4001;sources[1].utcSeconds=utc+4001.002;sources[0].monotonicSeconds=sources[1].monotonicSeconds=4011;
        require(clock.synchronize(sources,4011)&&clock.read(4011).error.empty(),"Reconnect failed");
        const double before=clock.read(4020).utcSeconds;
        sources[0].utcSeconds=before-.2;sources[1].utcSeconds=before-.198;sources[0].monotonicSeconds=sources[1].monotonicSeconds=4020;
        require(clock.synchronize(sources,4020),"Small correction rejected");
        const auto corrected=clock.read(4020);require(corrected.correctionSeconds<-.18,"Negative correction not slewed");
        require(clock.read(4021).utcSeconds>corrected.utcSeconds,"Small correction made time go backwards");
        require(std::abs(clock.read(4050).correctionSeconds)<1e-6,"Slew never converged");
        sources[0].utcSeconds=utc+10000;sources[1].utcSeconds=utc+10000.002;sources[0].monotonicSeconds=sources[1].monotonicSeconds=5000;
        require(clock.synchronize(sources,5000)&&clock.read(5000).generation==2,"Large correction did not invalidate ephemeris generation");
    }
    Discipline clock(utc,0);std::array<Observation,3> bad{{{"same",utc,0,.01,.01,1},{"same",utc,0,.01,.01,1},{"other",utc+1,0,.01,.01,1}}};
    require(!clock.synchronize(bad,0),"Duplicate providers count as a quorum");
    bad={{{"a",utc,0,.04,.01,1},{"b",utc+.07,0,.04,.01,1},{"c",utc+.14,0,.04,.01,1}}};
    require(!clock.synchronize(bad,0),"Ambiguous overlapping pairs accepted");
    require(!clock.synchronize({},0),"Empty/timeout response accepted");
    bad[0].utcSeconds=std::numeric_limits<double>::quiet_NaN();bad[1].monotonicSeconds=100;bad[2].uncertaintySeconds=2;
    require(!clock.synchronize(bad,0),"Invalid observations accepted");
    std::cout<<"Time OK: wrong civil clocks, delay compensation, packet validation, era rollover, independent quorum, outliers, slew, suspension/expiry and reconnect\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
