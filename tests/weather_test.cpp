#include "providers/gfs.hpp"
#include "providers/space_weather.hpp"
#include "physics/hwm14.hpp"
#include <algorithm>
#include <array>
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
// And 2026-10-18 00 UTC, against the GFS 18 UTC +6 h forecast (not assimilated).
constexpr Level evening[]{{925,820,19.1,38,7.7},{850,1546,15.6,357,4.7},{700,3175,7.4,286,7.2},{500,5872,-5.2,298,12.4},{400,7582,-17.4,264,12.2},
    {300,9682,-31.4,249,19.1},{250,10946,-41.4,231,18.9},{200,12415,-54.2,242,18.6},{150,14215,-64.6,276,16.6},{100,16621,-72.1,291,15.3}};
constexpr Level sonde[]{{925,815,21.1,356,1.2},{850,1544,17.3,261,3.2},{500,5886,-6.4,247,11.5},{400,7603,-15.9,243,11.2},{300,9703,-31.6,235,12.9},
    {250,10964,-42.3,228,13.6},{200,12430,-54.9,236,18},{150,14221,-64.6,270,16.3},{100,16631,-75,272,8.2},{70,18718,-70.5,216,4.1},{50,20764,-61.6,179,8.2},
    {30,23990,-56.5,26,3.3},{20,26616,-51.4,185,4.2},{10,31164,-47.3,274,7.3}};
}
int main(int argc,char** argv){try{
    if(argc<4)throw std::runtime_error("usage: weather_test analysis.grb2 cache-directory forecast.grb2");
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
    { // A forged header asking for 65535² constant points is refused before any allocation.
        std::size_t total=0;for(int i=0;i<8;++i)total=total<<8|static_cast<unsigned char>(bytes[8+static_cast<std::size_t>(i)]);std::string forged=bytes.substr(0,total);
        for(std::size_t sct=16;sct+5<total-4;){std::size_t length=0;for(int i=0;i<4;++i)length=length<<8|static_cast<unsigned char>(forged[sct+static_cast<std::size_t>(i)]);
            if(forged[sct+4]==3)for(std::size_t o:{sct+30,sct+34}){forged[o]=forged[o+1]=0;forged[o+2]=forged[o+3]=static_cast<char>(0xff);}
            if(forged[sct+4]==5){const std::uint64_t n=65535ull*65535ull;for(int i=0;i<4;++i)forged[sct+5+static_cast<std::size_t>(i)]=static_cast<char>(n>>(24-8*i));forged[sct+19]=0;}
            sct+=length;}
        bool refused=false;try{providers::decodeGrib2(forged);}catch(const std::runtime_error&){refused=true;}require(refused,"Oversized GRIB2 grid accepted");
    }
    // A low-cloud average (template 4.8), made from a real grid/packing header with a constant 50% field.
    auto cloudMessage=[&](int endHour){
        std::size_t total=0;for(int i=0;i<8;++i)total=total<<8|static_cast<unsigned char>(bytes[8+static_cast<std::size_t>(i)]);
        std::string message=bytes.substr(0,total);
        auto put=[&](std::size_t at,std::uint64_t value,int n){for(int i=0;i<n;++i)message[at+static_cast<std::size_t>(i)]=static_cast<char>(value>>(8*(n-1-i)));};
        for(std::size_t section=16;section+5<message.size()-4;){std::size_t length=0;for(int i=0;i<4;++i)length=length<<8|static_cast<unsigned char>(message[section+static_cast<std::size_t>(i)]);
            if(message[section+4]==4){
                message.replace(section,length,std::string(58,0));length=58;put(section,length,4);put(section+4,4,1);put(section+7,8,2);
                put(section+9,6,1);put(section+10,1,1);put(section+17,1,1);put(section+22,214,1);
                put(section+34,2026,2);put(section+36,10,1);put(section+37,17,1);put(section+38,endHour,1);put(section+41,1,1);
            }
            if(message[section+4]==5){put(section+11,0x42480000,4);put(section+15,0,2);put(section+17,0,2);put(section+19,0,1);} // 50.0f, zero packed bits.
            section+=length;
        }
        put(8,message.size(),8);return message;
    };
    const auto cloudFields=providers::decodeGrib2(cloudMessage(13));
    require(cloudFields.size()==1&&cloudFields[0].surface==214&&cloudFields[0].forecastMinutes==60,"Cloud average interval end or field decoding");
    near(cloudFields[0].values[0],50,1e-9,"Cloud packing");
    const physics::Weather w(providers::gfsGrid({fields},"fixture"));const auto& g=w.grid();
    require(g.rows==9&&g.columns==10&&g.pressure.size()==41&&g.pressure.front()==100000&&g.pressure.back()==1&&std::abs(g.west+83.5)<1e-9&&std::abs(g.south-26.75)<1e-9,"Grid layout");
    // Against the radiosonde at its own heights (geopotential to ellipsoidal height).
    const double lat=27.705*deg,lon=-82.401*deg,s2=std::sin(lat)*std::sin(lat),gamma=9.7803253359*(1+.00193185265241*s2)/std::sqrt(1-physics::earth.e2()*s2);
    const double n=physics::geoidHeight(lat,lon),t=static_cast<double>(physics::ttFromUtc(1792238400.));
    std::printf("geoid at Tampa %.2f m\n",n);near(n,-27,4,"Geoid at Tampa");
    auto compare=[&](const physics::Weather& model,const Level* levels,std::size_t count,double utc,const char* label){
        std::vector<double> dt;double windSquares=0,worstPressure=0,bias=0;const auto tt=physics::ttFromUtc(utc);
        for(std::size_t i=0;i<count;++i){const Level& l=levels[i];
            const double h=l.height*9.80665,z=6371008.8*h/(gamma*6371008.8-h);const physics::Air a=model.at(tt,lat,lon,z+n);
            const double u=-l.speed*std::sin(l.direction*deg),v=-l.speed*std::cos(l.direction*deg),dT=a.temperature-(l.temperature+273.15),dp=a.pressure/(l.pressure*100)-1;
            std::printf("%5.0f hPa %6.0f m: GFS %7.2f K %8.2f hPa wind %5.1f %5.1f | sonde %7.2f K wind %5.1f %5.1f\n",l.pressure,l.height,a.temperature,a.pressure/100,a.wind.y,a.wind.x,l.temperature+273.15,u,v);
            dt.push_back(std::abs(dT));bias+=dT/static_cast<double>(count);windSquares+=(a.wind.y-u)*(a.wind.y-u)+(a.wind.x-v)*(a.wind.x-v);worstPressure=std::max(worstPressure,std::abs(dp));
        }
        std::sort(dt.begin(),dt.end());const double median=dt[dt.size()/2],rms=std::sqrt(windSquares/static_cast<double>(count));
        std::printf("%s vs sonde: bias %.2f K, median |dT| %.2f K, max %.2f K, wind RMS %.2f m/s, max |dp/p| %.4f\n",label,bias,median,dt.back(),rms,worstPressure);
        return std::array{median,dt.back(),rms,worstPressure};
    };
    const auto analysis=compare(w,sonde,std::size(sonde),1792238400.,"GFS analysis");
    require(analysis[0]<1.5&&analysis[1]<4&&analysis[2]<4&&analysis[3]<.01,"GFS analysis against the radiosonde");
    {   std::ifstream f{argv[3],std::ios::binary};const std::string b{std::istreambuf_iterator<char>(f),{}};
        const physics::Weather forecast(providers::gfsGrid({providers::decodeGrib2(b)},"forecast"));require(forecast.grid().times[0].valid==physics::ttFromUtc(1792281600.),"Forecast valid time");
        const auto r=compare(forecast,evening,std::size(evening),1792281600.,"GFS +6 h forecast");
        require(r[0]<2&&r[1]<5&&r[2]<6&&r[3]<.015,"GFS forecast against the radiosonde");
    }
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
        if(url.find("var_TCDC=on")!=std::string::npos)throw std::runtime_error("Optional cloud field unavailable");
        ++downloads;require(url.find("filter_gfs_0p25.pl")!=std::string::npos&&url.find("lev_0.01_mb=on")!=std::string::npos&&url.find("var_SPFH=on")!=std::string::npos&&url.find("toplat=33.75")!=std::string::npos,"Filter request");
        return url.find(".f001")!=std::string::npos?hourOne:bytes;});
    const double now=1792238400.+1800; // 12:30 UTC: cycle 12Z, hours 0 and 1.
    const auto live=gfs.around(27.7,-82.4,now);
    require(live->grid().times.size()==2&&live->grid().times[1].valid-live->grid().times[0].valid==3600*physics::second&&downloads==2,"Two forecast hours");
    require(live->source().find("NOAA GFS 0.25° 2026-10-17 12Z (+0/+1 h)")!=std::string::npos,"Source names the cycle");
    gfs.around(27.7,-82.4,now);require(downloads==2,"Cached hours reused");
    providers::Gfs offline(cache,[](const std::string&,DownloadProgress*)->std::string{throw std::runtime_error("offline");});
    require(offline.around(27.7,-82.4,now)->grid().times.size()==2,"Complete cache used without network");
    { // A cached file holding only its first message is incomplete: downloaded again.
        std::filesystem::path first;for(const auto& e:std::filesystem::directory_iterator(cache))if(e.path().string().find("-f0-")!=std::string::npos)first=e.path();
        std::size_t total=0;for(int i=0;i<8;++i)total=total<<8|static_cast<unsigned char>(bytes[8+static_cast<std::size_t>(i)]);
        {std::ofstream o{first,std::ios::binary|std::ios::trunc};o.write(bytes.data(),static_cast<std::streamsize>(total));}
        gfs.around(27.7,-82.4,now);require(downloads==3,"Incomplete cache downloaded again");
    }
    {
        providers::Gfs withClouds(cache/"visual-clouds",[&](const std::string& url,DownloadProgress*){
            if(url.ends_with(".idx"))return std::string("index");
            if(url.find("var_TCDC=on")!=std::string::npos)return cloudMessage(url.find(".f001")!=std::string::npos?13:12);
            return url.find(".f001")!=std::string::npos?hourOne:bytes;
        });
        const auto downloaded=withClouds.fetch(27.7,-82.4,now);std::vector<providers::GribField> saved;
        for(const auto& raw:downloaded.grib)for(auto& field:providers::decodeGrib2(raw))saved.push_back(std::move(field));
        near(providers::gfsLowCloudCover(saved,27.7,-82.4,now),.5,1e-9,"Downloaded cloud metadata survives replay bytes");
        const auto clearAir=live->at(static_cast<physics::Tt>(t),lat,lon,5000),cloudAir=downloaded.model->at(static_cast<physics::Tt>(t),lat,lon,5000);
        require(clearAir.temperature==cloudAir.temperature&&clearAir.density==cloudAir.density&&clearAir.humidity==cloudAir.humidity&&clearAir.wind.x==cloudAir.wind.x,"Visual cloud metadata changed the physics atmosphere");
    }
    std::printf("provider: %d index probes, %d downloads\n",index,downloads);
    // CelesTrak SW-Last5Years.csv (2026-10-18 00 UTC download), 2026-10-12 to 22: observed then predicted.
    const std::string csv=
        "DATE,BSRN,ND,KP1,KP2,KP3,KP4,KP5,KP6,KP7,KP8,KP_SUM,AP1,AP2,AP3,AP4,AP5,AP6,AP7,AP8,AP_AVG,CP,C9,ISN,F10.7_OBS,F10.7_ADJ,F10.7_DATA_TYPE,F10.7_OBS_CENTER81,F10.7_OBS_LAST81,F10.7_ADJ_CENTER81,F10.7_ADJ_LAST81\n"
        "2026-10-12,2634,14,17,20,30,10,3,10,20,10,120,6,7,15,4,2,4,7,4,6,0.3,1,89,122.9,122.4,OBS,109.2,114.6,108.8,116.6\n"
        "2026-10-13,2634,15,33,33,20,30,30,20,27,10,203,18,18,7,15,15,7,12,4,12,0.7,3,65,136.0,135.3,OBS,108.8,114.5,108.4,116.4\n"
        "2026-10-14,2634,16,23,33,13,20,17,20,27,27,180,9,18,5,7,6,7,12,12,10,0.5,2,101,123.2,122.6,OBS,108.9,114.2,108.4,116.0\n"
        "2026-10-15,2634,17,20,10,7,33,23,30,27,23,173,7,4,3,18,9,15,12,9,10,0.5,2,72,125.7,125.0,OBS,108.9,113.9,108.4,115.7\n"
        "2026-10-16,2634,18,30,10,13,23,17,33,40,20,187,15,4,5,9,6,18,27,7,11,0.7,3,81,131.2,130.4,OBS,109.0,113.7,108.3,115.4\n"
        "2026-10-17,2634,19,13,7,3,3,7,27,30,10,100,5,3,2,2,3,12,15,4,6,0.3,1,71,131.6,130.7,OBS,109.0,113.6,108.3,115.2\n"
        "2026-10-18,2634,20,13,13,13,13,13,13,13,13,107,5,5,5,5,5,5,5,5,5,0.2,1,80,130.9,130.0,PRD,109.0,113.4,108.3,115.0\n"
        "2026-10-19,2634,21,27,27,27,27,27,27,27,27,216,12,12,12,12,12,12,12,12,12,0.7,3,80,126.0,125.0,PRD,109.0,113.1,108.2,114.6\n"
        "2026-10-20,2634,22,37,37,37,37,37,37,37,37,296,22,22,22,22,22,22,22,22,22,1.1,5,80,126.0,125.0,PRD,108.9,112.9,108.1,114.3\n"
        "2026-10-21,2634,23,30,30,30,30,30,30,30,30,240,15,15,15,15,15,15,15,15,15,0.8,4,80,116.0,115.0,PRD,108.8,112.6,107.9,113.9\n"
        "2026-10-22,2634,24,24,24,24,24,24,24,24,24,192,10,10,10,10,10,10,10,10,10,0.6,3,80,111.0,110.0,PRD,108.8,112.4,107.8,113.7\n";
    const auto days=providers::parseSpaceWeather(csv);require(days.size()==11&&days.back().type=="PRD","Space-weather rows");
    std::string note;const auto sw=providers::spaceWeatherAt(days,1792238400.,&note); // 2026-10-17 12 UTC.
    require(sw&&sw->history,"Space weather at noon");
    near(sw->f107,131.2,1e-9,"Previous-day F10.7");near(sw->f107a,109.0,1e-9,"81-day F10.7");near(sw->ap[0],6,1e-9,"Daily Ap");
    near(sw->ap[1],3,1e-9,"Current 3-hour ap");near(sw->ap[2],2,1e-9,"ap 3 h before");near(sw->ap[3],2,1e-9,"ap 6 h before");near(sw->ap[4],3,1e-9,"ap 9 h before");
    near(sw->ap[5],81./8,1e-9,"Mean ap 12-33 h before");near(sw->ap[6],85./8,1e-9,"Mean ap 36-57 h before");
    require(!providers::spaceWeatherAt(days,1792238400.+30*86400.),"Outside the table");
    const auto predicted=providers::spaceWeatherAt(days,1792238400.+3*86400.);require(predicted&&predicted->history&&predicted->ap[1]==22,"Predicted days carry flat 3-hour values");
    { // Indices follow the flight: the current 3-hour ap goes from 3 to 12 at 15 UTC on 2026-10-17.
        const auto table=std::make_shared<const physics::SpaceWeatherTable>(days);const physics::Msis live(*sw,"live",table),frozen(*sw,"frozen");
        const auto before=physics::ttFromUtc(1792249140.),after=physics::ttFromUtc(1792249260.); // 14:59 and 15:01 UTC.
        near(live.at(before,lat,lon,400e3).density,frozen.at(before,lat,lon,400e3).density,0,"Same 3-hour interval as the fixed indices");
        const double ratio=live.at(after,lat,lon,400e3).density/frozen.at(after,lat,lon,400e3).density;std::printf("ap 3 -> 12 at 15 UTC: density x%.4f at 400 km\n",ratio);
        require(table->at(1792249260.)->ap[1]==12&&ratio>1.002,"Indices follow the instant");
    }
    // NRLMSISE-00 above the weather: the thermosphere instead of vacuum.
    const auto upper=std::make_shared<const physics::Msis>(*sw,"NRLMSISE-00");const auto layered=w.above(upper);
    const auto thermosphere=layered->at(static_cast<std::int64_t>(t),lat,lon,300e3);
    std::printf("300 km: %.0f K, %.3g kg/m3 (%s)\n",thermosphere.temperature,thermosphere.density,layered->source().c_str());
    require(thermosphere.density>5e-12&&thermosphere.density<1e-10&&thermosphere.temperature>700&&thermosphere.temperature<1300,"Thermosphere from NRLMSISE-00");
    near(layered->at(static_cast<std::int64_t>(t),lat,lon,5000).temperature,w.at(static_cast<std::int64_t>(t),lat,lon,5000).temperature,1e-9,"Weather below the blend");
    { // Winds above the weather from HWM14 with the current 3-hour ap (2026-10-17 12 UTC is day 290), the weather's below.
        const auto above=layered->at(static_cast<std::int64_t>(t),lat,lon,100e3).wind;const auto hwm=physics::hwm14(290,43200,100,lat/deg,lon/deg,sw->ap[1]);
        std::printf("100 km: wind %.2f m/s north, %.2f m/s east (HWM14)\n",above.x,above.y);
        near(above.x,hwm.north,1e-12,"HWM14 north wind above the weather");near(above.y,hwm.east,1e-12,"HWM14 east wind above the weather");
        require(std::hypot(above.x,above.y)>1,"Wind above the weather");
        near(layered->at(static_cast<std::int64_t>(t),lat,lon,5000).wind.y,w.at(static_cast<std::int64_t>(t),lat,lon,5000).wind.y,1e-12,"Weather winds below the blend");
    }
    {
        providers::GribField cloud;cloud.category=6;cloud.number=1;cloud.surface=214;cloud.ni=cloud.nj=2;cloud.di=cloud.dj=1;
        cloud.la1=26;cloud.lo1=262;cloud.scan=0;cloud.reference=1000;cloud.values={0,100,100,0};auto later=cloud;later.forecastMinutes=60;later.values={100,100,100,100};
        near(providers::gfsLowCloudCover({cloud,later},25.5,-97.5,2800),.75,1e-9,"Visual cloud cover space/time interpolation");
        require(providers::gfsLowCloudCover({cloud,later},25.5,-97.5,5000)<0&&providers::gfsLowCloudCover({},25.5,-97.5,1000)<0,"Missing or stale cloud fields should be unknown");
        near(providers::gfsLowCloudCover({cloud},26,-98,1000),0,1e-9,"Clear GFS cloud cover was replaced by a default");
    }
    std::cout<<"weather_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
