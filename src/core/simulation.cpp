#include "simulation.hpp"
#include "time.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace albert::time {
namespace {
// Days since 1970-01-01 (H. Hinnant, "chrono-compatible low-level date algorithms").
long long daysFromCivil(long long y,unsigned m,unsigned d){
    y-=m<=2;const long long era=(y>=0?y:y-399)/400;const auto yoe=static_cast<unsigned>(y-era*400);
    const unsigned doy=(153*(m>2?m-3:m+9)+2)/5+d-1,doe=yoe*365+yoe/4-yoe/100+doy;
    return era*146097+static_cast<long long>(doe)-719468;
}
bool leap(int year){return (year%4==0&&year%100!=0)||year%400==0;}
int monthDays(int year,int month){constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};return month==2&&leap(year)?29:days[month-1];}
void validRate(double rate){if(!std::isfinite(rate)||std::abs(rate)>Simulation::maximumRate)throw std::invalid_argument("Simulation rate out of range");}
void validInstant(double utc){if(!std::isfinite(utc)||utc<Simulation::earliestUtc||utc>Simulation::latestUtc)throw std::invalid_argument("Simulated date out of range (1900-2100)");}
}

Simulation::Simulation(Now utc,Now monotonic):utc_(std::move(utc)),monotonic_(std::move(monotonic)){
    if(!utc_)utc_=[]{return clock().utcSeconds();};
    if(!monotonic_)monotonic_=monotonicSeconds;
}
Simulated Simulation::at(double monotonic) const{
    if(live_)return {utc_(),1.,true,false,generation_,1.};
    const double value=anchorUtc_+rate_*(monotonic-anchorMonotonic_);
    if(value<earliestUtc||value>latestUtc)return {std::clamp(value,earliestUtc,latestUtc),0.,false,true,generation_,0.};
    return {value,rate_,false,false,generation_,pace_};
}
Simulated Simulation::read() const{std::lock_guard lock(mutex_);return at(monotonic_());}
void Simulation::setRate(double rate){choose(std::nullopt,rate);}
void Simulation::jump(double utc){choose(utc,std::nullopt);}
void Simulation::choose(std::optional<double> utc,std::optional<double> rate){
    if(utc)validInstant(*utc);
    if(rate)validRate(*rate);
    std::lock_guard lock(mutex_);
    if(!utc&&(!rate||(live_&&*rate==1.)))return; // A live clock at 1x stays live.
    const double now=monotonic_();
    anchorUtc_=utc?*utc:at(now).utcSeconds;anchorMonotonic_=now;
    if(rate)pace_=rate_=*rate;
    live_=false;
    if(utc)++generation_;
}
void Simulation::stall(double utc){
    validInstant(utc);std::lock_guard lock(mutex_);
    if(live_)return; // Real time never waits for data.
    anchorUtc_=utc;anchorMonotonic_=monotonic_();
}
void Simulation::follow(double utc,double rate){
    validInstant(utc);validRate(rate);std::lock_guard lock(mutex_);
    if(live_)return;
    const double now=monotonic_(),current=at(now).utcSeconds,error=utc-current;anchorMonotonic_=now;
    pace_=rate;if(std::abs(error)>std::max(1.,.5*std::abs(rate))){anchorUtc_=utc;rate_=rate;return;}
    // Eased over 2 s within half to twice its rate: a flight publishing in bursts (a slow frame loop, a throttled CPU) had the scene
    // stop and dash (over a quarter second, clamped at nought).
    anchorUtc_=current;rate_=std::clamp(rate+error/2.,std::max(0.,.5*rate),std::min(maximumRate,2*rate));
}
void Simulation::live(){std::lock_guard lock(mutex_);if(live_)return;live_=true;pace_=rate_=1.;++generation_;}
Simulation& simulation(){static Simulation value;return value;}

Civil civil(double utc){
    if(!std::isfinite(utc)||std::abs(utc)>1e14)throw std::invalid_argument("Invalid UTC timestamp");
    const auto seconds=static_cast<long long>(std::floor(utc));
    long long days=seconds/86400,rest=seconds%86400;if(rest<0){rest+=86400;--days;}
    days+=719468;const long long era=(days>=0?days:days-146096)/146097;const auto doe=static_cast<unsigned>(days-era*146097);
    const unsigned yoe=(doe-doe/1460+doe/36524-doe/146096)/365,doy=doe-(365*yoe+yoe/4-yoe/100),mp=(5*doy+2)/153;
    const unsigned day=doy-(153*mp+2)/5+1,month=mp<10?mp+3:mp-9;
    const long long year=static_cast<long long>(yoe)+era*400+(month<=2);
    return {static_cast<int>(year),static_cast<int>(month),static_cast<int>(day),static_cast<int>(rest/3600),static_cast<int>(rest/60%60),static_cast<int>(rest%60)};
}
double unixSeconds(const Civil& c){
    if(c.year<-9999||c.year>9999||c.month<1||c.month>12||c.day<1||c.day>monthDays(c.year,c.month)||c.hour<0||c.hour>23||c.minute<0||c.minute>59||c.second<0||c.second>59)
        throw std::invalid_argument("Invalid civil date");
    return static_cast<double>(daysFromCivil(c.year,static_cast<unsigned>(c.month),static_cast<unsigned>(c.day))*86400+c.hour*3600+c.minute*60+c.second);
}
std::optional<double> parseUtc(std::string_view text){
    auto trim=[&]{while(!text.empty()&&(text.front()==' '||text.front()=='\t'))text.remove_prefix(1);while(!text.empty()&&(text.back()==' '||text.back()=='\t'))text.remove_suffix(1);};
    trim();
    if(text.ends_with("UTC"))text.remove_suffix(3);else if(text.ends_with('Z'))text.remove_suffix(1);
    trim();
    // Fixed-width fields: digits only, so signs, exponents and spaces are rejected.
    auto field=[&](size_t offset,size_t width,int& out){
        if(text.size()<offset+width)return false;out=0;
        for(size_t i=offset;i<offset+width;++i){const char c=text[i];if(c<'0'||c>'9')return false;out=out*10+(c-'0');}
        return true;
    };
    Civil c;
    if(!field(0,4,c.year)||text.size()<10||text[4]!='-'||!field(5,2,c.month)||text[7]!='-'||!field(8,2,c.day))return std::nullopt;
    if(text.size()>10){
        if((text[10]!=' '&&text[10]!='T')||!field(11,2,c.hour)||text.size()<16||text[13]!=':'||!field(14,2,c.minute))return std::nullopt;
        if(text.size()>16&&(text.size()!=19||text[16]!=':'||!field(17,2,c.second)))return std::nullopt;
    }
    try{return unixSeconds(c);}catch(const std::invalid_argument&){return std::nullopt;}
}
std::string formatUtc(double utc){
    const auto c=civil(utc);char text[32];
    std::snprintf(text,sizeof text,"%04d-%02d-%02d %02d:%02d:%02d",c.year,c.month,c.day,c.hour,c.minute,c.second);
    return text;
}
}
