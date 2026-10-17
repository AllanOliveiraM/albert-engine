#include "comets.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace albert::core {
namespace {
// 67P: Knight et al. 2017 (MNRAS 469, S661), Table 2: 1366-1649 cm at r = 1.25 AU in
// 2015 August; -4.2 from 35 to 120 days after perihelion; inbound slope from ~140 cm at
// 2.99 AU (2014 Nov. 12, 5000 km aperture scaled +40% to 10^4 km, as they measured).
// Activity onset at ~4.3 AU (Snodgrass et al. 2013). Grain speed: tens of m/s.
// Halley: peak phase-adjusted Afρ ~8e4 cm (Schleicher et al. 1998, as compared by
// Schleicher & Farnham 2024), taken at 0.9 AU; dust release ∝ r^-3.0±0.7 (Planet.
// Space Sci. 1997); OH measured out to 5.1 AU. Grain speed: hundreds of m/s.
constexpr std::array<CometActivity,2> comets{{
    {"67p",15.,1.25,2.7,4.2,4.3,3.6,20.},
    {"halley",800.,.9,3.,3.,6.,5.1,300.}}};
// Halley-Marcus composite, 0°-normalised, 0-180° in 1° steps (Lowell Observatory,
// dustphaseHM_table.txt, SHA-256 84b92184...).
constexpr std::array<double,181> phase{
    1.0000,0.9596,0.9217,0.8859,0.8522,0.8205,0.7906,0.7624,0.7358,0.7107,0.6871,0.6647,
    0.6436,0.6237,0.6049,0.5872,0.5704,0.5546,0.5396,0.5255,0.5122,0.4996,0.4877,0.4765,
    0.4659,0.4559,0.4465,0.4377,0.4293,0.4215,0.4142,0.4073,0.4009,0.3949,0.3893,0.3840,
    0.3792,0.3747,0.3706,0.3668,0.3634,0.3603,0.3575,0.3540,0.3509,0.3482,0.3458,0.3438,
    0.3421,0.3407,0.3397,0.3389,0.3385,0.3383,0.3385,0.3389,0.3396,0.3405,0.3418,0.3432,
    0.3450,0.3470,0.3493,0.3518,0.3546,0.3576,0.3609,0.3645,0.3683,0.3724,0.3768,0.3815,
    0.3865,0.3917,0.3973,0.4032,0.4094,0.4159,0.4228,0.4300,0.4376,0.4456,0.4540,0.4627,
    0.4720,0.4816,0.4918,0.5024,0.5136,0.5253,0.5375,0.5504,0.5638,0.5780,0.5928,0.6084,
    0.6247,0.6419,0.6599,0.6788,0.6987,0.7196,0.7416,0.7648,0.7892,0.8149,0.8420,0.8706,
    0.9008,0.9327,0.9664,1.0021,1.0399,1.0799,1.1223,1.1673,1.2151,1.2659,1.3200,1.3776,
    1.4389,1.5045,1.5744,1.6493,1.7294,1.8153,1.9075,2.0066,2.1132,2.2281,2.3521,2.4861,
    2.6312,2.7884,2.9592,3.1450,3.3474,3.5685,3.8104,4.0755,4.3669,4.6877,5.0418,5.4336,
    5.8682,6.3518,6.8912,7.4948,8.1724,8.9355,9.7981,10.7767,11.8914,13.1662,14.6309,16.3215,
    18.2826,20.5698,23.2524,26.4178,30.1769,34.6719,40.0862,46.6588,54.7036,64.6368,77.0148,92.5871,
    112.3702,137.7479,170.6031,213.4773,269.7281,343.5886,439.8869,562.9161,713.6276,884.4779,1053.2719,1182.2266,
    1231.1735};
double smooth(double t){t=std::clamp(t,0.,1.);return t*t*(3-2*t);}
// Stumpff functions C(z), S(z), with series near zero.
void stumpff(double z,double& c,double& s){
    if(std::abs(z)<1e-3){c=.5-z/24+z*z/720;s=1./6-z/120+z*z/5040;return;}
    if(z>0){const double q=std::sqrt(z);c=(1-std::cos(q))/z;s=(q-std::sin(q))/(q*q*q);return;}
    const double q=std::sqrt(-z);c=(std::cosh(q)-1)/-z;s=(std::sinh(q)-q)/(q*q*q);
}
double dot(Vector a,Vector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
double norm(Vector a){return std::sqrt(dot(a,a));}
}
std::span<const CometActivity> cometActivities(){return comets;}
const CometActivity* cometActivity(std::string_view id){for(const auto& c:comets)if(c.id==id)return &c;return nullptr;}
double afrho(const CometActivity& c,double r,bool inbound){
    if(!(r>0))throw std::invalid_argument("Invalid heliocentric distance");
    return c.afrhoMetres*std::pow(c.referenceAu/r,inbound?c.inboundSlope:c.outboundSlope)*smooth((c.onsetAu-r)/(c.onsetAu-c.fullAu));
}
double dustPhase(double degrees){
    const double a=std::clamp(degrees,0.,180.);const auto i=std::min<std::size_t>(179,static_cast<std::size_t>(a));const double t=a-static_cast<double>(i);
    return std::exp(std::lerp(std::log(phase[i]),std::log(phase[i+1]),t)); // Log-linear between degrees.
}
StateVector propagate(const StateVector& start,double mu,double dt){
    const double r0=norm(start.position),v0=norm(start.velocity);
    if(!(mu>0)||!(r0>0)||!std::isfinite(dt)||!std::isfinite(v0))throw std::invalid_argument("Invalid two-body state");
    if(dt==0)return start;
    const double root=std::sqrt(mu),radial=dot(start.position,start.velocity)/r0,alpha=2/r0-v0*v0/mu;
    // Time of flight grows monotonically with chi: Newton, kept inside a bracket.
    auto flight=[&](double chi,double& derivative){
        double c,s;const double z=alpha*chi*chi;stumpff(z,c,s);
        derivative=r0*radial/root*chi*(1-z*s)+(1-alpha*r0)*chi*chi*c+r0;
        return (r0*radial/root*chi*chi*c+(1-alpha*r0)*chi*chi*chi*s+r0*chi)/root;
    };
    const double sign=dt>0?1.:-1.;double low=0,high=sign*root*std::abs(dt)/r0,d;
    for(int i=0;i<200&&sign*(flight(high,d)-dt)<0;++i){low=high;high*=2;}
    double chi=std::abs(alpha)>1e-30?std::clamp(root*alpha*dt,std::min(low,high),std::max(low,high)):.5*(low+high);
    for(int i=0;i<100;++i){
        const double error=flight(chi,d)-dt;
        if(sign*error>0)high=chi;else low=chi;
        double next=chi-error*root/d;
        if(!(next>std::min(low,high)&&next<std::max(low,high)))next=.5*(low+high);
        if(std::abs(next-chi)<=1e-14*std::max(1.,std::abs(chi))){chi=next;break;}
        chi=next;
    }
    double c,s;const double z=alpha*chi*chi;stumpff(z,c,s);
    const double f=1-chi*chi/r0*c,g=dt-chi*chi*chi*s/root;
    const Vector position=start.position*f+start.velocity*g;const double r=norm(position);
    const double fdot=root/(r*r0)*(z*s-1)*chi,gdot=1-chi*chi/r*c;
    return {position,start.position*fdot+start.velocity*gdot};
}
StateAt heliocentricState(const LocalOrbit& orbit){
    if(orbit.parent!="sun")throw std::invalid_argument("Not a heliocentric orbit");
    return [&orbit](double jd){const auto s=localState(orbit,jd);return StateVector{s.positionMeters,s.velocityMetersPerSecond};};
}
DustGrid dustGrid(const StateAt& state,const CometActivity& comet,double jd,std::span<const double> betas,std::span<const double> ages){
    if(!state||betas.size()<2||ages.empty())throw std::invalid_argument("Empty dust grid");
    constexpr double au=149597870700.;
    DustGrid grid{{betas.begin(),betas.end()},{ages.begin(),ages.end()},{},{}};
    const auto now=state(jd);
    // Fraction of Afρ per unit ln(beta): ∝ sqrt(beta), normalised over the grid (trapezoids).
    double total=0;for(std::size_t i=0;i+1<betas.size();++i){
        if(!(betas[i]>0&&betas[i+1]>betas[i]&&betas[i+1]<1))throw std::invalid_argument("Invalid beta grid");
        total+=.5*(std::sqrt(betas[i])+std::sqrt(betas[i+1]))*std::log(betas[i+1]/betas[i]);}
    for(const double beta:betas)for(const double age:ages){
        if(!(age>0))throw std::invalid_argument("Invalid dust age");
        const auto released=state(jd-age/86400.);
        const double r=norm(released.position)/au;const bool inbound=dot(released.position,released.velocity)<0;
        const auto grain=propagate(released,(1-beta)*solarGM,age);
        grid.offsets.push_back(grain.position-now.position);
        const double speed=comet.grainSpeed*std::sqrt(beta/.1/r);
        grid.rate.push_back(2*speed*afrho(comet,r,inbound)*std::sqrt(beta)/total);
    }
    return grid;
}
}
