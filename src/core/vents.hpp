#pragma once
#include "scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace albert::core {
inline constexpr std::uint64_t farmVent=std::uint64_t{1}<<63;
inline std::uint64_t tankVentId(std::size_t part){return farmVent|static_cast<std::uint64_t>(part);}
inline double ventFlow(std::uint64_t vent,double utc){
    if(!std::isfinite(utc)||std::abs(utc)>1e15)return 0;
    auto random=[&](std::int64_t bucket,std::uint64_t salt){std::uint64_t x=vent^(static_cast<std::uint64_t>(bucket)*0x9e3779b97f4a7c15ull)^salt;
        x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return static_cast<double>((x^(x>>31))>>11)*0x1.0p-53;};
    auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);};
    const double t=utc/32;const auto slow=static_cast<std::int64_t>(std::floor(t));const double blend=smooth(t-std::floor(t));
    const double drift=(1-blend)*random(slow,0x632be59bd9b4e019ull)+blend*random(slow+1,0x632be59bd9b4e019ull);
    const double base=(vent&farmVent)?.2+.2*drift:.2+.3*drift; // A low drifting plume the releases stand out of (at .4-.8 they were heard as one steady sound).
    // A farm vent releases about every 3 minutes (with ~40 of them, one now and then from somewhere in the farm; every 40 s had ~8 at
    // once: a steady roar), the stack's every 28-52 s.
    const double period=(vent&farmVent)?180:40;const auto bucket=static_cast<std::int64_t>(std::floor(utc/period));
    const double age=utc-period*static_cast<double>(bucket)-2-(period-28)*random(bucket,0xa0761d6478bd642full);
    const double duration=6+5*random(bucket,0xe7037ed1a0b428dbull),burst=smooth(age/.3)*smooth((duration-age)/2); // A valve opening: ~0.3 s up, ~2 s down.
    return base+(1-base)*burst*(.75+.25*random(bucket,0x8ebc6af09c88c6e3ull)); // Broad releases over a drifting plume.
}
inline double ventFlow(std::uint64_t vent,double utc,bool closed){return closed?0:ventFlow(vent,utc);}
inline bool stackVentsClosed(double countSeconds,bool scrub){return !scrub&&countSeconds>=-40;}
struct StackVent {double height,angle,gain;}; // Height above the drawn vehicle's centre.
inline std::vector<StackVent> stackVents(const VehicleLook& look){
    std::vector<StackVent> out;if(look.tanks.empty())return out;const double half=look.length/2;
    for(std::size_t i=0;i<look.tanks.size();++i)out.push_back({half-look.tanks[i].from,1.1+2.1*static_cast<double>(i),1});
    if(look.engines.size()>=20)out.push_back({half-1.8,.7,2.5});
    else if(std::any_of(look.sections.begin(),look.sections.end(),[](const auto& s){return s.tiles>0;})){out.push_back({-half+4.5,2.3,1.1});out.push_back({half-3,.8,1.1});}
    return out; // One layout keeps acoustic flow and visible outlets indexed alike.
}
}
