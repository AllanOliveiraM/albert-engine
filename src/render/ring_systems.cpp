#include "ring_systems.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace {
using albert::RingBand;
// PDS Ring-Moon Systems Node, Jupiter ring table (de Pater et al. 2018; Burns et al. 1999).
// The main ring optical depth is an upper limit (< 8e-6); 5e-6 is used.
constexpr std::array<RingBand,5> jupiter{{
    {100000,122400,1e-6,true},{122400,129100,5e-6,true},{122400,181350,5e-7,true},
    {122400,221900,1e-7,true},{221900,270000,1e-9,true}}};
// Uranus ring table (Nicholson et al. 2018; Showalter and Lissauer 2006): centre
// radius +- width/2. Epsilon's optical depth varies from 0.5 to 2.3 along the ring; 1 is used.
constexpr RingBand band(double centre,double width,double tau,bool dust){return {centre-width/2,centre+width/2,tau,dust};}
constexpr std::array<RingBand,21> uranus{{
    band(30863,8050,1e-4,true),band(36639,2960,5e-4,true),band(38440,23200,.005,true),band(39600,3500,.0045,true),
    band(41838,1.53,.3,false),band(42234,2.28,.5,false),band(42571,2.33,.3,false),band(43027,3353,.002,true),
    band(44718,8.46,.4,false),band(44879,312,.002,true),band(45661,9.49,.3,false),band(47201,40,.02,true),
    band(47176,1.6,.4,false),band(47627,2.15,.3,false),band(48289,15,.03,true),band(48300,4.6,.5,false),
    band(49936,3.1,.15,true),band(50024,2.3,.1,false),band(51149,58.1,1.,false),band(67300,3800,5.6e-6,true),
    band(97700,17000,8.5e-6,true)}};
// Neptune ring table (de Pater et al. 2018; Porco et al. 1995). Le Verrier's width is
// an upper limit (< 100 km); the Adams arcs (optical depth ~0.1) and Arago (no width
// or depth listed) are not drawn. The narrow rings are dust-rich: half of their depth
// is drawn as dust.
constexpr std::array<RingBand,6> neptune{{
    band(42000,2000,1e-4,true),band(53200,100,.0015,false),band(53200,100,.0015,true),band(55200,4000,1e-4,true),
    band(62933,15,.0015,false),band(62933,15,.0015,true)}};
// Ring particles of all three systems are very dark (geometric albedo of a few per
// cent); dust scatters mostly forward. Neutral colour; an approximate visual model.
constexpr std::array<albert::RingSystem,3> systems{{
    {"jupiter",jupiter,.03f,.5f,.7f},{"uranus",uranus,.03f,.5f,.7f},{"neptune",neptune,.03f,.5f,.7f}}};
}

const albert::RingSystem* albert::ringSystem(std::string_view planet){
    for(const auto& s:systems)if(s.planet==planet)return &s;
    return nullptr;
}

albert::RingProfile albert::ringProfile(const RingSystem& system,unsigned texels){
    if(texels<2||system.bands.empty())throw std::invalid_argument("Empty ring profile");
    RingProfile profile;profile.innerKm=system.bands.front().innerKm;profile.outerKm=system.bands.front().outerKm;
    for(const auto& b:system.bands){profile.innerKm=std::min(profile.innerKm,b.innerKm);profile.outerKm=std::max(profile.outerKm,b.outerKm);}
    profile.tau.assign(2*static_cast<size_t>(texels),0.f);const double step=(profile.outerKm-profile.innerKm)/texels;
    for(const auto& b:system.bands){
        const auto first=static_cast<unsigned>(std::clamp((b.innerKm-profile.innerKm)/step,0.,texels-1.));
        const auto last=static_cast<unsigned>(std::clamp((b.outerKm-profile.innerKm)/step,0.,texels-1.));
        for(unsigned i=first;i<=last;++i){
            const double low=profile.innerKm+i*step,overlap=std::min(b.outerKm,low+step)-std::max(b.innerKm,low);
            if(overlap>0)profile.tau[2*i+(b.dust?1:0)]+=static_cast<float>(b.tau*overlap/step);
        }
    }
    return profile;
}

float albert::ringReferenceReflectance(const RingSystem& system,const RingProfile& profile){
    // Same slab model as the shader, lit side, mu = mu0 = 0.5: mu0/(mu+mu0) = 0.5.
    const double g=system.dustAsymmetry,side=(1-g*g)/std::pow(1+g*g,1.5);float brightest=0;
    for(size_t i=0;i<profile.tau.size();i+=2){
        auto slab=[](double tau){return .5*(1-std::exp(-4*tau));};
        // Lambert-sphere phase at 90 deg is 1/pi.
        brightest=std::max(brightest,static_cast<float>(system.particleAlbedo/std::numbers::pi*slab(profile.tau[i])+system.dustAlbedo*side/4*slab(profile.tau[i+1])));
    }
    return brightest;
}
