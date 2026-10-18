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
std::optional<physics::SpaceWeather> spaceWeatherAt(const std::vector<SpaceWeatherDay>& days,double utc,std::string* note){return physics::SpaceWeatherTable(days).at(utc,note);}
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
std::shared_ptr<const physics::Msis> SpaceWeatherService::msis(double utc,std::vector<SpaceWeatherDay>* used){
    std::lock_guard lock(mutex_);
    auto days=table("SW-Last5Years.csv",12*3600.);if(!physics::SpaceWeatherTable(days).at(utc))days=table("SW-All.csv",7*86400.);
    const double d=std::floor(utc/86400.);std::erase_if(days,[&](const SpaceWeatherDay& x){return x.day<d-4||x.day>d+60;}); // The flight's span.
    auto m=msisFrom(days,utc);if(m&&used)*used=std::move(days);
    return m;
}
std::shared_ptr<const physics::Msis> msisFrom(const std::vector<SpaceWeatherDay>& days,double utc){
    std::string note;auto t=std::make_shared<const physics::SpaceWeatherTable>(days);const auto w=t->at(utc,&note);
    return w?std::make_shared<const physics::Msis>(*w,"NRLMSISE-00 (at start: "+note+", CelesTrak)",std::move(t)):nullptr;
}
SpaceWeatherService& spaceWeather(){static SpaceWeatherService value(dataDirectory()/"spaceweather");return value;}
}
