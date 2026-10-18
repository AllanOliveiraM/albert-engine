#include "providers/gfs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace albert;
namespace {
constexpr double deg=3.14159265358979323846/180;
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
void near(double a,double b,double tol,const char* m){if(!(std::abs(a-b)<=tol)){std::fprintf(stderr,"%s: %.9g vs %.9g (tolerance %.3g)\n",m,a,b,tol);throw std::runtime_error(m);}}
// University of Wyoming, Tampa Bay (72210) 2026-10-17 12 UTC, mandatory levels:
// pressure (hPa), geopotential height (m), temperature (°C), wind from (deg), speed (m/s).
struct Level {double pressure,height,temperature,direction,speed;};
constexpr Level sonde[]{{925,815,21.1,356,1.2},{850,1544,17.3,261,3.2},{500,5886,-6.4,247,11.5},{400,7603,-15.9,243,11.2},{300,9703,-31.6,235,12.9},
    {250,10964,-42.3,228,13.6},{200,12430,-54.9,236,18},{150,14221,-64.6,270,16.3},{100,16631,-75,272,8.2},{70,18718,-70.5,216,4.1},{50,20764,-61.6,179,8.2},
    {30,23990,-56.5,26,3.3},{20,26616,-51.4,185,4.2},{10,31164,-47.3,274,7.3}};
}
int main(int argc,char** argv){try{
    if(argc<3)throw std::runtime_error("usage: weather_test fixture.grb2 cache-directory");
    std::ifstream in{argv[1],std::ios::binary};const std::string bytes{std::istreambuf_iterator<char>(in),{}};
    // GFS 0.25° analysis 2026-10-17 12 UTC from the NOMADS filter, 26.75-28.75 N, 83.5-81.25 W.
    const auto fields=providers::decodeGrib2(bytes);require(fields.size()==205,"GRIB2 field count");
    struct Ref {int category,number;double level,node44,node00;}; // Independent Python decode.
    for(const Ref& r:{Ref{0,0,50000,267.037266,267.267266},Ref{3,5,50000,5877.695625,5881.975625},Ref{2,2,25000,13.644482,12.944482},Ref{1,0,85000,.011601,.011728},Ref{0,0,1,178.577227,178.177227}}){
        const auto f=std::find_if(fields.begin(),fields.end(),[&](const auto& x){return x.surface==100&&x.category==r.category&&x.number==r.number&&x.level==r.level;});
        require(f!=fields.end()&&f->ni==10&&f->nj==9,"GRIB2 field missing");near(f->values[44],r.node44,std::max(std::abs(r.node44)*1e-6,6e-7),"GRIB2 value");near(f->values[0],r.node00,std::max(std::abs(r.node00)*1e-6,6e-7),"GRIB2 value"); // Printed to 6 decimals.
        near(f->la1,26.75,1e-9,"Grid latitude");near(f->lo1,276.5,1e-9,"Grid longitude");require(f->reference==1792238400.&&f->forecastMinutes==0,"Reference time");
    }
    bool rejected=false;try{providers::decodeGrib2("<html>error</html>");}catch(const std::runtime_error&){rejected=true;}require(rejected,"Error page accepted as GRIB2");
    const physics::Weather w(providers::gfsGrid({fields},"fixture"));const auto& g=w.grid();
    require(g.rows==9&&g.columns==10&&g.pressure.size()==41&&g.pressure.front()==100000&&g.pressure.back()==1&&std::abs(g.west+83.5)<1e-9&&std::abs(g.south-26.75)<1e-9,"Grid layout");
    // Against the radiosonde at its own heights (geopotential to ellipsoidal height).
    const double lat=27.705*deg,lon=-82.401*deg,s2=std::sin(lat)*std::sin(lat),gamma=9.7803253359*(1+.00193185265241*s2)/std::sqrt(1-physics::earth.e2()*s2);
    const double n=physics::geoidHeight(lat,lon),t=static_cast<double>(physics::ttFromUtc(1792238400.));
    std::printf("geoid at Tampa %.2f m\n",n);near(n,-27,4,"Geoid at Tampa");
    std::vector<double> dt;double windSquares=0,worstPressure=0;
    for(const Level& l:sonde){
        const double h=l.height*9.80665,z=6371008.8*h/(gamma*6371008.8-h);const physics::Air a=w.at(static_cast<std::int64_t>(t),lat,lon,z+n);
        const double u=-l.speed*std::sin(l.direction*deg),v=-l.speed*std::cos(l.direction*deg),dT=a.temperature-(l.temperature+273.15),dp=a.pressure/(l.pressure*100)-1;
        std::printf("%5.0f hPa %6.0f m: GFS %7.2f K %8.2f hPa wind %5.1f %5.1f | sonde %7.2f K wind %5.1f %5.1f\n",l.pressure,l.height,a.temperature,a.pressure/100,a.wind.y,a.wind.x,l.temperature+273.15,u,v);
        dt.push_back(std::abs(dT));windSquares+=(a.wind.y-u)*(a.wind.y-u)+(a.wind.x-v)*(a.wind.x-v);worstPressure=std::max(worstPressure,std::abs(dp));
    }
    std::sort(dt.begin(),dt.end());const double median=dt[dt.size()/2],rms=std::sqrt(windSquares/static_cast<double>(dt.size()));
    std::printf("GFS analysis vs sonde: median |dT| %.2f K, max %.2f K, wind RMS %.2f m/s, max |dp/p| %.4f\n",median,dt.back(),rms,worstPressure);
    require(median<1.5&&dt.back()<4&&rms<4&&worstPressure<.01,"GFS analysis against the radiosonde");
    // Above the grid, outside it and between valid times.
    near(w.at(static_cast<std::int64_t>(t),lat,lon,100e3).density,0,0,"Vacuum above the weather and 86 km");
    near(w.at(static_cast<std::int64_t>(t),40*deg,lon,1000).temperature,physics::standardAtmosphere(1000).temperature,1e-9,"Standard atmosphere outside the grid");
    auto later=providers::gfsGrid({fields},"fixture");later.times.push_back(later.times[0]);later.times[1].valid+=3600*physics::second;for(auto& x:later.times[1].temperature)x+=10;
    const physics::Weather pair(std::move(later));
    near(pair.at(static_cast<std::int64_t>(t)+1800*physics::second,lat,lon,5000).temperature-pair.at(static_cast<std::int64_t>(t),lat,lon,5000).temperature,5,1e-4,"Linear in time");
    // Provider: cycle through the index, subregion download, cache reuse.
    const std::filesystem::path cache=argv[2];std::filesystem::remove_all(cache);int index=0,downloads=0;
    std::string hourOne=bytes; // Same fields relabelled as the +1 h forecast (section 4, octets 19-22).
    for(std::size_t at=0;at<hourOne.size();){std::size_t total=0;for(int i=0;i<8;++i)total=total<<8|static_cast<unsigned char>(hourOne[at+8+static_cast<std::size_t>(i)]);
        for(std::size_t sct=at+16;sct+5<at+total-4;){std::size_t length=0;for(int i=0;i<4;++i)length=length<<8|static_cast<unsigned char>(hourOne[sct+static_cast<std::size_t>(i)]);
            if(hourOne[sct+4]==4){hourOne[sct+18]=hourOne[sct+19]=hourOne[sct+20]=0;hourOne[sct+21]=1;}sct+=length;}
        at+=total;}
    providers::Gfs gfs(cache,[&](const std::string& url,DownloadProgress*){if(url.ends_with(".idx")){++index;if(url.find("/12/")==std::string::npos)throw std::runtime_error("404");return std::string("1:0:d=2026101712:HGT\n");}
        ++downloads;require(url.find("filter_gfs_0p25.pl")!=std::string::npos&&url.find("lev_0.01_mb=on")!=std::string::npos&&url.find("var_SPFH=on")!=std::string::npos&&url.find("toplat=33.75")!=std::string::npos,"Filter request");
        return url.find(".f001")!=std::string::npos?hourOne:bytes;});
    const double now=1792238400.+1800; // 12:30 UTC: cycle 12Z, hours 0 and 1.
    const auto live=gfs.around(27.7,-82.4,now);
    require(live->grid().times.size()==2&&live->grid().times[1].valid-live->grid().times[0].valid==3600*physics::second&&downloads==2,"Two forecast hours");
    require(live->source().find("NOAA GFS 0.25° 2026-10-17 12Z (+0/+1 h)")!=std::string::npos,"Source names the cycle");
    gfs.around(27.7,-82.4,now);require(downloads==2,"Cached hours reused");
    std::printf("provider: %d index probes, %d downloads\n",index,downloads);
    std::cout<<"weather_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
