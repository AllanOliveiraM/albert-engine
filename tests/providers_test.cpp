#include "providers/horizons.hpp"
#include "providers/small_bodies.hpp"
#include "providers/store.hpp"
#include "net/sha256.hpp"
#include "core/astro_time.hpp"
#include "core/time.hpp"
#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>
#include <filesystem>
#include <fstream>
using namespace albert::providers;
void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class F>void rejects(F fn){bool no=false;try{fn();}catch(const std::exception&){no=true;}require(no,"Invalid provider input accepted");}
const std::string fixture="Header\n$$SOE\n2451545.0, A.D. 2000-Jan-01, 100000000,2,3,4,5,6,\n2451545.1, A.D. 2000-Jan-01, 100000010,4,6,4,5,6,\n$$EOE\n";
void persistentCache(){
    namespace fs=std::filesystem;
    const auto directory=fs::temp_directory_path()/("albert-provider-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{directory};
    double utc=946728000.;int calls=0;
    auto transport=[&](const std::string&,albert::DownloadProgress*){++calls;return fixture;};
    auto now=[&]{return utc;};auto mono=[]{return 0.;};
    Query query{499,"500@399",utc,utc+600,"5 min"};
    {Horizons first(transport,now,mono,directory);require(!first.query(query).fromCache&&calls==1,"First response was not fetched");}
    {Horizons reopened(transport,now,mono,directory);const auto saved=reopened.query(query);require(saved.fromCache&&calls==1&&saved.fetchedUtc==utc,"Restart did not reuse persisted ephemerides");}
    utc+=400;query.bootstrapCache=true;
    {Horizons reopened(transport,now,mono,directory);require(reopened.query(query).fromCache&&calls==1,"Startup rejected still-current cached samples");query.forceRefresh=true;require(!reopened.query(query).fromCache&&calls==2,"Background refresh did not replace disk data");}
    utc+=400;query.bootstrapCache=false;query.forceRefresh=false;
    {Horizons expired(transport,now,mono,directory);require(!expired.query(query).fromCache&&calls==3,"Expired disk response was treated as fresh");}
    query.bootstrapCache=true;query.startUtc=946750000.;query.endUtc=946751000.;utc=query.startUtc;
    {Horizons uncovered(transport,now,mono,directory);rejects([&]{uncovered.query(query);});require(calls==4,"Startup extrapolated an expired position table");}
    for(const auto& entry:fs::directory_iterator(directory))if(entry.path().extension()==".json"){std::ofstream file(entry.path(),std::ios::trunc);file<<"broken";}
    utc=946728000.;query={499,"500@399",utc,utc+600,"5 min"};
    {Horizons corrupted(transport,now,mono,directory);require(!corrupted.query(query).fromCache&&calls==5,"Corrupt disk cache prevented a fresh download");}
    saveResponse(directory,"checksum",{"payload",utc},1024);
    const auto stored=directory/(albert::sha256("checksum")+".json");
    {std::ifstream input(stored);std::string text((std::istreambuf_iterator<char>(input)),{});input.close();text.replace(text.find(":\"payload\"")+2,7,"changed");std::ofstream output(stored);output<<text;}
    require(!loadResponse(directory,"checksum",1024),"Altered cache passed integrity validation");
    saveResponse(directory,"oversized",{std::string(2048,'x'),utc},1024);require(!loadResponse(directory,"oversized",1024),"Cache ignored payload bound");
    auto dayFixture=fixture;dayFixture.replace(dayFixture.find("2451545.1"),9,"2451546.1");
    int dailyCalls=0;double monotonic=0;Horizons daily([&](const std::string&,albert::DownloadProgress*){++dailyCalls;return dayFixture;},now,[&]{return monotonic;},directory/"day");
    auto dailyQuery=query;dailyQuery.endUtc=utc+86400.;daily.query(dailyQuery);utc+=301;monotonic=301;
    require(!daily.query(dailyQuery).fromCache&&dailyCalls==2,"Day-long position table incorrectly received a day-long refresh interval");
    int searches=0;auto search=[&](const std::string&,albert::DownloadProgress*){++searches;return R"({"signature":{"version":"1.3"},"object":{"des":"433","fullname":"433 Eros","spkid":"20000433"}})";};
    {SmallBodies first(search,directory/"catalog");require(first.resolve("433").spkId==20000433,"Initial SBDB lookup failed");}
    {SmallBodies reopened(search,directory/"catalog");require(reopened.resolve("433").spkId==20000433&&searches==1,"Restart ignored persisted catalog lookup");}
    for(int i=0;i<132;++i)saveResponse(directory/"bounded",std::to_string(i),{"test",utc},1024);
    require(std::distance(fs::directory_iterator(directory/"bounded"),fs::directory_iterator{})<=128,"Provider cache grew beyond file-count bound");
}
int main(int argc,char** argv){try{
    if(argc>1&&std::string(argv[1])=="--network"){
        const double now=albert::time::clock().utcSeconds();
        const auto value=horizons().query({499,"500@399",now-60,now+1800,"5 min"});
        const auto discoveries=SmallBodies{}.search("Apophis");require(discoveries.size()==1&&discoveries[0].radiusMeters>0,"Real SBDB lookup failed");
        const auto asteroid=horizons().query({discoveries[0].spkId,"500@399",now-60,now+1800,"5 min"});require(asteroid.points.size()>2,"SBDB SPK identity failed Horizons lookup");
        std::cout<<"SBDB real Apophis + Horizons OK; Horizons real Mars OK: "<<value.points.size()<<" ICRF/SI samples, fetched UTC "<<value.fetchedUtc<<'\n';return 0;
    }
    const auto small=parseSmallBodies(R"({"signature":{"version":"1.3"},"object":{"des":"433","fullname":"433 Eros","spkid":"20000433"},"phys_par":[{"name":"diameter","value":"16.84","units":"km"}]})");
    require(small.size()==1&&small[0].spkId==20000433&&std::abs(small[0].radiusMeters-8420)<1e-8,"SBDB target/diameter units lost");
    require(smallBodyDesignation(smallBodyCatalogId("2024 YR4"))=="2024 YR4","SBDB designation ID failed round trip");
    rejects([]{smallBodyDesignation("sbdb.zz");});rejects([]{smallBodyCatalogId(std::string(41,'a'));});
    rejects([]{parseSmallBodies(R"({"signature":{"version":"2.0"}})");});
    auto matches=parseSmallBodies(R"({"signature":{"version":"1.3"},"list":[{"pdes":"141P","name":"141P/Machholz 2"},{"pdes":"141P-A","name":"141P-A/Machholz 2"}]})");require(matches.size()==2&&matches[0].spkId==0&&matches[0].radiusMeters==0,"Ambiguous SBDB match invented a target or diameter");
    SmallBodies smallProvider([](const std::string& url,albert::DownloadProgress*){require(url.find("2024%20YR4")!=std::string::npos,"SBDB query not escaped");return R"({"signature":{"version":"1.3"},"object":{"des":"2024 YR4","fullname":"2024 YR4","spkid":"54321"}})";});require(smallProvider.resolve("2024 YR4").radiusMeters==0,"Missing diameter not preserved");
    auto values=parseVectors(fixture);require(values.size()==2&&values[0].positionMeters.x==1e11&&values[0].velocityMetersPerSecond.x==4000.,"Provider did not normalize SI quantities");
    rejects([&]{parseVectors("ambiguous search result");});rejects([&]{parseVectors("$$SOE\n2451545,date,1,2\n$$EOE");});
    auto invalid=fixture;invalid.replace(invalid.find("100000000"),9,"nan");rejects([&]{parseVectors(invalid);});
    invalid=fixture;invalid.replace(invalid.find("2451545.1"),9,"2451545.0");rejects([&]{parseVectors(invalid);});
    invalid=fixture;invalid.replace(invalid.find("100000000"),9,"1000oops");rejects([&]{parseVectors(invalid);});
    Query request{499,"500@399",946728000,946728600,"5 min"};const auto url=horizonsURL(request);
    require(url.find("REF_PLANE=FRAME")!=std::string::npos&&url.find("TIME_TYPE=TDB")!=std::string::npos&&url.find("OUT_UNITS=KM-S")!=std::string::npos,"Reference frame/time/units are implicit");
    auto asteroidRequest=request;asteroidRequest.target=20099942;require(horizonsURL(asteroidRequest).find("DES%3D%2020099942%3B")!=std::string::npos,"SBDB SPK was treated as a Horizons record number");
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
    persistentCache();
    std::cout<<"Providers OK: ICRF/SI validation, time scales, response coverage, persistent bounded cache, refresh, corruption recovery, serialized requests and cancellation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
