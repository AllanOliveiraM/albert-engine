#include "flight_paths.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
using V=albert::core::Vector;
constexpr double mu=3.986004418e14,radius=albert::core::earthRadiusMeters;
double length(V v){return std::hypot(v.x,v.y,v.z);}
V unit(V v){const double r=length(v);return r>0?v*(1/r):V{};}
V cross(V a,V b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V gravity(V p){const double r=length(p);return r>1?p*(-mu/(r*r*r)):V{};}
bool belowSurface(V p){constexpr double polar=6356752.314245;return (p.x*p.x+p.y*p.y)/(radius*radius)+p.z*p.z/(polar*polar)<1;}
}
std::vector<albert::TrajectoryPoint> albert::predictTrajectory(TrajectoryPoint p,double seconds,double step){
    std::vector<TrajectoryPoint> out;
    if(!std::isfinite(length(p.position))||!std::isfinite(length(p.velocity))||length(p.position)<radius-25000||!std::isfinite(seconds)||!std::isfinite(step)||seconds<=0||step<=0)return out;
    seconds=std::min(seconds,12000.);step=std::max(step,seconds/1200);out.push_back(p);
    for(double elapsed=0;elapsed<seconds;){const double h=std::min(step,seconds-elapsed);const V r=p.position,v=p.velocity;
        const V a=gravity(r),v2=v+a*(h/2),a2=gravity(r+v*(h/2)),v3=v+a2*(h/2),a3=gravity(r+v2*(h/2)),v4=v+a3*h,a4=gravity(r+v3*h);
        p.position=r+(v+v2*2+v3*2+v4)*(h/6);p.velocity=v+(a+a2*2+a3*2+a4)*(h/6);p.utc+=h;elapsed+=h;
        if(belowSurface(p.position)){break;} // Stop at the reference ellipsoid; never draw through the Earth.
        out.push_back(p);if(length(p.position)>1e9)break;
    }
    return out;
}
std::vector<albert::TrajectoryPoint> albert::starshipTargetOrbit(TrajectoryPoint launch){
    const V up=unit(launch.position),east=unit(cross({0,0,1},up)),north=unit(cross(up,east));
    const double heading=93*std::numbers::pi/180;
    const V along=unit(north*std::cos(heading)+east*std::sin(heading));
    const double a=radius+190000,e=10000/a,p=a*(1-e*e),period=2*std::numbers::pi*std::sqrt(a*a*a/mu);
    std::vector<TrajectoryPoint> out;
    for(int i=0;i<=256;++i){const double angle=2*std::numbers::pi*i/256,c=std::cos(angle),s=std::sin(angle),r=p/(1+e*c);
        const V position=(up*c+along*s)*r,velocity=(up*(-s)+along*(e+c))*std::sqrt(mu/p);
        out.push_back({launch.utc+period*i/256,position,velocity});
    }
    return out;
}
void albert::FlightHistory::update(const core::Object& flight,double now){
    // A stage replacement changes the scene instance while the mission continues.
    if(!points_.empty()&&now<points_.back().utc)clear();instance_=flight.instance;
    for(const auto& p:flight.samples)if(p.utcSeconds<=now&&(points_.empty()||p.utcSeconds>=points_.back().utc+1)){
        points_.push_back({p.utcSeconds,p.positionMeters,p.velocityMetersPerSecond});
        if(points_.size()>=8192){std::vector<TrajectoryPoint> reduced;reduced.reserve(4096);for(std::size_t i=0;i<points_.size();i+=2)reduced.push_back(points_[i]);points_=std::move(reduced);}
    }
}
