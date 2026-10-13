#include "providers/horizons.hpp"
#include "core/astro_time.hpp"
#include "core/time.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
using namespace albert::providers;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class F>void rejects(F fn){bool no=false;try{fn();}catch(const std::exception&){no=true;}require(no,"Invalid provider input accepted");}
const std::string fixture="Header\n$$SOE\n2451545.0, A.D. 2000-Jan-01, 100000000,2,3,4,5,6,\n2451545.1, A.D. 2000-Jan-01, 100000010,4,6,4,5,6,\n$$EOE\n";
int main(int argc,char** argv){try{
    if(argc>1&&std::string(argv[1])=="--network"){
        const double now=albert::time::clock().utcSeconds();
        const auto value=horizons().query({499,"500@399",now-60,now+1800,"5 min"});
        std::cout<<"Horizons real Mars OK: "<<value.points.size()<<" ICRF/SI samples, fetched UTC "<<value.fetchedUtc<<'\n';return 0;
    }
    auto values=parseVectors(fixture);require(values.size()==2&&values[0].positionMeters.x==1e11&&values[0].velocityMetersPerSecond.x==4000.,"Provider did not normalize SI quantities");
    rejects([&]{parseVectors("ambiguous search result");});rejects([&]{parseVectors("$$SOE\n2451545,date,1,2\n$$EOE");});
    auto invalid=fixture;invalid.replace(invalid.find("100000000"),9,"nan");rejects([&]{parseVectors(invalid);});
    invalid=fixture;invalid.replace(invalid.find("2451545.1"),9,"2451545.0");rejects([&]{parseVectors(invalid);});
    invalid=fixture;invalid.replace(invalid.find("100000000"),9,"1000oops");rejects([&]{parseVectors(invalid);});
    Query request{499,"500@399",946728000,946728600,"5 min"};const auto url=horizonsURL(request);
    require(url.find("REF_PLANE=FRAME")!=std::string::npos&&url.find("TIME_TYPE=TDB")!=std::string::npos&&url.find("OUT_UNITS=KM-S")!=std::string::npos,"Reference frame/time/units are implicit");
    auto bad=request;bad.center="500@evil";rejects([&]{horizonsURL(bad);});bad=request;bad.step="1 min&COMMAND=10";rejects([&]{horizonsURL(bad);});bad=request;bad.endUtc=std::numeric_limits<double>::quiet_NaN();rejects([&]{horizonsURL(bad);});
    for(double utc:{946728000.,1483228801.,1791886400.,2200000000.})require(std::abs(albert::astro::tdbToUtc(albert::astro::utcToTdb(utc))-utc)<.0001,"UTC/TAI/TT/TDB round trip lost precision");
    std::atomic_int calls=0,active=0,maximum=0;std::atomic<double> mono=0;
    Horizons provider([&](const std::string&,albert::DownloadProgress*){++calls;const int current=++active;maximum.store(std::max(maximum.load(),current));std::this_thread::sleep_for(std::chrono::milliseconds(2));--active;return fixture;},[]{return 946728000.;},[&]{return mono.load();});
    const auto first=provider.query(request);require(!first.fromCache&&first.fetchedUtc==946728000.,"Source provenance missing");
    require(provider.query(request).fromCache&&calls==1,"Repeated query ignored bounded cache");
    mono=301;require(!provider.query(request).fromCache&&calls==2,"Expired provider cache reused");
    std::jthread one([&]{auto q=request;q.target=299;provider.query(q);});std::jthread two([&]{auto q=request;q.target=599;provider.query(q);});one.join();two.join();require(maximum==1,"Horizons requests ran concurrently");
    albert::DownloadProgress cancelled;cancelled.cancelled=true;rejects([&]{provider.query(request,&cancelled);});
    Horizons incomplete([](const std::string&,albert::DownloadProgress*){return "$$SOE\n2451545.01,date,1,2,3,4,5,6,\n2451545.02,date,1,2,3,4,5,6,\n$$EOE";},[]{return 946728000.;},[]{return 0.;});
    rejects([&]{incomplete.query(request);});
    std::cout<<"Providers OK: ICRF/SI validation, time scales, response coverage, cache expiry, serialized requests and cancellation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
