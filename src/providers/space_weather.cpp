#include "space_weather.hpp"
#include "platform.hpp"
#include "core/simulation.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
namespace albert::providers {
std::vector<SpaceWeatherDay> parseSpaceWeather(const std::string& csv){
    std::vector<SpaceWeatherDay> days;std::istringstream in(csv);std::string line;std::map<std::string,std::size_t> column;
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();
        std::vector<std::string> f;std::string cell;std::istringstream s(line);while(std::getline(s,cell,','))f.push_back(cell);
        if(f.empty())continue;
        if(column.empty()){for(std::size_t i=0;i<f.size();++i)column[f[i]]=i;
            for(const char* need:{"DATE","AP_AVG","F10.7_OBS","F10.7_DATA_TYPE","F10.7_OBS_CENTER81","AP1","AP8"})if(!column.contains(need))throw std::runtime_error("Unexpected space-weather columns");
            continue;}
        auto get=[&](const char* name)->std::string{const auto i=column.at(name);return i<f.size()?f[i]:std::string();};
        const auto date=time::parseUtc(get("DATE"));if(!date)throw std::runtime_error("Invalid space-weather date");
        SpaceWeatherDay d;d.day=std::floor(*date/86400.);d.type=get("F10.7_DATA_TYPE");
        auto number=[](const std::string& v){return v.empty()?std::nan(""):std::stod(v);};
        d.f107=number(get("F10.7_OBS"));d.f107a=number(get("F10.7_OBS_CENTER81"));d.apAverage=number(get("AP_AVG"));
        d.hasAp=true;for(int k=0;k<8;++k){const std::string name="AP"+std::to_string(k+1);d.ap[static_cast<std::size_t>(k)]=number(get(name.c_str()));if(std::isnan(d.ap[static_cast<std::size_t>(k)]))d.hasAp=false;}
        if(!std::isnan(d.f107)&&!std::isnan(d.f107a))days.push_back(d);
    }
    return days;
}
std::optional<physics::SpaceWeather> spaceWeatherAt(const std::vector<SpaceWeatherDay>& days,double utc,std::string* note){
    std::map<double,const SpaceWeatherDay*> byDay;for(const auto& d:days)byDay[d.day]=&d;
    const double day=std::floor(utc/86400.);const auto today=byDay.find(day),yesterday=byDay.find(day-1);
    if(today==byDay.end()||yesterday==byDay.end())return std::nullopt;
    physics::SpaceWeather w;w.f107=yesterday->second->f107;w.f107a=today->second->f107a;
    const double daily=today->second->apAverage;w.ap.fill(std::isnan(daily)?15.:daily);
    bool history=true;for(int k=0;k<4;++k){const auto d=byDay.find(day-k);if(d==byDay.end()||!d->second->hasAp)history=false;}
    if(history){ // Interval 0 is the current 3-hour period; older ones count backwards.
        const int current=static_cast<int>((utc-day*86400.)/10800.);
        auto ap=[&](int back){const int k=current-back;const int dayOffset=k>=0?0:(-k+7)/8;return byDay.at(day-dayOffset)->ap[static_cast<std::size_t>(k+8*dayOffset)];};
        w.ap[1]=ap(0);w.ap[2]=ap(1);w.ap[3]=ap(2);w.ap[4]=ap(3);
        double a=0,b=0;for(int i=4;i<12;++i)a+=ap(i);for(int i=12;i<20;++i)b+=ap(i);w.ap[5]=a/8;w.ap[6]=b/8;w.history=true;
    }
    if(note){char s[160];std::snprintf(s,sizeof s,"F10.7 %.1f, 81-day %.1f, Ap %.0f (%s)",w.f107,w.f107a,w.ap[0],today->second->type.c_str());*note=s;}
    return w;
}
SpaceWeatherService::SpaceWeatherService(std::filesystem::path cache,Transport transport,Now now):cache_(std::move(cache)),transport_(std::move(transport)),now_(std::move(now)){
    if(!transport_)transport_=[](const std::string& url,DownloadProgress* p){return httpGet(url,p,{16*1024*1024,90});};
    if(!now_)now_=[]{return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();};
}
std::vector<SpaceWeatherDay> SpaceWeatherService::table(const std::string& name,double maximumAge){
    const auto path=cache_/name;std::error_code ec;std::string text;
    const auto age=std::filesystem::exists(path,ec)?std::chrono::duration<double>(std::filesystem::file_time_type::clock::now()-std::filesystem::last_write_time(path,ec)).count():1e18;
    if(age<maximumAge){std::ifstream in{path,std::ios::binary};text.assign(std::istreambuf_iterator<char>(in),{});}
    if(text.empty())try{
        text=transport_("https://celestrak.org/SpaceData/"+name,nullptr);parseSpaceWeather(text);
        std::filesystem::create_directories(cache_,ec);const auto partial=path.string()+".tmp";
        {std::ofstream o{partial,std::ios::binary};o.write(text.data(),static_cast<std::streamsize>(text.size()));}std::filesystem::rename(partial,path,ec);
    }catch(const std::exception&){std::ifstream in{path,std::ios::binary};text.assign(std::istreambuf_iterator<char>(in),{});if(text.empty())throw;} // Stale copy beats none.
    return parseSpaceWeather(text);
}
std::shared_ptr<const physics::Msis> SpaceWeatherService::msis(double utc){
    std::lock_guard lock(mutex_);std::string note;
    auto w=spaceWeatherAt(table("SW-Last5Years.csv",12*3600.),utc,&note);
    if(!w)w=spaceWeatherAt(table("SW-All.csv",7*86400.),utc,&note);
    if(!w)return nullptr;
    return std::make_shared<const physics::Msis>(*w,"NRLMSISE-00 ("+note+", CelesTrak)");
}
SpaceWeatherService& spaceWeather(){static SpaceWeatherService value(dataDirectory()/"spaceweather");return value;}
}
