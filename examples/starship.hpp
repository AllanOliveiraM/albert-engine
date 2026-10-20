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
#include <algorithm>
#include <bit>
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
// Those tables as Albert reads them (linear between the 10° and Mach nodes, held past the ends), for a guidance's predictions.
inline double lerp(const double* x,const double* y,int n,double v){if(v<=x[0])return y[0];for(int i=0;i+1<n;++i)if(v<=x[i+1])return y[i]+(y[i+1]-y[i])*(v-x[i])/(x[i+1]-x[i]);return y[n-1];}
inline double liftOf(double length,double alpha){ // alpha: rad, signed.
    const double side=1.2*length*2*radius/(pi*radius*radius),a=std::clamp(alpha/deg,-180.,180.),lo=std::floor(a/10)*10,hi=std::min(lo+10,180.);
    auto f=[&](double x){const double r=x*deg,s=std::sin(r);return side*std::abs(s)*s*std::cos(r);};return hi>lo?f(lo)+(f(hi)-f(lo))*(a-lo)/(hi-lo):f(lo);
}
inline double dragOf(double length,double angle,double mach){ // angle: the total one (rad, 0-180°).
    constexpr double machs[]{0,.8,1.2,2,5},nose[]{.35,.4,.65,.5,.35},base[]{1,1.1,1.4,1.2,1};
    const double side=1.2*length*2*radius/(pi*radius*radius),a=std::clamp(std::abs(angle)/deg,0.,180.),lo=std::floor(a/10)*10,hi=std::min(lo+10,180.);
    const double n=lerp(machs,nose,5,mach),b=lerp(machs,base,5,mach);
    auto f=[&](double x){const double r=x*deg,c=std::cos(r),s=std::sin(r);return (c>=0?n:b)*c*c+side*s*s*s;};return hi>lo?f(lo)+(f(hi)-f(lo))*(a-lo)/(hi-lo):f(lo);
}
inline void raptors(api::StageDefinition* st,double base,bool boosterRings){
    auto add=[&](const std::string& name,double r,double angle,double gimbal,double thrust,double exhaust,double exit){auto* e=st->add_engines();e->set_name(name);
        put(e->mutable_position_meters(),base,r*std::cos(angle),r*std::sin(angle));e->set_vacuum_thrust_newtons(thrust);e->set_exhaust_velocity_meters_per_second(exhaust);
        e->set_exit_area_meters2(exit);e->set_min_throttle(.4);e->set_gimbal_radians(gimbal);e->set_gimbal_rate_radians_per_second(20*deg);};
    if(boosterRings){ // 3 centre and 10 middle gimballed, 20 outer fixed.
        for(int i=0;i<3;++i)add("centre "+std::to_string(i+1),.75,i*120*deg,15*deg,raptorVacuum,raptorExhaust,raptorExit);
        for(int i=0;i<10;++i)add("middle "+std::to_string(i+1),2.2,i*36*deg,15*deg,raptorVacuum,raptorExhaust,raptorExit);
        for(int i=0;i<20;++i)add("outer "+std::to_string(i+1),3.9,(i*18+9)*deg,0,raptorVacuum,raptorExhaust,raptorExit);
    }else{ // The ship: 3 sea-level Raptors gimballed in the centre (the first on body z, square to the catch pins: hovering on it
        // alone the ship leans about the pins' line, both pins level), 3 Raptor Vacuum fixed outside between them.
        for(int i=0;i<3;++i)add("raptor "+std::to_string(i+1),1,(90+i*120)*deg,15*deg,raptorVacuum,raptorExhaust,raptorExit);
        for(int i=0;i<3;++i)add("vacuum "+std::to_string(i+1),3.2,(30+i*120)*deg,0,rvacThrust,rvacExhaust,4.15);
    }
}
inline void mass(api::StageDefinition* st,const MassProperties& full,const MassProperties& empty,double propellant,double length){
    st->set_propellant_kilograms(propellant);st->set_mass_full_kilograms(full.mass);st->set_mass_empty_kilograms(empty.mass);st->set_cg_full_meters(full.cg);st->set_cg_empty_meters(empty.cg);
    put(st->mutable_inertia_full_kilogram_meters2(),full.ix,full.iy,full.iy);put(st->mutable_inertia_empty_kilogram_meters2(),empty.ix,empty.iy,empty.iy);st->set_length_meters(length);st->set_nose_radius_meters(radius);
}
// The ship's return hardware: four flaps, a cold-gas RCS, the heat shield's tiles and a steel panel as thermal nodes, and catch
// pins. A flap is a drag on its area (C_N 1.2, at |sin α| of the flow and sin(45° + δ) of its opening: δ -45° folded, +45°
// square to the flow) placed where it is, so its pitch, roll and yaw come from there. Approximate: the flaps' places and
// areas, 10 kN thrusters, 25 mm tiles (1700 K) and 4 mm steel (1100 K).
struct Flap {const char* name;double x,y,area;};
constexpr Flap flaps[]{{"flap forward left",8,-radius,25},{"flap forward right",8,radius,25},{"flap aft left",46,-radius,50},{"flap aft right",46,radius,50}};
constexpr double flapCn=1.2,shipPinAt=12;
inline void shipSystems(api::StageDefinition* s){
    const double area=pi*radius*radius;
    for(const auto& f:flaps){auto* x=s->add_surfaces();x->set_name(f.name);x->set_limit_radians(45*deg);x->set_rate_radians_per_second(30*deg);put(x->mutable_position_meters(),f.x,f.y,0);
        auto* t=x->mutable_drag();t->add_mach(0);for(int a=-180;a<=180;a+=15)t->add_angle_degrees(a);for(int d=-45;d<=45;d+=15)t->add_deflection_degrees(d);
        for(int a=-180;a<=180;a+=15)for(int d=-45;d<=45;d+=15)t->add_values(flapCn*f.area/area*std::sin((45+d)*deg)*std::abs(std::sin(a*deg)));}
    for(const double x:{4.,46.})for(const double side:{-1.,1.})for(const double push:{-1.,1.}){
        auto* a=s->add_thrusters();a->set_name("rcs y");put(a->mutable_position_meters(),x,side*radius,0);put(a->mutable_direction(),0,0,push);a->set_thrust_newtons(1e4);a->set_exhaust_velocity_meters_per_second(700);
        auto* b=s->add_thrusters();b->set_name("rcs z");put(b->mutable_position_meters(),x,0,side*radius);put(b->mutable_direction(),0,push,0);b->set_thrust_newtons(1e4);b->set_exhaust_velocity_meters_per_second(700);}
    for(int i=0;i<6;++i){auto* n=s->add_thermal_nodes();n->set_name("tile "+std::to_string(i+1));put(n->mutable_position_meters(),4+8*i,0,radius);put(n->mutable_normal(),0,0,1);
        n->set_area_meters2(1);n->set_heat_capacity_joules_per_kelvin(6e3);n->set_emissivity(.85);n->set_absorptivity(.85);n->set_conductance_watts_per_kelvin(1);n->set_limit_kelvin(1700);}
    auto* n=s->add_thermal_nodes();n->set_name("leeward steel");put(n->mutable_position_meters(),25,0,-radius);put(n->mutable_normal(),0,0,-1);
    n->set_area_meters2(1);n->set_heat_capacity_joules_per_kelvin(1.58e4);n->set_emissivity(.3);n->set_absorptivity(.4);n->set_limit_kelvin(1100);
    for(const double y:{-(radius+1),radius+1})put(s->add_hard_points_meters(),shipPinAt,y,0);
}
// The booster's grid fins as on V3 (2026): three, ~50% larger than the four before (27 m² each, C_Nδ 2.5) and lower, their
// spindles 7.5 m below the top, two over the catch fittings (body ±y) and one on -z. Three and asymmetric is what SpaceX
// shows; this layout is an assumption. Their normal force per radian on the stage's reference area, and the hard points
// (catch fittings) 9 m below the top, 5.5 m off the axis, under the ±y fins.
struct Fin {double y,z;}; // The spindle's direction (body y, z).
constexpr Fin fins[]{{1,0},{-1,0},{0,-1}};
constexpr double pinAt=9,pinOut=radius+1,finAt=7.5,finSpan=3.7,finWidth=6.1,finLimit=30*deg,finSlope=27*2.5/(pi*radius*radius),finDrag=.15;
// How they look (the scene only), from each one's nose `at` m along its stage: stainless steel; the ship's nose a tangent ogive
// (11 m) in sections, black tiles on its windward half (+z), four flaps (hinged on its sides along x: +45° square to the
// flow, -45° folded to the lee) turning with surfaces 1-4 when `moving`, else folded; the booster's vented hot-staging ring
// and its three grid fins (on their radial spindles, with surfaces 1-3 when `moving`); both vehicles' catch pins.
inline void put(api::Vector3* o,const Vec& v){put(o,v.x,v.y,v.z);}
inline void section(api::StageAppearance* a,double from,double to,double r0,double r1,std::uint32_t colour,double tiles){
    auto* x=a->add_sections();x->set_from_meters(from);x->set_to_meters(to);x->set_radius_from_meters(r0);x->set_radius_to_meters(r1);x->set_color(colour);x->set_tiles_radians(tiles);}
inline void hinged(api::StageAppearance* a,const char* name,Vec hinge,Vec axis,Vec out,double span,double width,double thick,unsigned surface,double gain,double offset,std::uint32_t colour,bool grid=false){
    auto* x=a->add_parts();x->set_name(name);put(x->mutable_hinge_meters(),hinge);put(x->mutable_axis(),axis);put(x->mutable_outward(),out);x->set_span_meters(span);x->set_width_meters(width);
    x->set_thickness_meters(thick);x->set_surface(surface);x->set_gain(gain);x->set_offset_radians(offset);x->set_color(colour);x->set_grid(grid);}
constexpr std::uint32_t steel=0xc9c9c6,shield=0x161616,vents=0x3c3c3c;
inline void shipLook(api::StageAppearance* a,double at,bool moving){
    const double l=11,rho=(radius*radius+l*l)/(2*radius);auto r=[&](double x){return std::sqrt(rho*rho-(l-x)*(l-x))+radius-rho;};
    const double cuts[]{0,.4,1.2,2.5,4,6,8.5,11};for(int i=0;i+1<8;++i)section(a,at+cuts[i],at+cuts[i+1],std::max(r(cuts[i]),.05),r(cuts[i+1]),steel,pi);
    section(a,at+l,at+shipLength,radius,radius,steel,pi);
    for(const double side:{-1.,1.}){ // Forward flaps on the nose, aft ones by the engines; the left one turns the other way.
        const unsigned fwd=side<0?1:2,aft=side<0?3:4;const double g=moving?side:0,o=moving?-side*45*deg:-side*90*deg; // Folded: up to -z.
        hinged(a,side<0?"flap forward left":"flap forward right",{at+8,side*r(8),0},{1,0,0},{0,side,0},7,3.4,.35,moving?fwd:0,g,o,steel);
        hinged(a,side<0?"flap aft left":"flap aft right",{at+44.5,side*radius,0},{1,0,0},{0,side,0},10,5,.4,moving?aft:0,g,o,steel);
        hinged(a,"pin",{at+shipPinAt,side*radius,0},{0,side,0},{1,0,0},1.2,1,1,0,0,0,vents);}
}
inline void boosterLook(api::StageAppearance* a,double at,bool moving){
    section(a,at,at+2,radius,radius,vents,0);section(a,at+2,at+boosterLength,radius,radius,steel,0);
    for(unsigned i=0;i<3;++i){const Fin f=fins[i];const double r=radius+finSpan/2; // +δ turns it about its spindle, pushing along body x × spindle.
        hinged(a,"grid fin",{at+finAt,f.y*r,f.z*r},{0,f.y,f.z},{0,-f.z,f.y},finSpan,finWidth,.6,moving?i+1:0,1,0,0x2e2e2e,true);}
    for(const double side:{-1.,1.})hinged(a,"pin",{at+pinAt,side*radius,0},{0,side,0},{1,0,0},1.2,1,1,0,0,0,vents);
}
// The stack (stage 0: booster burning with the full ship on top; stage 1: the ship), the ship alone and the booster alone.
inline api::VehicleDefinition stack(){
    api::VehicleDefinition v;v.set_name("Starship-class stack (approximate, public figures)");v.set_diameter_meters(2*radius);v.set_attitude_torque_newton_meters(5e7);
    auto parts=[](bool boosterFull){auto p=ship(true);for(auto x:booster(boosterFull)){x.from+=shipLength;x.to+=shipLength;p.push_back(x);}return p;};
    auto* s0=v.add_stages();s0->set_name("Super Heavy and ship");mass(s0,combine(parts(true)),combine(parts(false)),boosterPropellant,shipLength+boosterLength);
    raptors(s0,shipLength+boosterLength,true);aerodynamics(s0,shipLength+boosterLength);shipLook(s0->mutable_appearance(),0,false);boosterLook(s0->mutable_appearance(),shipLength,false);
    auto* s1=v.add_stages();s1->set_name("Ship");mass(s1,combine(ship(true)),combine(ship(false)),shipPropellant,shipLength);raptors(s1,shipLength,false);aerodynamics(s1,shipLength);shipSystems(s1);shipLook(s1->mutable_appearance(),0,true);
    return v;
}
inline api::VehicleDefinition shipAlone(){
    api::VehicleDefinition v;v.set_name("Starship-class ship (approximate)");v.set_diameter_meters(2*radius);
    auto* s=v.add_stages();s->set_name("Ship");mass(s,combine(ship(true)),combine(ship(false)),shipPropellant,shipLength);raptors(s,shipLength,false);aerodynamics(s,shipLength);shipSystems(s);
    shipLook(s->mutable_appearance(),0,true);
    return v;
}
inline api::VehicleDefinition superHeavy(){
    api::VehicleDefinition v;v.set_name("Super Heavy-class booster (approximate)");v.set_diameter_meters(2*radius);
    auto* s=v.add_stages();s->set_name("Booster");mass(s,combine(booster(true)),combine(booster(false)),boosterPropellant,boosterLength);raptors(s,boosterLength,true);aerodynamics(s,boosterLength);
    boosterLook(s->mutable_appearance(),0,true);
    for(const double y:{-pinOut,pinOut})put(s->add_hard_points_meters(),pinAt,y,0);
    // The three grid fins, always out, each where it is: its normal force along body x × its spindle (finSlope a radian of
    // deflection, engines-first flight) and 0.15 of drag, both on the stage's reference area.
    for(const Fin f:fins){auto* x=s->add_surfaces();x->set_name("grid fin");x->set_limit_radians(finLimit);x->set_rate_radians_per_second(60*deg);
        put(x->mutable_position_meters(),finAt,f.y*(radius+finSpan/2),f.z*(radius+finSpan/2));put(x->mutable_normal(),0,-f.z,f.y);
        auto* l=x->mutable_lift();l->add_mach(0);l->add_angle_degrees(0);l->add_deflection_degrees(-30);l->add_deflection_degrees(30);l->add_values(-finSlope*finLimit);l->add_values(finSlope*finLimit);
        auto* d=x->mutable_drag();d->add_mach(0);d->add_angle_degrees(0);d->add_deflection_degrees(0);d->add_values(finDrag);}
    // Cold-gas RCS 3 m below the nose: on the ±y sides pushing ±z and on the ±z sides pushing ±y (pitch, yaw and, in opposed
    // pairs, roll), 30 kN each.
    for(const double side:{-1.,1.})for(const double push:{-1.,1.}){
        auto* a=s->add_thrusters();a->set_name("rcs y");put(a->mutable_position_meters(),3,side*radius,0);put(a->mutable_direction(),0,0,push);a->set_thrust_newtons(3e4);a->set_exhaust_velocity_meters_per_second(700);
        auto* b=s->add_thrusters();b->set_name("rcs z");put(b->mutable_position_meters(),3,0,side*radius);put(b->mutable_direction(),0,push,0);b->set_thrust_newtons(3e4);b->set_exhaust_velocity_meters_per_second(700);}
    return v;
}
// ---- Guidance from what the flight API reports (ECEF, NED attitude, rates, mass, propellant, contact) ----
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
// toBody: ECEF to body (rows: the body axes); specific: the IMU's specific force (body, m/s²); q: dynamic pressure (Pa);
// surfaces: their deflections (rad); failed: by engine (0-based), as the engines report it.
struct State {Vec position,velocity,rate,contact,specific;Mat toBody;double mass=0,propellant=0,thrust=0,height=0,q=0;std::vector<double> surfaces;std::vector<bool> failed;};
// surfaces: deflections (rad) by surface, in the stage's order; none sent when empty.
struct Command {std::vector<int> ignite,cutoff;double throttle=-1,gimbalPitch=0,gimbalYaw=0;std::vector<double> surfaces;Vec torque;bool torqueOn=false;};
// Where an engine's nozzle sits off the axis (body y, z; m), by its number (1-based), as raptors() lays them out.
inline Vec boosterNozzle(int i){const double r=i<=3?.75:i<=13?2.2:3.9,a=(i<=3?(i-1)*120.:i<=13?(i-4)*36.:(i-14)*18+9.)*deg;return{0,r*std::cos(a),r*std::sin(a)};}
inline Vec shipNozzle(int i){const double r=i<=3?1:3.2,a=(i<=3?90+(i-1)*120.:30+(i-4)*120.)*deg;return{0,r*std::cos(a),r*std::sin(a)};}
// The engines a burn uses: the first n still working of an order of preference (1-based), lit and put out by commands; one
// that fails while lit (the engines report it) is left out and the next working one lit in its place on the next use(). Up to
// 3, out of the working ones among the first `balance`, the set whose thrust centre is nearest the body's z axis (the line
// across the catch pins: leaning to bring the thrust through the centre of mass then keeps both pins level), then nearest the
// axis, then the most preferred.
class Engines {
public:
    Engines(std::vector<int> order,Vec(*nozzle)(int),std::size_t balance=0):order_(std::move(order)),nozzle_(nozzle),balance_(balance){}
    void use(Command& c,const State& s,int n){
        std::vector<int> want=choose(s,n);
        for(int e:want)if(std::find(lit_.begin(),lit_.end(),e)==lit_.end())c.ignite.push_back(e);
        for(int e:lit_)if(std::find(want.begin(),want.end(),e)==want.end())c.cutoff.push_back(e);
        lit_=std::move(want);n_=n;
    }
    void keep(Command& c,const State& s){if(std::any_of(lit_.begin(),lit_.end(),[&](int e){return failed(s,e);}))use(c,s,n_);} // A failed one replaced.
    int count()const{return static_cast<int>(lit_.size());}
    int wanted()const{return n_;}
    int working(const State& s,int n)const{int k=0;for(int e:order_)k+=k<n&&!failed(s,e);return k;} // Of the first n wanted, how many could be lit.
    Vec centre()const{Vec c;for(int e:lit_)c=c+nozzle_(e);return lit_.empty()?c:c*(1./static_cast<double>(lit_.size()));} // Thrust centre (body y, z).
private:
    static bool failed(const State& s,int e){return e>=1&&static_cast<std::size_t>(e)<=s.failed.size()&&s.failed[static_cast<std::size_t>(e-1)];}
    std::vector<int> choose(const State& s,int n)const{
        std::vector<int> pool,best;for(std::size_t i=0;i<order_.size()&&i<balance_;++i)if(!failed(s,order_[i]))pool.push_back(order_[i]);
        if(n<1||n>3||static_cast<int>(pool.size())<n){for(int e:order_)if(static_cast<int>(best.size())<n&&!failed(s,e))best.push_back(e);return best;}
        double least=1e300;unsigned pick=0;
        for(unsigned m=1;m<(1u<<pool.size());++m)if(std::popcount(m)==n){Vec c;double rank=0; // Every set of n: its thrust centre and rank.
            for(std::size_t i=0;i<pool.size();++i)if(m>>i&1u){c=c+nozzle_(pool[i]);rank+=static_cast<double>(i);}
            const double x=(10*std::abs(c.y)+std::abs(c.z))/n+1e-3*rank;if(x<least){least=x;pick=m;}}
        for(std::size_t i=0;i<pool.size();++i)if(pick>>i&1u)best.push_back(pool[i]);return best;
    }
    std::vector<int> order_,lit_;Vec(*nozzle_)(int);std::size_t balance_;int n_=0;
};
// The U.S. Standard Atmosphere 1976 to 86 km (none above, as Albert's): its temperature (K) and density (kg/m³) at a height
// over the sea (m), by layers of constant lapse in geopotential height.
struct Air76 {double temperature,density;};
inline Air76 air76(double z){
    if(z>=86e3)return{186.87,0};
    constexpr double base[]{0,11e3,20e3,32e3,47e3,51e3,71e3,84852},temp[]{288.15,216.65,216.65,228.65,270.65,270.65,214.65,186.946},
        lapse[]{-.0065,0,.001,.0028,0,-.0028,-.002,0},pressure[]{101325,22632.06,5474.889,868.0187,110.9063,66.93887,3.956420,.3734};
    const double h=6356766*std::max(z,0.)/(6356766+std::max(z,0.));int i=0;while(i<7&&h>=base[i+1])++i;
    const double t=temp[i]+lapse[i]*(h-base[i]),p=lapse[i]!=0?pressure[i]*std::pow(temp[i]/t,.0341632/lapse[i]):pressure[i]*std::exp(-.0341632*(h-base[i])/temp[i]);
    return{t,p/(287.053*t)};
}
inline double density(double h){return air76(h).density;}
inline double sound(double h){return std::sqrt(1.4*287.053*air76(h).temperature);}
// What a vehicle needs to come down onto the tower's arms on its engines: the site's frame, the steering by gimbal and by
// RCS, the roll that puts its pins over the arms, the approach across, and the hover over them and the descent onto them.
class TowerApproach {
public:
    // site: the tower's base (ECEF, m) at geodetic latitude and longitude (rad), whose north and up the arms are laid out in;
    // arms: their height over it (m); rest: the pins' over them when they rest (m); length, pin: the vehicle's and its pins'
    // place aft of the nose (m); full, empty: its mass properties full and empty of its propellant (kg).
    TowerApproach(Vec site,double latitude,double longitude,double arms,double rest,double length,double pin,MassProperties full,MassProperties empty,double propellant)
        :site_(site),arms_(arms),rest_(rest),length_(length),pin_(pin),propellant_(propellant),full_(full),empty_(empty){
        north_={-std::sin(latitude)*std::cos(longitude),-std::sin(latitude)*std::sin(longitude),std::cos(latitude)};up_={std::cos(latitude)*std::cos(longitude),std::cos(latitude)*std::sin(longitude),std::sin(latitude)};}
    Vec up()const{return up_;}
    double catchHeight(const State& s)const{return arms_+rest_-(cg(s.propellant)-pin_);} // Of the centre of mass, pins on the arms, upright.
    double hoverAbove=10;
    bool armsClosed=true; // The arms around it: going down onto them waits for that.
protected:
    double share(double p)const{return std::clamp(p/propellant_,0.,1.);}
    double cg(double p)const{return empty_.cg+(full_.cg-empty_.cg)*share(p);}
    double pitchInertia(double p)const{return empty_.iy+(full_.iy-empty_.iy)*share(p);}
    double rollInertia(double p)const{return empty_.ix+(full_.ix-empty_.ix)*share(p);}
    static Vec bodyX(const State& s){return s.toBody.r0;}
    static Vec gravity(Vec r){return r*(-earth.mu/std::pow(norm(r),3));}
    // Thrust along d (ECEF) by the gimbal, the gain from the thrust's lever on the inertia (natural frequency 1.2 rad/s); offset:
    // the lit engines' thrust centre off the axis (body y, z), its line turned through the centre of mass beforehand and the body
    // leaned so that line, not the body's axis, runs along d.
    void aim(Command& c,const State& s,Vec d,Vec offset={})const{
        const double lever=std::max(length_-cg(s.propellant),1.),gain=pitchInertia(s.propellant)/std::max(s.thrust*lever,1.);
        d=unit(d+s.toBody.r1*(offset.y/lever)+s.toBody.r2*(offset.z/lever));const Vec e=mul(s.toBody,cross(bodyX(s),d)),w=s.rate;
        c.gimbalPitch=std::clamp(gain*(1.44*e.y-2*w.y)-offset.z/lever,-15*deg,15*deg);c.gimbalYaw=std::clamp(gain*(1.44*e.z-2*w.z)+offset.y/lever,-15*deg,15*deg);
        c.torqueOn=true;c.torque={-2e6*w.x,0,0}; // The RCS keeps the roll still while the engines steer.
    }
    void hold(Command& c,const State& s,Vec d)const{ // By the RCS alone (engines off): body x along d, no roll; from nearly
        // opposite it, over in pitch (the turn's axis is lost there).
        Vec e=mul(s.toBody,cross(bodyX(s),d));const Vec w=s.rate;const double i=pitchInertia(s.propellant);if(dot(bodyX(s),d)<0&&norm(e)<.5)e={0,1,0};
        c.torqueOn=true;c.torque={-2e6*w.x,i*(.04*e.y-.4*w.y),i*(.04*e.z-.4*w.z)};
    }
    // Minus the roll that takes body y or -y to north (the pins are symmetric, so the shorter way), in (-90°, 90°].
    double rollError(const State& s)const{
        double a=std::atan2(dot(s.toBody.r2,north_),dot(s.toBody.r1,north_));if(a>pi/2)a-=pi;else if(a<=-pi/2)a+=pi;return a;
    }
    void roll(Command& c,const State& s)const{c.torque.x=8e6*rollError(s)-4e7*s.rate.x;c.torqueOn=true;} // The pins over the arms.
    // Across onto the tower's axis without overshooting: toward it at min(0.15 d, √(2 · 1 m/s² · d)), that speed's error at 0.4/s
    // (a third of the attitude loop's bandwidth).
    Vec toAxis(const State& s,Vec flat)const{
        const double d=norm(flat);const Vec across=s.velocity+up_*-dot(s.velocity,up_),want=d>0?flat*(-std::min(.15,std::sqrt(2/d))):Vec{};
        return (want+across*-1)*.4;
    }
    // Over the tower's axis, still and aligned with the arms: ready to go down onto them.
    bool settled(const State& s,Vec flat)const{
        return norm(flat)<.4&&norm(s.velocity+up_*-dot(s.velocity,up_))<.3&&std::abs(dot(s.velocity,up_))<.5&&std::abs(rollError(s))<3*deg&&std::abs(s.rate.x)<.01;
    }
    // On `engines` engines: the hover hoverAbove over the catch height, or the descent at 1 m/s, over the axis and aligned; the
    // thrust it asks for (N).
    double overArms(Command& c,const State& s,Vec flat,double over,bool descend,int engines,Vec offset={})const{
        const double vz=dot(s.velocity,up_),vertical=std::max((descend?.7*(-1-vz):.2*(catchHeight(s)+hoverAbove-over)-.8*vz)+norm(gravity(s.position)),0.);
        Vec side=toAxis(s,flat);const double limit=std::tan(12*deg)*vertical;if(norm(side)>limit)side=side*(limit/norm(side));
        const Vec thrust=up_*vertical+side;c.throttle=std::clamp(s.mass*norm(thrust)/(engines*raptorSea),.4,1.);aim(c,s,unit(thrust),offset);roll(c,s);return s.mass*norm(thrust);
    }
    Vec site_,north_,up_;double arms_,rest_,length_,pin_,propellant_;MassProperties full_,empty_;
};
// The booster's way back from staging to a catch, at 50 Hz: a flip on 3 engines, the boostback on 13 (3 for its last km)
// until the predicted landing is the tower, the coast engines first (RCS above the air, the grid fins steering the landing
// onto the tower in it), the landing burn on 13 then 3, a hover over the tower aligned with the arms, and down at 1 m/s until
// the pins rest on them.
class BoosterReturn:public TowerApproach {
public:
    enum class Phase {wait,flip,boostback,coast,landing,hover,descend,caught};
    BoosterReturn(Vec site,double latitude,double longitude,double arms,double rest)
        :TowerApproach(site,latitude,longitude,arms,rest,boosterLength,pinAt,combine(booster(true)),combine(booster(false)),boosterPropellant){}
    Phase phase()const{return phase_;}
    double predicted()const{return miss_;} // The latest predicted landing's distance from the tower (m).
    Command step(double t,const State& s){
        Command c;c.surfaces={0.,0.,0.};const Vec g=gravity(s.position);set_.keep(c,s);const Vec rel=s.position+site_*-1; // keep: a lit engine that failed, the next working one in its place.
        const double over=dot(rel,up_);const Vec flat=rel+up_*-over;
        switch(phase_){
        case Phase::wait:if(t-start_>2){set_.use(c,s,3);c.throttle=.4;phase_=Phase::flip;}hold(c,s,unit(s.velocity));break; // Clear of the ship, then 3 engines.
        case Phase::flip:{ // Turn to point the thrust from the predicted landing back to the tower, boostbackPitch from the horizon.
            if(age_--<=0){aimed_=landing(s);age_=5;}const Vec d=boostbackDirection(aimed_);aim(c,s,d,set_.centre());if(dot(bodyX(s),d)>std::cos(8*deg)){set_.use(c,s,13);c.throttle=1;phase_=Phase::boostback;back_=d;}break;}
        case Phase::boostback:{ // The prediction every 0.1 s, every step within 10 km of the tower.
            if(age_--<=0||dot(aimed_,back_)>-1e4){aimed_=landing(s);age_=5;}const Vec e=aimed_;aim(c,s,boostbackDirection(e),set_.centre());miss_=norm(e);
            if(engines_==13&&dot(e,back_)>-5e3){set_.use(c,s,3);engines_=3;} // The last few km on 3, to cut finely.
            if(dot(e,back_)>=0||s.propellant<120e3){set_.use(c,s,0);engines_=13;phase_=Phase::coast;}break;}
        case Phase::coast:{ // Engines first through the descent: held by the RCS above the air, then the grid fins tilt the body so its
            // crossflow lift (away from the nose's tilt) carries the impact onto the tower; the landing burn when the stop needs it.
            if(age_--<=0){aimed_=landing(s,&seconds_);age_=25;}const Vec e=aimed_;const double seconds=seconds_;miss_=norm(e);const Vec vhat=unit(s.velocity);
            if(s.q<2e3)hold(c,s,vhat*-1);
            else{Vec want=e*(-6/std::max(seconds*seconds,1.));want=want+vhat*-dot(want,vhat);
                const double a=norm(want),side=1.2*boosterLength*2*radius/(pi*radius*radius),tilt=std::min(std::sqrt(a*s.mass/(s.q*pi*radius*radius*side)),std::sin(12*deg));
                steer(c,s,a>0?unit(vhat*-std::sqrt(1-tilt*tilt)+unit(want)*-tilt):vhat*-1);}
            if(over<40e3&&gateHeight(s,set_.working(s,13)*.9*raptorSea/s.mass)<=catchHeight(s)+hoverAbove+gate(s.mass,set_.working(s,3))+50){set_.use(c,s,13);c.throttle=.9;phase_=Phase::landing;}break;}
        case Phase::landing:{ // 13 engines to the gate (gateSpeed at the height 3 engines stop from), then 3 to rest at the hover
            // point: the constant deceleration that does it net of the drag; across, E-guidance onto the tower's axis over the
            // same time on 13 engines, the braking law on 3.
            const double above=over-catchHeight(s)-hoverAbove,vz=dot(s.velocity,up_),floor=engines_==13?gate(s.mass,set_.working(s,3)):0,v2=engines_==13?gateSpeed*gateSpeed:0;
            if(engines_==13&&(-vz<=gateSpeed||above<=floor)){set_.use(c,s,3);engines_=3;}
            const Vec across=s.velocity+up_*-vz;const double to=above-floor;
            const double brake=to>1&&vz<0?std::max(vz*vz-v2,0.)/(2*to):0,tgo=std::max(vz<0&&above>1?(engines_==13?2*to/(gateSpeed-vz)+2*gate(s.mass,set_.working(s,3))/gateSpeed:2*above/-vz):1.,2.);
            const double up=std::max(brake+norm(g)-drag(s)*(norm(s.velocity)>0?-vz/norm(s.velocity):0),0.);
            // At most the tilt whose thrust across beats the crossflow lift it raises (T sin θ - q S C sin²θ peaks at sin θ = T/2qSC).
            const double side0=1.2*boosterLength*2*radius/(pi*radius*radius),best=std::asin(std::min(1.,std::max(s.thrust,1.)/(2*std::max(s.q,1.)*pi*radius*radius*side0)));
            const double tilt=std::tan(std::min(20*deg,best))*std::max(up,norm(g));Vec side=engines_==13?flat*(-6/(tgo*tgo))+across*(-4/tgo):toAxis(s,flat);
            if(norm(side)>tilt)side=side*(tilt/norm(side));
            const Vec a=up_*up+side;c.throttle=std::clamp(s.mass*norm(a)/(std::max(set_.count(),1)*raptorSea),.4,1.);aim(c,s,unit(a),set_.centre());if(engines_==3)roll(c,s);
            if(engines_==3&&(above<2||vz>-1))phase_=Phase::hover;break;}
        case Phase::hover:case Phase::descend: // Over the tower, then down at 1 m/s until the pins rest on the arms.
            if(phase_==Phase::hover&&armsClosed&&settled(s,flat))phase_=Phase::descend;
            overArms(c,s,flat,over,phase_==Phase::descend,std::max(set_.count(),1),set_.centre());
            if(norm(s.contact)>0){if(touch_<0)touch_=t;if(t-touch_>=.3){c.throttle=.4;phase_=Phase::caught;caught_=t;}}else touch_=-1;break; // On the arms for 0.3 s.
        case Phase::caught: // The engines at their least, held upright, while the arms take the load, then off; slipping off in that second: back down.
            if(!off_){if(norm(s.contact)==0){phase_=Phase::descend;touch_=-1;break;}aim(c,s,up_,set_.centre());c.throttle=.4;if(t-caught_>1){set_.use(c,s,0);off_=true;}}
            else{c.torqueOn=true;c.torque={};}break;
        }
        return c;
    }
    void start(double t){start_=t;}
    static constexpr double gateSpeed=80;
    double dt=.02,boostbackPitch=-5*deg; // boostbackPitch: above the horizon (negative: below).
    // The boostback and the fall aim this far east of the tower (over the sea, away from the tower's side), the landing
    // burn then bringing it onto the arms' axis from there while still high (m).
    double offshore=300;
private:
    Vec boostbackDirection(Vec miss)const{const Vec h=unit(miss*-1);return h*std::cos(boostbackPitch)+up_*std::sin(boostbackPitch);} // miss: horizontal.
    void steer(Command& c,const State& s,Vec d)const{ // Body x along d by the grid fins (engines off, in the air), the roll by the RCS.
        const Vec e=mul(s.toBody,cross(bodyX(s),d)),w=s.rate;const double i=pitchInertia(s.propellant);
        c.surfaces=finDeflections({0,i*(.3*e.y-.8*w.y),i*(.3*e.z-.8*w.z)},s.q,cg(s.propellant));c.torqueOn=true;c.torque={-2e6*w.x,0,0};
    }
    // The fins' deflections that give moment m (body, N m; x: the roll, none here) at dynamic pressure q: fin i gives, a radian,
    // q S finSlope (r, -L u_y, -L u_z) (its force along x × u at r off the axis, L ahead of the centre of mass), the 3 x 3 system
    // solved by Cramer's rule; each clamped to its limit.
    static std::vector<double> finDeflections(Vec m,double q,double cg){
        const double k=std::max(q,1.)*pi*radius*radius*finSlope,r=radius+finSpan/2,l=cg-finAt;Vec col[3];
        for(int i=0;i<3;++i)col[i]=Vec{r,-l*fins[i].y,-l*fins[i].z}*k;
        auto det=[](Vec a,Vec b,Vec c){return dot(a,cross(b,c));};const double d=det(col[0],col[1],col[2]);
        return{std::clamp(det(m,col[1],col[2])/d,-finLimit,finLimit),std::clamp(det(col[0],m,col[2])/d,-finLimit,finLimit),std::clamp(det(col[0],col[1],m)/d,-finLimit,finLimit)};
    }
    // Where a burn lit now (thrust `accel` m/s² against the velocity) slows the fall to gateSpeed: the height over the tower's base.
    double gateHeight(const State& s,double accel)const{
        Vec r=s.position,v=s.velocity;fly(r,v,s.mass,accel,.1,[&]{return -dot(v,up_)<=gateSpeed||dot(r+site_*-1,up_)<0;});return dot(r+site_*-1,up_);
    }
    // The 3 engines' stop from gateSpeed at mass m (their net deceleration floored at 1 m/s²: too heavy to stop on 3).
    static double gate(double m,int engines=3){return gateSpeed*gateSpeed/(2*std::max(.8*engines*raptorSea/m-g0,1.));}
    // The mass to plan the landing with: the booster's now, at most its own with the propellant a boostback leaves.
    static double landingMass(double m){return std::min(m,boosterEmpty+170e3);}
    // Where the fall reaches the gate: ballistic until a burn of 13 engines at 90% lit there would reach it at its height (checked
    // every 2 s), then that burn: horizontal from the point offshore (m); seconds: the time to it.
    Vec landing(const State& s,double* seconds=nullptr)const{
        const double mass=landingMass(s.mass),accel=set_.working(s,13)*.9*raptorSea/mass,low=catchHeight(s)+hoverAbove+gate(mass,set_.working(s,3))+50;Vec r=s.position,v=s.velocity;int n=0,m=0;
        auto height=[&](Vec p){return dot(p+site_*-1,up_);};auto gated=[&](Vec& p,Vec& u){fly(p,u,mass,accel,.25,[&]{++m;return -dot(u,up_)<=gateSpeed||height(p)<0;});};
        fly(r,v,mass,0,.5,[&]{if(++n%4==0&&dot(v,up_)<0&&height(r)<40e3){Vec p=r,u=v;const int k=m;gated(p,u);m=k;if(height(p)<=low)return true;}return height(r)<0&&dot(v,up_)<0;});
        gated(r,v);if(seconds)*seconds=.5*(n-1)+.25*m;const Vec rel=r+site_*-1;return rel+up_*-dot(rel,up_)+offshore_*-1.;
    }
    // The drag coefficient engines first (the stage's base table and the fins').
    static double baseDrag(double v,double h){const double m=v/sound(h),x[]{0,.8,1.2,2,5},y[]{1,1.1,1.4,1.2,1};
        for(int i=0;i<4;++i)if(m<=x[i+1])return 3*finDrag+y[i]+(y[i+1]-y[i])*(m-x[i])/(x[i+1]-x[i]);return 1+3*finDrag;}
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
    Phase phase_=Phase::wait;Vec back_,aimed_,offshore_=cross(north_,up_)*offshore;double start_=0,miss_=0,seconds_=0,caught_=0,touch_=-1;int engines_=13,age_=0;bool off_=false;
    // The 13 gimballed engines first (the centre 3, then the middle ring), then the outer ones: the burns' engines, failed ones replaced.
    Engines set_{[]{std::vector<int> v;for(int i=1;i<=33;++i)v.push_back(i);return v;}(),boosterNozzle,13};
};
}
