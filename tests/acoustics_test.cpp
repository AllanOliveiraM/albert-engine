// Acoustics against published data: ISO 9613-1 absorption (ISO 9613-2 Table 2; ISO 9613-1 Table 1), NASA SP-8072 rocket noise
// against the maximum levels measured by BYU at Starship and SLS launches, the propagation (retarded time, Doppler), the flow
// noise and sonic booms (Carlson against Concorde; the Mach cone's arrival).
#include "physics/acoustics.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace albert::physics;
namespace ac=albert::physics::acoustics;
namespace {
int failures=0;
void near(double got,double want,double tolerance,const std::string& what){
    const bool ok=std::abs(got-want)<=tolerance;std::printf("  %-52s %10.3f  reference %10.3f%s\n",what.c_str(),got,want,ok?"":"  <-- outside");if(!ok)++failures;}
constexpr double pi=3.14159265358979323846,deg=pi/180;
}
int main(){try{
    std::printf("ISO 9613-2 Table 2, 10 °C and 70%% RH (dB/km, octave bands at the exact centres):\n");
    {const double table[]{.1,.4,1.0,1.9,3.7,9.7,32.8,117};
     for(int i=0;i<8;++i){const double f=1000*std::pow(10.,(3*i-12)/10.),a=ac::absorption(f,283.15,101325,ac::molarFromRelative(70,283.15,101325))*1e3;
        near(a,table[i],std::max(.051,table[i]*.02),std::to_string(static_cast<int>(std::lround(f)))+" Hz");}}
    std::printf("ISO 9613-1 Table 1, -20 °C and 10%% RH (dB/km):\n");
    near(ac::absorption(1000,253.15,101325,ac::molarFromRelative(10,253.15,101325))*1e3,1.65,.02,"1 kHz");
    near(ac::absorption(1e4,253.15,101325,ac::molarFromRelative(10,253.15,101325))*1e3,16.4,.2,"10 kHz");
    std::printf("SP-8072: the spectrum and the lobe carry the power once:\n");
    {const ac::RocketNoise small{1e5,2500,.3};double sum=0;for(double l:ac::powerLevels(small))sum+=std::pow(10.,(l-120)/10);
     near(10*std::log10(sum),10*std::log10(ac::soundPower(small)),.5,"bands over the sound power (dB)");
     double s=0,w=0;for(int i=0;i<1800;++i){const double t=(i+.5)/10,ws=std::sin(t*deg);s+=std::pow(10.,ac::directivity(t)/10)*ws;w+=ws;}
     near(10*std::log10(s/w),0,.02,"directivity averaged over the sphere (dB)");near(ac::directivity(65),4.8,.1,"directivity at 65° (dB)");}
    // Maximum overall level where the receiver sees the vehicle 65° off its plume (25° up), 1.5 m over hard ground, air at
    // 20 °C and 70% RH. Starship (Gee et al. 2025): 74 MN, 3.2 km/s, 33 nozzles of 1.3 m. SLS Artemis I (Gee et al. 2023):
    // 39.1 MN, 52 GW of jet power (2.66 km/s), two 3.87 m and four 2.30 m nozzles; both at η 0.35% and 0.33%.
    auto maximum=[&](const ac::RocketNoise& r,double horizontal){
        const double slant=horizontal/std::cos(25*deg);ac::Spectrum l=ac::freeField(r,slant,65);const ac::Spectrum g=ac::ground(1.5,25*deg,343);
        for(int b=0;b<ac::bands;++b)l[static_cast<std::size_t>(b)]+=g[static_cast<std::size_t>(b)]-ac::absorption(ac::bandCentre(b),293.15,101325,ac::molarFromRelative(70,293.15,101325))*slant;
        return l;};
    std::printf("Maximum overall levels against BYU's launch measurements (dB):\n");
    const ac::RocketNoise starship{74e6,3200,std::sqrt(33.)*1.3,.0035},sls{39.1e6,2*52e9/39.1e6,std::sqrt(2*3.87*3.87+4*2.30*2.30),.0033};
    near(ac::overall(maximum(starship,1000*std::cos(25*deg))),145.7,1.5,"Starship at 1 km");
    near(ac::overall(maximum(starship,5000*std::cos(25*deg))),131.5,1.5,"Starship at 5 km");
    near(ac::overall(maximum(starship,1e4*std::cos(25*deg))),125.2,1.5,"Starship at 10 km");
    near(10*std::log10(ac::soundPower(sls))+120,202.4,.1,"SLS sound power (dB re 1 pW)");
    near(ac::overall(maximum(sls,1480)),136,2.5,"SLS at 1.48 km");near(ac::overall(maximum(sls,5150)),128,2.5,"SLS at 5.15 km");
    {const auto l=maximum(sls,5150);int peak=0;for(int b=1;b<ac::bands;++b)if(l[static_cast<std::size_t>(b)]>l[static_cast<std::size_t>(peak)])peak=b;
     near(ac::bandCentre(peak),20,6,"SLS one-third-octave peak (Hz)");}
    std::printf("Propagation:\n");
    {   // A source 2 km up and 3 km out over a flat 'planet' of standard air, flying away at 100 m/s along x.
        const Planet flat=earth;auto air=[](const Geodetic& g){return standardAtmosphere(g.height);};
        const Geodetic site{0,0,1.5};const V3 listener=fixedPosition(flat,site);
        const double sl=0,cl=1;(void)sl;(void)cl;const V3 up=listener*(1/norm(listener)),east{0,1,0},north{0,0,1};
        std::vector<ac::Emission> history;
        for(int i=0;i<=600;++i){ac::Emission e;e.time=i*.1;e.position=listener+up*2000+east*(3000+100*e.time);e.velocity=east*100;e.plume=up*-1;
            e.noise={7.6e6,3050,2.76};e.on=true;history.push_back(e);}
        const double now=50;const auto h=ac::hear(history,now,listener,{},flat,air);
        const V3 at=listener+up*2000+east*(3000+100*h.emitted);const double c=standardAtmosphere(1000).sound;
        std::printf("  heard %.3f s after emission from %.1f m (Doppler %.4f)\n",h.delay,h.distance,h.doppler);
        near(norm(listener-at)/h.delay,c,3,"distance over delay: the path's sound speed (m/s)");
        const double receding=dot(east*100,(listener-at)*(1/norm(listener-at)));
        near(h.doppler,standardAtmosphere(1.5).sound/(standardAtmosphere(1.5).sound-receding),.002,"Doppler of a receding source");
        auto off=history;for(auto& e:off)e.on=false;near(ac::hear(off,now,listener,{},flat,air).audible?1:0,0,0,"silent while off");
        near(ac::hear(history,.5,listener,{},flat,air).audible?1:0,0,0,"silent until the first sound arrives");
        auto vacuum=[](const Geodetic&){return Air{};};near(ac::hear(history,now,listener,{},flat,vacuum).audible?1:0,0,0,"silent in vacuum");
        auto high=history;for(auto& e:high)e.position=e.position+up*198000; // 200 km up: the path's thin air absorbs it all.
        const auto far=ac::hear(high,1000,listener,{},flat,air);near(far.audible?ac::overall(far.levels):-1e9,-1e9,1e8,"200 km up: absorbed on the way (dB)");
        auto below=history;for(auto& e:below)e.position=listener+east*(-2e6)+up*(-1e6); // Behind the planet.
        near(ac::hear(below,2000,listener,{},flat,air).audible?1:0,0,0,"silent through the ground");
    }
    std::printf("Flow noise and sonic booms:\n");
    {const double w=ac::flowPower(4e7,.24);near(w,1600,1,"an airliner's 40 MW of drag power at Mach 0.24 (W)");
     double sum=0;int peak=0;const auto l=ac::flowLevels(w,80,6);for(int b=0;b<ac::bands;++b){sum+=std::pow(10.,(l[static_cast<std::size_t>(b)]-120)/10);if(l[static_cast<std::size_t>(b)]>l[static_cast<std::size_t>(peak)])peak=b;}
     near(10*std::log10(sum),10*std::log10(w),.3,"bands over the flow's power (dB)");near(std::log10(ac::bandCentre(peak)/(.2*80/6)),0,.2,"peak band at St 0.2 (log10 of f/f_p)");
     near(ac::flowPower(1e9,2)/ac::flowPower(1e9,.5),1,1e-12,"flat above Mach 0.5");}
    {const ac::Boom z1=ac::blast(1,1,101325),z10=ac::blast(1000,100,101325); // Kinney and Graham's table: ~10 atm at Z = 1, ~0.1 at Z = 10.
     near(z1.overpressure/101325,9.95,.1,"blast at a scaled distance of 1 m/kg^1/3 (atm)");near(z10.overpressure/101325,.0985,.002,"and at 10 (atm)");}
    {const Air at=standardAtmosphere(16500);const ac::Boom b=ac::carlson(2,at.pressure,at.sound,101325,16500,62,true);
     near(b.overpressure,93,25,"Concorde's boom, Mach 2 at 16.5 km (Pa; ~93 measured)");near(b.duration,.3,.12,"and its duration (s)");}
    {   // Mach 2 at 10 km, level, eastward over a listener 1.5 m up: the cone arrives h √(M̄²-1) / V after it passes overhead.
        const Planet flat=earth;auto air=[](const Geodetic& g){return standardAtmosphere(g.height);};
        const V3 listener=fixedPosition(flat,{0,0,1.5}),up=listener*(1/norm(listener)),east{0,1,0};const Air high=standardAtmosphere(1e4);const double v=2*high.sound;
        std::vector<ac::Emission> history;
        for(int i=0;i<=1200;++i){ac::Emission e;e.time=i*.05;e.position=listener+up*1e4+east*(v*(e.time-30));e.velocity=east*v;e.length=30;e.mach=2;e.sound=high.sound;e.pressure=high.pressure;history.push_back(e);}
        const double mean=.5*(high.sound+standardAtmosphere(1.5).sound),m=v/mean,expected=30+1e4*std::sqrt(m*m-1)/v;
        const auto before=ac::booms(history,expected-.5,listener,flat,air),after=ac::booms(history,expected+.5,listener,flat,air);
        near(static_cast<double>(before.size()),0,0,"no boom before the cone arrives");near(after.empty()?0:after.back().time,expected,.05,"the cone's arrival (s)");
        // Inside the cone two emissions arrive at once (before and after the Mach angle): the newer, nearer one is heard.
        for(auto& e:history){e.on=true;e.noise={1e6,2500,1};}
        const double later=expected+5;const auto heard=ac::hear(history,later,listener,{},flat,air);double latest=0;
        for(double t=0;t<later;t+=1e-3){const V3 at=listener+up*1e4+east*(v*(t-30)); // The roots at the mid-height's sound speed.
            if(std::abs(later-t-norm(listener-at)/standardAtmosphere(5000).sound)<2e-3)latest=t;}
        near(heard.emitted,latest,.2,"the newer of the two emissions heard (s)");
        near(after.empty()?0:after.back().overpressure,ac::carlson(2,high.pressure,high.sound,standardAtmosphere(1.5).pressure,1e4-1.5,30,true).overpressure,1,"its overpressure by Carlson (Pa; the source a little higher there)");
    }
    if(failures)throw std::runtime_error(std::to_string(failures)+" values outside");
    std::printf("acoustics_test: ok\n");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
