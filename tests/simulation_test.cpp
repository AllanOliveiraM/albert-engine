#include "core/simulation.hpp"
#include "core/approximate_ephemeris.hpp"
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace albert::time;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F>void rejects(F fn,const char* message){bool rejected=false;try{fn();}catch(const std::invalid_argument&){rejected=true;}require(rejected,message);}
int main(){try{
    // Civil dates against independent references (Unix timestamps from `date -u -d`).
    require(unixSeconds({1969,7,20,20,17,40})==-14182940.,"Apollo 11 landing instant");
    require(unixSeconds({1900,1,1,0,0,0})==Simulation::earliestUtc&&unixSeconds({2100,12,31,23,59,59})==Simulation::latestUtc,"Supported interval");
    require(unixSeconds({2000,2,29,12,0,0})==951825600.&&unixSeconds({1910,4,20,0,0,0})==-1884038400.,"Leap day and Halley 1910 perihelion");
    rejects([]{unixSeconds({1900,2,29,0,0,0});},"1900 is not a leap year");
    rejects([]{unixSeconds({2023,2,29,0,0,0});},"2023 is not a leap year");
    rejects([]{unixSeconds({2026,4,31,0,0,0});},"April has 30 days");
    rejects([]{unixSeconds({2026,1,1,24,0,0});},"Hour out of range");
    {const auto c=civil(-14182940.);require(c.year==1969&&c.month==7&&c.day==20&&c.hour==20&&c.minute==17&&c.second==40,"Civil date before 1970");}
    {const auto c=civil(-0.5);require(c.year==1969&&c.month==12&&c.day==31&&c.hour==23&&c.minute==59&&c.second==59,"Fractions round down");}
    std::mt19937_64 random(7);std::uniform_int_distribution<long long> instant(static_cast<long long>(Simulation::earliestUtc),static_cast<long long>(Simulation::latestUtc));
    for(int i=0;i<20000;++i){const double t=static_cast<double>(instant(random));require(unixSeconds(civil(t))==t,"Civil round trip");}
    require(formatUtc(-14182940.)=="1969-07-20 20:17:40","Formatted date");
    require(parseUtc("1969-07-20 20:17")==-14182980.&&parseUtc(" 1969-07-20T20:17:40Z ")==-14182940.&&parseUtc("1969-07-20")==-14256000.&&parseUtc("2026-10-17 12:00 UTC")==1792238400.,"Accepted formats");
    for(const char* bad:{"","1969-7-20","1969-07-20 20","1969-07-20 20:17:4","1969-07-20 25:00","+969-07-20","1969-02-30","1969-07-20 20:17:40:00","19690720"})
        require(!parseUtc(bad),"Malformed date accepted");

    // Deterministic clocks: real UTC and monotonic seconds.
    double real=1792238400.,mono=100.;
    Simulation s([&]{return real;},[&]{return mono;});
    auto state=s.read();require(state.live&&state.rate==1.&&state.utcSeconds==real&&state.generation==0,"Starts live");
    s.setRate(1.);require(s.read().live,"1x keeps a live clock live");
    real+=5;mono+=5;s.setRate(3600.);state=s.read();
    require(!state.live&&state.utcSeconds==real&&state.rate==3600.&&state.generation==0,"A rate change is continuous");
    mono+=2;real+=2;require(std::abs(s.read().utcSeconds-(real-2+7200.))<1e-6,"Advances at the chosen rate over monotonic time");
    real+=1000;require(std::abs(s.read().utcSeconds-(real-1002+7200.))<1e-6,"Real clock corrections do not move a chosen date");
    s.setRate(0.);const double paused=s.read().utcSeconds;mono+=50;require(s.read().utcSeconds==paused&&s.read().rate==0.,"Paused");
    s.jump(-14182940.);state=s.read();require(state.utcSeconds==-14182940.&&state.rate==0.&&state.generation==1,"A date jump keeps the rate and changes generation");
    rejects([&]{s.choose(Simulation::latestUtc+1,1.);},"Date after 2100 accepted");
    rejects([&]{s.choose(0.,Simulation::maximumRate*2);},"Excessive rate accepted");
    rejects([&]{s.setRate(std::nan(""));},"NaN rate accepted");
    state=s.read();require(state.utcSeconds==-14182940.&&state.rate==0.&&state.generation==1,"A rejected choice changed the clock");
    s.choose(0.,-86400.);mono+=1;state=s.read();require(state.utcSeconds==-86400.&&state.rate==-86400.&&state.generation==2,"Atomic choice");
    s.stall(-50000.);mono+=1;state=s.read();require(state.utcSeconds==-136400.&&state.generation==2,"Stall holds without a discontinuity");
    s.jump(Simulation::earliestUtc+10);mono+=1;state=s.read();require(state.limited&&state.utcSeconds==Simulation::earliestUtc&&state.rate==0.,"Held at the first instant");
    s.setRate(60.);mono+=1;state=s.read();require(!state.limited&&state.utcSeconds==Simulation::earliestUtc+60.,"Leaves the limit in the opposite direction");
    s.live();state=s.read();require(state.live&&state.rate==1.&&state.utcSeconds==real&&state.generation==4,"Back to live time");
    s.stall(0.);require(s.read().live,"Live time never stalls");
    // Analytic positions against NASA/JPL Horizons (geocentric ICRF, km, no corrections)
    // at 1969-07-20 and 2026-10-17 00:00 TDB, within the published ERFA maxima
    // (planets: longitude/latitude arcseconds at their distance plus radius error).
    struct Reference {double jd;const char* id;double x,y,z,tolerance;};
    constexpr Reference references[]{
        {2440423.5,"sun",-7.250219056455936E+07,1.225697288194057E+08,5.314976041503397E+07,15},
        {2440423.5,"moon",-3.823596632490958E+05,-5.940820435709668E+04,-3.709429035817704E+04,35},
        {2440423.5,"mercury",-8.901575474913155E+07,1.609583284990715E+08,7.536762791697474E+07,2000},
        {2440423.5,"venus",3.580415975950237E+07,1.307466953239729E+08,4.997059144502492E+07,3700},
        {2440423.5,"mars",-3.982124088335591E+07,-7.031695074015069E+07,-3.620492435876627E+07,30000},
        {2440423.5,"jupiter",-8.738214978429239E+08,-2.416450884348536E+07,9.780605488356031E+06,300000},
        {2440423.5,"saturn",1.096622715126697E+09,8.249109004891208E+08,2.928927025734644E+08,650000},
        {2440423.5,"uranus",-2.804843125642613E+09,-6.433203493078586E+07,1.000704020041034E+07,1300000},
        {2440423.5,"neptune",-2.465873955721946E+09,-3.464408914688577E+09,-1.355473575373392E+09,350000},
        {2461330.5,"sun",-1.369887116888706E+08,-5.404765584697053E+07,-2.342808456130781E+07,15},
        {2461330.5,"moon",3.564962534383548E+04,-3.574847394497418E+05,-1.861948149139171E+05,35},
        {2461330.5,"mercury",-9.257989760383317E+07,-9.027438061629739E+07,-4.738299285954729E+07,2000},
        {2461330.5,"venus",-3.451344835628292E+07,-1.959516421236806E+07,-1.440840400454605E+07,3700},
        {2461330.5,"mars",-1.501475585491952E+08,1.599846450936757E+08,7.509846336278379E+07,30000},
        {2461330.5,"jupiter",-6.728448656018549E+08,4.804932956249886E+08,2.187345219361902E+08,300000},
        {2461330.5,"saturn",1.244855154357466E+09,2.242317550159899E+08,3.200290169689004E+07,650000},
        {2461330.5,"uranus",1.190577433437423E+09,2.322689918496643E+09,9.987250957702235E+08,1300000},
        {2461330.5,"neptune",4.326232123992137E+09,1.849701995151944E+08,-3.670908597766806E+07,350000}};
    for(const auto& r:references){
        const auto p=albert::core::approximateGeocentric(r.id,r.jd);require(p.has_value(),"Analytic body missing");
        const double error=std::hypot(p->x/1000-r.x,p->y/1000-r.y,p->z/1000-r.z);
        if(error>r.tolerance){std::cerr<<r.id<<" at "<<r.jd<<": "<<error<<" km\n";throw std::runtime_error("Analytic position outside the ERFA bound");}
    }
    require(!albert::core::approximateGeocentric("pluto",2461330.5),"Unmodelled body returned a position");
    std::cout<<"simulation_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
