#pragma once
// Apollo 11's powered descent as Luminary 99 flew it, for Albert's `apolloLunarModule`: 26 s at 10% thrust while the
// engine trims, then Klumpp's quadratic guidance (Apollo Lunar-Descent Guidance, R-695, 1971, Eqs. 7 and 12) toward the
// braking-phase (P63) targets of Eagle's erasable load, the approach (P64) targets once the time to go reaches 62 s, and
// a vertical descent at 3 ft/s from 12 s to go (P65; Armstrong took over in P66 there). Throttle: the fixed throttle position
// (93.5%) while the command exceeds 65%, the command itself once it falls below 57% (throttle recovery), 10% to 65%.
// It works in the Moon's rotating frame, as Luminary's guidance frame: x up through the site, z downrange, y across.
// Not modelled: the transport-delay lead (2.2 s), landing radar updates (navigation is exact), landing point redesignation.
#include "gravity_turn.hpp"
namespace albert::example {
class ApolloDescent {
public:
    enum class Phase {trim,braking,approach,vertical,landed};
    // site: the landing site in the Moon's rotating frame (m); toward: a point uprange on the approach ground track.
    ApolloDescent(Vec site,Vec toward,double rated=46706):site_(site),rated_(rated){
        x_=unit(site);const Vec n=unit(cross(toward,site));z_=unit(cross(n,site));y_=cross(z_,x_);}
    Phase phase()const{return phase_;}
    double recovery()const{return recovery_;}   // Elapsed time of throttle recovery (s).
    double highGate()const{return highGate_;}   // Elapsed time at P64 (s).
    double lowGate()const{return lowGate_;}     // At P65.
    Vec frame(Vec v)const{return{dot(v,x_),dot(v,y_),dot(v,z_)};} // Rotating frame to the guidance frame.
    Commands step(const Reading& r){
        Commands c;const Vec R=frame(r.fixedPosition+site_*-1),V=frame(r.fixedVelocity);
        const double rn=norm(r.fixedPosition);const Vec G=frame(r.fixedPosition*(-mu/(rn*rn*rn)));
        Vec thrust; // Commanded thrust acceleration, guidance frame.
        if(phase_==Phase::trim){c.ignite=r.elapsed==0;thrust=unit(V)*-1;c.throttle=.1;if(r.elapsed>=26)phase_=Phase::braking;}
        if(phase_==Phase::braking||phase_==Phase::approach){
            const Target& t=phase_==Phase::braking?braking:approach;
            // Time to go (negative) from the downrange jerk: JTz T³ + 6 ATz T² + (18 VTz + 6 Vz) T + 24 (RTz - Rz) = 0, by Newton.
            for(int i=0;i<20;++i){const double f=t.j*T_*T_*T_+6*t.a.z*T_*T_+(18*t.v.z+6*V.z)*T_+24*(t.r.z-R.z),d=3*t.j*T_*T_+12*t.a.z*T_+18*t.v.z+6*V.z;
                const double next=T_-f/d;if(!(next<0))break;const bool done=std::abs(next-T_)<1e-6;T_=next;if(done)break;}
            const Vec acg=(t.r+R*-1)*(12/(T_*T_))+(t.v+V)*(6/T_)+t.a;thrust=acg+G*-1;
            if(phase_==Phase::braking&&T_>-62){phase_=Phase::approach;highGate_=r.elapsed;T_=-62;}
            else if(phase_==Phase::approach&&T_>-12){phase_=Phase::vertical;lowGate_=r.elapsed;}
        }
        if(phase_==Phase::vertical){ // Rate of descent 3 ft/s, horizontal speed nulled over 2 s, tilt up to 20°.
            const Vec want{-.9144,0,0};thrust=(want+V*-1)*.5+G*-1;
            const double horizontal=std::hypot(thrust.y,thrust.z),limit=std::tan(20*3.14159265358979323846/180)*std::max(thrust.x,0.);
            if(horizontal>limit){thrust.y*=limit/horizontal;thrust.z*=limit/horizontal;}
            if(r.contact){c.cutoff=true;phase_=Phase::landed;}
        }
        if(phase_!=Phase::trim&&phase_!=Phase::landed){
            const double command=r.mass*norm(thrust)/rated_;
            if(command>.65)fixed_=true;else if(fixed_&&command<.57){fixed_=false;if(recovery_==0)recovery_=r.elapsed;}
            c.throttle=fixed_?.935:std::clamp(command,.1,.65);
        }
        if(phase_==Phase::landed){c.torque=Vec{-r.rate.x,-r.rate.y,-r.rate.z}*inertia;return c;}
        // Thrust (body x) along the command: guidance frame → rotating frame → NED (a sphere) → body, by ideal torque.
        const Vec d=unit(x_*thrust.x+y_*thrust.y+z_*thrust.z),p=unit(r.fixedPosition);const double lat=std::asin(p.z),lon=std::atan2(p.y,p.x);
        const Vec north{-std::sin(lat)*std::cos(lon),-std::sin(lat)*std::sin(lon),std::cos(lat)},east{-std::sin(lon),std::cos(lon),0};
        const Vec ned{dot(d,north),dot(d,east),-dot(d,p)};
        const double w=r.qw,qx=r.qx,qy=r.qy,qz=r.qz;
        const Vec n0{1-2*(qy*qy+qz*qz),2*(qx*qy+w*qz),2*(qx*qz-w*qy)},n1{2*(qx*qy-w*qz),1-2*(qx*qx+qz*qz),2*(qy*qz+w*qx)},n2{2*(qx*qz+w*qy),2*(qy*qz-w*qx),1-2*(qx*qx+qy*qy)};
        const Vec b{dot(n0,ned),dot(n1,ned),dot(n2,ned)}; // The command in body axes; the error is x × b.
        c.torque=Vec{-2.3*r.rate.x,-2*b.z-2.3*r.rate.y,2*b.y-2.3*r.rate.z}*inertia;
        return c;
    }
    double inertia=9e4,mu=4.9028001224453e12; // Pitch inertia (kg m²) for the attitude gain; the Moon's GM (m³/s²).
private:
    struct Target {Vec r,v,a;double j;};
    // Eagle's erasable load (LM-5 Data Book, Luminary 99): RBRFG/VBRFG/ABRFG/JBRFGZ and RAPFG/VAPFG/AAPFG/JAPFGZ (m, m/s, m/s², m/s³).
    static constexpr Target braking{{52.375,0,-3254.836},{-32.271,0,-.317},{.19023,0,-2.77502},-.0057384};
    static constexpr Target approach{{33.859,0,-8.167},{-1.5219,0,.0756},{-.07998,0,-.15606},.000551};
    static Vec unit(Vec a){return a*(1/norm(a));}
    Vec site_,x_,y_,z_;double rated_,T_=-600,recovery_=0,highGate_=0,lowGate_=0;bool fixed_=true;Phase phase_=Phase::trim;
};
}
