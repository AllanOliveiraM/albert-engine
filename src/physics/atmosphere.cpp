#include "physics/atmosphere.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace albert::physics {
namespace {
constexpr double r0=6356766.,g0=9.80665,gasConstant=8.31432,airMass=0.0289644,gmr=g0*airMass/gasConstant;
constexpr std::array<double,8> base{0,11000,20000,32000,47000,51000,71000,84852},lapse{-.0065,0,.001,.0028,0,-.0028,-.002,0};
struct Layers {std::array<double,8> temperature,pressure;};
const Layers& layers(){
    static const Layers l=[]{Layers r;r.temperature[0]=288.15;r.pressure[0]=101325.;
        for(int i=1;i<8;++i){const double dh=base[i]-base[i-1],t=r.temperature[i-1],k=lapse[i-1];r.temperature[i]=t+k*dh;
            r.pressure[i]=r.pressure[i-1]*(k==0?std::exp(-gmr*dh/t):std::pow(t/(t+k*dh),gmr/k));}
        return r;}();
    return l;
}
// Molecular-weight ratio M/M0 from 80 to 86 km geometric, 0.5 km steps (USSA76 Table 8).
constexpr std::array<double,13> ratio{1.,.999996,.999989,.999971,.999941,.999909,.999870,.999829,.999786,.999741,.999694,.999641,.999579};
}
Air marsAtmosphere(double z){
    if(!(z<marsTop))return{};
    constexpr double gas=192.1,gamma=1.3,radius=3389500.,g0=3.7276,cold=140; // J/(kg K); m; m/s²; K.
    struct Layer {double h,t,lapse,p;}; // Geopotential base (m), temperature (K), K/m, pressure (Pa).
    auto down=[](const Layer& l,double dh){const double t=l.t+l.lapse*dh;return l.lapse!=0?l.p*std::pow(t/l.t,-g0/(gas*l.lapse)):l.p*std::exp(-g0*dh/(gas*l.t));};
    static const std::array<Layer,3> layers=[&]{ // Glenn temperatures, 699 Pa at the datum.
        std::array<Layer,3> l{{{0,242.1,-.000998,699},{radius*7000/(radius+7000),0,-.00222,0},{0,cold,0,0}}};
        l[1].t=242.1-.000998*l[1].h;l[1].p=down(l[0],l[1].h);l[2].h=l[1].h+(l[1].t-cold)/.00222;l[2].p=down(l[1],l[2].h-l[1].h);return l;}();
    const double h=radius*z/(radius+z);const Layer& l=h<layers[1].h?layers[0]:h<layers[2].h?layers[1]:layers[2];
    const double t=l.t+l.lapse*(h-l.h),p=down(l,h-l.h);
    return{t,p,p/(gas*t),std::sqrt(gamma*gas*t),{}};
}
Air standardAtmosphere(double z){
    if(!(z<standardTop))return{};
    const double h=r0*z/(r0+z);const auto& l=layers();
    const int i=static_cast<int>(std::upper_bound(base.begin()+1,base.end(),h)-base.begin())-1;
    const double dh=h-base[i],k=lapse[i],tb=l.temperature[i],tm=tb+k*dh;
    const double p=l.pressure[i]*(k==0?std::exp(-gmr*dh/tb):std::pow(tb/tm,gmr/k));
    double m=1;
    if(z>80000.){const double x=(z-80000.)/500.;const int j=std::min(11,static_cast<int>(x));m=ratio[j]+(ratio[j+1]-ratio[j])*(x-j);}
    return{tm*m,p,p*airMass/(gasConstant*tm),std::sqrt(1.4*gasConstant*tm/airMass)};
}
}
