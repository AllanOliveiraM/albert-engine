#pragma once
// A Starship-class flight built only from Albert's generic physics: the vehicle as definitions (no preset in the core), the
// flight program here. Public figures (Wikipedia, SpaceX Super Heavy and SpaceX Starship, read 2026-10-19): Super Heavy
// 71 m × 9 m, 275 t empty, 3,400 t of propellant, 33 Raptor 2 giving 73.5 MN at sea level, Isp 327 s at sea level and 347 s
// in vacuum, the 13 inner engines gimballed, staging near 64 km about 141 s after liftoff, landing burn about 6 min after,
// caught by the tower's arms on hard points by two grid fins; the ship (Block 1) 50.3 m, ~100 t empty (2021), 1,200 t of
// propellant, 3 Raptors and 3 Raptor Vacuum (380 s), 12.25 MN in all. Derived here, so approximate: a Raptor's vacuum thrust
// 2.36 MN = 73.5 MN / 33 × 347 / 327, its exit area 1.34 m² from that difference over 101,325 Pa (Raptor's ~1.3 m nozzle);
// a Raptor Vacuum's 1.86 MN = (12.25 MN - 3 × 2.23 MN) / 3; 1.6 t an engine; the tanks' places; the aerodynamics (crossflow
// on a 9 m cylinder); the grid fins' moments and drag; the booster's RCS (30 kN cold gas); the propellant left at main engine
// cutoff (450 t, 13%: the Δv of a boostback from ~1.7 km/s downrange and a landing); the tower's arms (steel rails, shock-
// absorbed). Not a model of any flight: the timeline is compared with the public profile only.
#include "albert/v1/engine.pb.h"
#include "gravity_turn.hpp"
#include <array>
#include <cmath>
#include <string>
#include <vector>
namespace albert::example::starship {
namespace api=::albert::v1;
constexpr double pi=3.14159265358979323846,deg=pi/180,g0=9.80665,radius=4.5,shipLength=50.3,boosterLength=71;
constexpr double raptorSea=73.5e6/33,raptorVacuum=raptorSea*347/327,raptorExhaust=347*g0,raptorExit=(raptorVacuum-raptorSea)/101325,rvacThrust=(12.25e6-3*raptorSea)/3,rvacExhaust=380*g0;
constexpr double boosterEmpty=275e3,boosterPropellant=3400e3,shipEmpty=100e3,shipPropellant=1200e3,engineMass=1.6e3,reserve=450e3;
// A solid cylinder from `from` to `to` m aft of the nose: mass, its centre, and principal moments about that centre.
struct Part {double mass,from,to;};
struct MassProperties {double mass=0,cg=0;double ix=0,iy=0;};
inline MassProperties combine(const std::vector<Part>& parts,double shift=0){ // shift: added to every position.
    MassProperties m;for(const auto& p:parts){m.mass+=p.mass;m.cg+=p.mass*((p.from+p.to)/2+shift);}if(m.mass>0)m.cg/=m.mass;
    for(const auto& p:parts){const double l=p.to-p.from,c=(p.from+p.to)/2+shift-m.cg;m.ix+=.5*p.mass*radius*radius;m.iy+=p.mass*((3*radius*radius+l*l)/12+c*c);}
    return m;
}
// The ship's parts and the booster's, from each one's own nose; full or empty of propellant.
inline std::vector<Part> ship(bool full){std::vector<Part> p{{shipEmpty-6*engineMass,0,shipLength},{6*engineMass,shipLength-3.3,shipLength}};
    if(full){p.push_back({.78*shipPropellant,30,46});p.push_back({.22*shipPropellant,14,30});}return p;}
inline std::vector<Part> booster(bool full){std::vector<Part> p{{boosterEmpty-33*engineMass,0,boosterLength},{33*engineMass,boosterLength-3.3,boosterLength}};
    if(full){p.push_back({2650e3,35,68});p.push_back({750e3,6,35});}return p;}
inline void put(api::Vector3* o,double x,double y,double z){o->set_x(x);o->set_y(y);o->set_z(z);}
// Coefficients on the 9 m disc (π r²) for a cylinder of this length: axial drag nose-first or base-first by Mach, and the
// crossflow (Cd 1.2 on the side, L D / π r² times) for lift, side force and drag at any angle.
inline void aerodynamics(api::StageDefinition* st,double length){
    auto* a=st->mutable_aerodynamics();a->set_reference_area_meters2(pi*radius*radius);a->set_reference_length_meters(length);a->set_reference_span_meters(2*radius);
    a->set_pitch_damping(-20);a->set_yaw_damping(-20);a->set_roll_damping(-1);st->set_aerodynamic_reference_meters(length/2);
    const std::array<double,5> mach{0,.8,1.2,2,5},nose{.35,.4,.65,.5,.35},base{1,1.1,1.4,1.2,1};const double side=1.2*length*2*radius/(pi*radius*radius);
    std::vector<double> total,signed_;for(int i=0;i<=18;++i)total.push_back(i*10);for(int i=-18;i<=18;++i)signed_.push_back(i*10);
    auto table=[&](api::AeroTable* t,const std::vector<double>& angles,auto f){for(double m:mach)t->add_mach(m);for(double x:angles)t->add_angle_degrees(x);
        for(std::size_t i=0;i<mach.size();++i)for(double x:angles)t->add_values(f(i,x*deg));};
    table(a->mutable_drag(),total,[&](std::size_t i,double x){const double c=std::cos(x),s=std::sin(x);return (c>=0?nose[i]:base[i])*c*c+side*s*s*s;});
    table(a->mutable_lift(),signed_,[&](std::size_t,double x){const double s=std::sin(x);return side*std::abs(s)*s*std::cos(x);});
    table(a->mutable_side(),signed_,[&](std::size_t,double x){const double s=std::sin(x);return -side*std::abs(s)*s;});
}
inline void raptors(api::StageDefinition* st,double base,bool boosterRings){
    auto add=[&](const std::string& name,double r,double angle,double gimbal,double thrust,double exhaust,double exit){auto* e=st->add_engines();e->set_name(name);
        put(e->mutable_position_meters(),base,r*std::cos(angle),r*std::sin(angle));e->set_vacuum_thrust_newtons(thrust);e->set_exhaust_velocity_meters_per_second(exhaust);
        e->set_exit_area_meters2(exit);e->set_min_throttle(.4);e->set_gimbal_radians(gimbal);e->set_gimbal_rate_radians_per_second(20*deg);};
    if(boosterRings){ // 3 centre and 10 middle gimballed, 20 outer fixed.
        for(int i=0;i<3;++i)add("centre "+std::to_string(i+1),.75,i*120*deg,15*deg,raptorVacuum,raptorExhaust,raptorExit);
        for(int i=0;i<10;++i)add("middle "+std::to_string(i+1),2.2,i*36*deg,15*deg,raptorVacuum,raptorExhaust,raptorExit);
        for(int i=0;i<20;++i)add("outer "+std::to_string(i+1),3.9,(i*18+9)*deg,0,raptorVacuum,raptorExhaust,raptorExit);
    }else{ // The ship: 3 sea-level Raptors gimballed in the centre, 3 Raptor Vacuum fixed outside.
        for(int i=0;i<3;++i)add("raptor "+std::to_string(i+1),1,i*120*deg,15*deg,raptorVacuum,raptorExhaust,raptorExit);
        for(int i=0;i<3;++i)add("vacuum "+std::to_string(i+1),3.2,(i*120+60)*deg,0,rvacThrust,rvacExhaust,4.15);
    }
}
inline void mass(api::StageDefinition* st,const MassProperties& full,const MassProperties& empty,double propellant,double length){
    st->set_propellant_kilograms(propellant);st->set_mass_full_kilograms(full.mass);st->set_mass_empty_kilograms(empty.mass);st->set_cg_full_meters(full.cg);st->set_cg_empty_meters(empty.cg);
    put(st->mutable_inertia_full_kilogram_meters2(),full.ix,full.iy,full.iy);put(st->mutable_inertia_empty_kilogram_meters2(),empty.ix,empty.iy,empty.iy);st->set_length_meters(length);st->set_nose_radius_meters(radius);
}
// The stack (stage 0: booster burning with the full ship on top; stage 1: the ship) and the booster alone (spawned at staging).
inline api::VehicleDefinition stack(){
    api::VehicleDefinition v;v.set_name("Starship-class stack (approximate, public figures)");v.set_diameter_meters(2*radius);v.set_attitude_torque_newton_meters(5e7);
    auto parts=[](bool boosterFull){auto p=ship(true);for(auto x:booster(boosterFull)){x.from+=shipLength;x.to+=shipLength;p.push_back(x);}return p;};
    auto* s0=v.add_stages();s0->set_name("Super Heavy and ship");mass(s0,combine(parts(true)),combine(parts(false)),boosterPropellant,shipLength+boosterLength);
    raptors(s0,shipLength+boosterLength,true);aerodynamics(s0,shipLength+boosterLength);
    auto* s1=v.add_stages();s1->set_name("Ship");mass(s1,combine(ship(true)),combine(ship(false)),shipPropellant,shipLength);raptors(s1,shipLength,false);aerodynamics(s1,shipLength);
    return v;
}
constexpr double pinAt=6,pinOut=radius+1,finLimit=30*deg,finMoment=.77; // Hard points by the grid fins: 6 m below the nose, 5.5 m off the axis (body ±y).
inline api::VehicleDefinition superHeavy(){
    api::VehicleDefinition v;v.set_name("Super Heavy-class booster (approximate)");v.set_diameter_meters(2*radius);
    auto* s=v.add_stages();s->set_name("Booster");mass(s,combine(booster(true)),combine(booster(false)),boosterPropellant,boosterLength);raptors(s,boosterLength,true);aerodynamics(s,boosterLength);
    for(const double y:{-pinOut,pinOut})put(s->add_hard_points_meters(),pinAt,y,0);
    // Four grid fins by the nose as two pairs, always out: pitch and yaw moments by deflection in engines-first flight (0.77 a
    // radian on the stage's references: a pair's ~18 m² at C_Nδ 2.5, 39 m from the centre of mass) and 0.2 of drag a pair.
    for(const bool pitch:{true,false}){auto* f=s->add_surfaces();f->set_name(pitch?"grid fins pitch":"grid fins yaw");f->set_limit_radians(finLimit);f->set_rate_radians_per_second(60*deg);
        auto* m=pitch?f->mutable_pitch():f->mutable_yaw();m->add_mach(0);m->add_angle_degrees(0);m->add_deflection_degrees(-30);m->add_deflection_degrees(30);m->add_values(-finMoment*finLimit);m->add_values(finMoment*finLimit);
        auto* d=f->mutable_drag();d->add_mach(0);d->add_angle_degrees(0);d->add_deflection_degrees(0);d->add_values(.2);}
    // Cold-gas RCS 3 m below the nose: on the ±y sides pushing ±z and on the ±z sides pushing ±y (pitch, yaw and, in opposed
    // pairs, roll), 30 kN each.
    for(const double side:{-1.,1.})for(const double push:{-1.,1.}){
        auto* a=s->add_thrusters();a->set_name("rcs y");put(a->mutable_position_meters(),3,side*radius,0);put(a->mutable_direction(),0,0,push);a->set_thrust_newtons(3e4);a->set_exhaust_velocity_meters_per_second(700);
        auto* b=s->add_thrusters();b->set_name("rcs z");put(b->mutable_position_meters(),3,0,side*radius);put(b->mutable_direction(),0,push,0);b->set_thrust_newtons(3e4);b->set_exhaust_velocity_meters_per_second(700);}
    return v;
}
// ---- The booster's return, from what the flight API reports (ECEF, NED attitude, rates, mass, propellant, contact) ----
struct Mat {Vec r0,r1,r2;}; // Rows.
inline Vec mul(const Mat& m,Vec v){return{dot(m.r0,v),dot(m.r1,v),dot(m.r2,v)};}
inline Vec tmul(const Mat& m,Vec v){return m.r0*v.x+m.r1*v.y+m.r2*v.z;} // Transposed.
inline Vec unit(Vec v){const double n=norm(v);return n>0?v*(1/n):v;}
inline Mat nedOf(Vec p){ // ECEF to NED at p (a sphere's: good enough for steering).
    const Vec u=unit(p);const double lat=std::asin(u.z),lon=std::atan2(u.y,u.x);
    return{{-std::sin(lat)*std::cos(lon),-std::sin(lat)*std::sin(lon),std::cos(lat)},{-std::sin(lon),std::cos(lon),0},{-u.x,-u.y,-u.z}};
}
inline Mat bodyToNed(double w,double x,double y,double z){ // Rows of the body-to-NED matrix.
    return{{1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)},{2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)},{2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)}};
}
struct State {Vec position,velocity,rate,contact;Mat toBody;double mass=0,propellant=0,thrust=0,height=0,q=0;}; // toBody: ECEF to body; q: dynamic pressure (Pa).
struct Command {std::vector<int> ignite,cutoff;double throttle=-1,gimbalPitch=0,gimbalYaw=0,finPitch=0,finYaw=0;Vec torque;bool torqueOn=false;};
// The booster's way back from staging to a catch, at 50 Hz from what the flight API reports: a flip on 3 engines, the
// boostback on 13 (3 for its last km) until the predicted landing is the tower, the coast engines first (RCS above the air,
// the grid fins steering the landing onto the tower in it), the landing burn on 13 then 3, a hover over the tower aligned
// with the arms, and down at 1 m/s until the pins rest on them.
class BoosterReturn {
public:
    enum class Phase {wait,flip,boostback,coast,landing,hover,descend,caught};
    // site: the tower's base (ECEF, m); arms: the arms' height over it (m); rest: the pins' over the arms when they rest (m).
    BoosterReturn(Vec site,double arms,double rest):site_(site),arms_(arms),rest_(rest){const Mat n=nedOf(site);north_=n.r0;up_=n.r2*-1;}
    Phase phase()const{return phase_;}
    double predicted()const{return miss_;} // The latest predicted landing's distance from the tower (m).
    double catchHeight(const State& s)const{return arms_+rest_-(cg(s.propellant)-pinAt);} // Of the centre of mass, pins on the arms, upright.
    Command step(double t,const State& s){
        Command c;const Vec g=s.position*(-earth.mu/std::pow(norm(s.position),3));const Vec rel=s.position+site_*-1;
        const double over=dot(rel,up_);const Vec flat=rel+up_*-over;
        switch(phase_){
        case Phase::wait:if(t-start_>2){c.ignite={1,2,3};c.throttle=.4;phase_=Phase::flip;}hold(c,s,unit(s.velocity));break; // Clear of the ship, then 3 engines.
        case Phase::flip:{ // Turn to point the thrust from the predicted landing back to the tower, boostbackPitch above the horizon.
            const Vec d=boostbackDirection(landing(s));aim(c,s,d);if(dot(bodyX(s),d)>std::cos(8*deg)){for(int i=4;i<=13;++i)c.ignite.push_back(i);c.throttle=1;phase_=Phase::boostback;back_=d;}break;}
        case Phase::boostback:{const Vec e=landing(s);aim(c,s,boostbackDirection(e));miss_=norm(e);
            if(engines_==13&&dot(e,back_)>-5e3){for(int i=4;i<=13;++i)c.cutoff.push_back(i);engines_=3;} // The last few km on 3, to cut finely.
            if(dot(e,back_)>=0||s.propellant<120e3){for(int i=1;i<=3;++i)c.cutoff.push_back(i);if(engines_==13)for(int i=4;i<=13;++i)c.cutoff.push_back(i);engines_=13;phase_=Phase::coast;}break;}
        case Phase::coast:{ // Engines first through the descent: held by the RCS above the air, then the grid fins tilt the body so its
            // crossflow lift (away from the nose's tilt) carries the impact onto the tower; the landing burn when the stop needs it.
            if(age_--<=0){aimed_=landing(s,&seconds_);age_=25;}const Vec e=aimed_;const double seconds=seconds_;miss_=norm(e);const Vec vhat=unit(s.velocity);
            if(s.q<2e3)hold(c,s,vhat*-1);
            else{Vec want=e*(-6/std::max(seconds*seconds,1.));want=want+vhat*-dot(want,vhat);
                const double a=norm(want),side=1.2*boosterLength*2*radius/(pi*radius*radius),tilt=std::min(std::sqrt(a*s.mass/(s.q*pi*radius*radius*side)),std::sin(12*deg));
                steer(c,s,a>0?unit(vhat*-std::sqrt(1-tilt*tilt)+unit(want)*-tilt):vhat*-1);}
            if(over<40e3&&gateHeight(s,13*.9*raptorSea/s.mass)<=catchHeight(s)+hoverAbove+gate(s)+50){for(int i=1;i<=13;++i)c.ignite.push_back(i);c.throttle=.9;phase_=Phase::landing;}break;}
        case Phase::landing:{ // 13 engines to the gate (gateSpeed at the height 3 engines stop from), then 3 to rest at the hover
            // point: the constant deceleration that does it net of the drag; across, E-guidance onto the tower's axis over the
            // same time on 13 engines, the braking law on 3.
            const double above=over-catchHeight(s)-hoverAbove,vz=dot(s.velocity,up_),floor=engines_==13?gate(s):0,v2=engines_==13?gateSpeed*gateSpeed:0;
            if(engines_==13&&(-vz<=gateSpeed||above<=floor)){for(int i=4;i<=13;++i)c.cutoff.push_back(i);engines_=3;}
            const Vec across=s.velocity+up_*-vz;const double to=above-floor;
            const double brake=to>1&&vz<0?std::max(vz*vz-v2,0.)/(2*to):0,tgo=std::max(vz<0&&above>1?(engines_==13?2*to/(gateSpeed-vz)+2*gate(s)/gateSpeed:2*above/-vz):1.,2.);
            const double up=std::max(brake+norm(g)-drag(s)*(norm(s.velocity)>0?-vz/norm(s.velocity):0),0.);
            // At most the tilt whose thrust across beats the crossflow lift it raises (T sin θ - q S C sin²θ peaks at sin θ = T/2qSC).
            const double side0=1.2*boosterLength*2*radius/(pi*radius*radius),best=std::asin(std::min(1.,std::max(s.thrust,1.)/(2*std::max(s.q,1.)*pi*radius*radius*side0)));
            const double tilt=std::tan(std::min(20*deg,best))*std::max(up,norm(g));Vec side=engines_==13?flat*(-6/(tgo*tgo))+across*(-4/tgo):toAxis(s,flat);
            if(norm(side)>tilt)side=side*(tilt/norm(side));
            const Vec a=up_*up+side;c.throttle=std::clamp(s.mass*norm(a)/(engines_*raptorSea),.4,1.);aim(c,s,unit(a));if(engines_==3)roll(c,s);
            if(engines_==3&&(above<2||vz>-1))phase_=Phase::hover;break;}
        case Phase::hover:case Phase::descend:{ // Over the tower, then down at 1 m/s until the pins rest on the arms.
            if(phase_==Phase::hover&&norm(flat)<.4&&norm(s.velocity+up_*-dot(s.velocity,up_))<.3&&std::abs(dot(s.velocity,up_))<.5){phase_=Phase::descend;descend_=over;}
            if(phase_==Phase::descend)descend_-=dt;
            const double height=phase_==Phase::descend?std::max(descend_,catchHeight(s)-1):catchHeight(s)+hoverAbove;
            const double vz=dot(s.velocity,up_),vertical=std::max((phase_==Phase::descend?.7*(-1-vz):.2*(height-over)-.8*vz)+norm(g),0.);
            Vec side=toAxis(s,flat);const double limit=std::tan(12*deg)*vertical;if(norm(side)>limit)side=side*(limit/norm(side));
            const Vec thrust=up_*vertical+side;
            c.throttle=std::clamp(s.mass*norm(thrust)/(3*raptorSea),.4,1.);aim(c,s,unit(thrust));roll(c,s);
            if(norm(s.contact)>0){c.throttle=.4;phase_=Phase::caught;caught_=t;}break;}
        case Phase::caught: // The engines at their least for a second while the arms take the load, then off.
            if(t-caught_>1&&!off_){for(int i=1;i<=3;++i)c.cutoff.push_back(i);off_=true;}c.torqueOn=true;c.torque={};break;
        }
        return c;
    }
    void start(double t){start_=t;}
    double dt=.02,hoverAbove=10,gateSpeed=80,boostbackPitch=-5*deg;
private:
    static double cg(double propellant){const auto f=combine(booster(true)),e=combine(booster(false));const double x=std::clamp(propellant/boosterPropellant,0.,1.);return e.cg+(f.cg-e.cg)*x;}
    static double pitchInertia(double propellant){const auto f=combine(booster(true)),e=combine(booster(false));const double x=std::clamp(propellant/boosterPropellant,0.,1.);return e.iy+(f.iy-e.iy)*x;}
    static Vec bodyX(const State& s){return tmul(s.toBody,Vec{1,0,0});}
    Vec boostbackDirection(Vec miss)const{const Vec h=unit(miss*-1);return h*std::cos(boostbackPitch)+up_*std::sin(boostbackPitch);} // miss: horizontal.
    // Thrust along d (ECEF) by the gimbal, the gain from the thrust's lever on the inertia (natural frequency 1.2 rad/s).
    void aim(Command& c,const State& s,Vec d)const{
        const Vec e=mul(s.toBody,cross(tmul(s.toBody,Vec{1,0,0}),d)),w=s.rate;const double lever=std::max(boosterLength-cg(s.propellant),1.),gain=pitchInertia(s.propellant)/std::max(s.thrust*lever,1.);
        c.gimbalPitch=std::clamp(gain*(1.44*e.y-2*w.y),-15*deg,15*deg);c.gimbalYaw=std::clamp(gain*(1.44*e.z-2*w.z),-15*deg,15*deg);
        c.torqueOn=true;c.torque={-2e6*w.x,0,0}; // The RCS keeps the roll still while the engines steer.
    }
    void hold(Command& c,const State& s,Vec d)const{ // By the RCS alone (engines off): body x along d, no roll.
        const Vec e=mul(s.toBody,cross(tmul(s.toBody,Vec{1,0,0}),d)),w=s.rate;const double i=pitchInertia(s.propellant);
        c.torqueOn=true;c.torque={-2e6*w.x,i*(.04*e.y-.4*w.y),i*(.04*e.z-.4*w.z)};
    }
    void roll(Command& c,const State& s)const{ // Body y toward north (the pins over the arms), about the upright axis.
        const Vec y=tmul(s.toBody,Vec{0,1,0}),z=tmul(s.toBody,Vec{0,0,1});const double angle=std::atan2(dot(z,north_),dot(y,north_));
        c.torque.x=8e6*angle-4e7*s.rate.x;c.torqueOn=true; // angle: minus the roll from there.
    }
    // Across onto the tower's axis without overshooting: toward it at min(0.15 d, √(2 · 1 m/s² · d)), that speed's error at 0.4/s
    // (a quarter of the attitude loop's bandwidth).
    Vec toAxis(const State& s,Vec flat)const{
        const double d=norm(flat);const Vec across=s.velocity+up_*-dot(s.velocity,up_),want=d>0?flat*(-std::min(.15,std::sqrt(2/d))):Vec{};
        return (want+across*-1)*.4;
    }
    void steer(Command& c,const State& s,Vec d)const{ // Body x along d by the grid fins (engines off, in the air), the roll by the RCS.
        const Vec e=mul(s.toBody,cross(bodyX(s),d)),w=s.rate;const double i=pitchInertia(s.propellant),k=std::max(s.q*pi*radius*radius*boosterLength*finMoment,1.);
        c.finPitch=std::clamp(i*(.3*e.y-.8*w.y)/k,-finLimit,finLimit);c.finYaw=std::clamp(i*(.3*e.z-.8*w.z)/k,-finLimit,finLimit);c.torqueOn=true;c.torque={-2e6*w.x,0,0};
    }
    // Where a burn lit now (thrust `accel` m/s² against the velocity) slows the fall to gateSpeed: the height over the tower's base.
    double gateHeight(const State& s,double accel)const{
        Vec r=s.position,v=s.velocity;fly(r,v,s.mass,accel,.1,[&]{return -dot(v,up_)<=gateSpeed||dot(r+site_*-1,up_)<0;});return dot(r+site_*-1,up_);
    }
    double gate(const State& s)const{return gateSpeed*gateSpeed/(2*(.8*3*raptorSea/s.mass-g0));} // The 3 engines' stop from gateSpeed.
    // Where the fall reaches the gate: ballistic until a burn of 13 engines at 90% lit there would reach it at its height (checked
    // every 2 s), then that burn: horizontal from the tower (m); seconds: the time to it.
    Vec landing(const State& s,double* seconds=nullptr)const{
        const double accel=13*.9*raptorSea/s.mass,low=catchHeight(s)+hoverAbove+gate(s)+50;Vec r=s.position,v=s.velocity;int n=0,m=0;
        auto height=[&](Vec p){return dot(p+site_*-1,up_);};auto gated=[&](Vec& p,Vec& u){fly(p,u,s.mass,accel,.25,[&]{++m;return -dot(u,up_)<=gateSpeed||height(p)<0;});};
        fly(r,v,s.mass,0,.5,[&]{if(++n%4==0&&dot(v,up_)<0&&height(r)<40e3){Vec p=r,u=v;const int k=m;gated(p,u);m=k;if(height(p)<=low)return true;}return height(r)<0&&dot(v,up_)<0;});
        gated(r,v);if(seconds)*seconds=.5*(n-1)+.25*m;const Vec rel=r+site_*-1;return rel+up_*-dot(rel,up_);
    }
    // The standard atmosphere's density (troposphere, then the stratosphere's exponential) and the drag coefficient engines first
    // (the stage's base table and the fins').
    static double density(double h){return h<11e3?1.225*std::pow(1-2.25577e-5*std::max(h,0.),4.2559):h<1e5?.36392*std::exp(-(h-11e3)/6341.6):0.;}
    static double baseDrag(double v,double h){const double m=v/(h<11e3?340.3-.0039*h:295.),x[]{0,.8,1.2,2,5},y[]{1,1.1,1.4,1.2,1};
        for(int i=0;i<4;++i)if(m<=x[i+1])return .4+y[i]+(y[i+1]-y[i])*(m-x[i])/(x[i+1]-x[i]);return 1.4;}
    double drag(const State& s)const{const double h=norm(s.position)-norm(site_),v=norm(s.velocity);return .5*density(h)*baseDrag(v,h)*pi*radius*radius*v*v/s.mass;}
    // RK4 in ECEF (gravity, the Earth's turning, drag engines first, thrust against the air) until done.
    template<class Done> void fly(Vec& r,Vec& v,double m,double accel,double h,Done done)const{
        const Vec w{0,0,7.292115e-5};const double area=pi*radius*radius;
        auto acc=[&](Vec p,Vec u){const double rn=norm(p),ht=rn-norm(site_),speed=norm(u);
            return p*(-earth.mu/(rn*rn*rn))+cross(w,u)*-2.+cross(w,cross(w,p))*-1.+u*(-.5*density(ht)*baseDrag(speed,ht)*area*speed/m)+(speed>0?u*(-accel/speed):Vec{});};
        for(int i=0;i<6000&&!done();++i){
            const Vec a1=acc(r,v),v1=v+a1*(h/2),r1=r+v*(h/2),a2=acc(r1,v1),v2=v+a2*(h/2),r2=r+v1*(h/2),a3=acc(r2,v2),v3=v+a3*h,r3=r+v2*h,a4=acc(r3,v3);
            r=r+(v+v1*2+v2*2+v3)*(h/6);v=v+(a1+a2*2+a3*2+a4)*(h/6);}
    }
    Phase phase_=Phase::wait;Vec site_,north_,up_,back_,aimed_;double arms_,rest_,start_=0,miss_=0,descend_=0,seconds_=0,caught_=0;int engines_=13,age_=0;bool off_=false;
};
}
