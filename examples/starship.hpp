#pragma once
// A Starship-class flight built only from Albert's generic physics: the vehicle as definitions (no preset in the core), the
// flight program here. Public figures, Starship V3 (read 2026-10-20): Super Heavy 72.3 m × 9 m of a 124.4 m stack, its
// propellant 400 t over V2's (3,800 t here, from the 3,400 t of earlier boosters), 33 Raptor 3; the ship 52.1 m, 100 t more
// than V2 (1,600 t here), 3 Raptor 3 and 3 Raptor Vacuum; Raptor 3 by SpaceX's August 2024 card: 280 tf (2.75 MN) at sea
// level, 350 s, 1,525 kg (1,720 kg with what the vehicle carries for it); the 13 inner engines gimballed; three grid fins (see
// fins); staging near 64 km about 150 s after liftoff, landing burn about 6 min after, the catch on the tower's arms. Earlier
// figures kept: Super Heavy 275 t empty, the ship ~100 t (2021). Derived here, so approximate: a Raptor 3's exit area 1.33 m²
// (Raptor's ~1.3 m nozzle), so its vacuum thrust 2.88 MN and 334 s at sea level; a Raptor Vacuum 3's 3.08 MN (V2's 258 tf
// scaled as Raptor 2 to 3: 230 to 280 tf; no public figure), 380 s; the tanks' places (LOX 78%, CH4 22%); the aerodynamics
// (crossflow on a 9 m cylinder); the grid fins' forces; the booster's RCS (30 kN cold gas); the propellant left at main engine
// cutoff (13%: the Δv of a boostback from ~1.6 km/s downrange and a landing); the tower's arms (steel rails, shock-absorbed).
// Not a model of any flight: the timeline is compared with the public profile only.
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
constexpr double pi=3.14159265358979323846,deg=pi/180,g0=9.80665,radius=4.5,shipLength=52.1,boosterLength=72.3;
constexpr double raptorSea=280e3*g0,raptorExit=1.33,raptorVacuum=raptorSea+101325*raptorExit,raptorExhaust=350*g0,rvacThrust=258e3*g0*280/230,rvacExhaust=380*g0;
constexpr double pinOut=radius+1; // The catch pins' ends off the axis (m): 1 m out of the hull.
constexpr double boosterEmpty=275e3,boosterPropellant=3800e3,shipEmpty=100e3,shipPropellant=1600e3,engineMass=1.72e3,reserve=.15*boosterPropellant;
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
    if(full){p.push_back({.78*shipPropellant,30,48});p.push_back({.22*shipPropellant,14,30});}return p;}
inline std::vector<Part> booster(bool full){std::vector<Part> p{{boosterEmpty-33*engineMass,0,boosterLength},{33*engineMass,boosterLength-3.3,boosterLength}};
    if(full){p.push_back({.78*boosterPropellant,35.6,69.3});p.push_back({.22*boosterPropellant,6,35.6});}return p;}
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
        e->set_exit_area_meters2(exit);e->set_min_throttle(.4);e->set_gimbal_radians(gimbal);e->set_gimbal_rate_radians_per_second(20*deg);e->set_start_kilograms(100);}; // A start's spin-up.
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
// The cryogens as the tanks hold them (estimates): 1% of what is left boiled off a day, the residuals the engines cannot draw.
inline void cryogens(api::StageDefinition* st,double residual){st->set_boiloff_per_day(.01);st->set_residual_kilograms(residual);}
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
    const double area=pi*radius*radius;s->set_thruster_gas_kilograms(6e3); // Its RCS on the tanks' gas (an estimate).
    for(const auto& f:flaps){auto* x=s->add_surfaces();x->set_name(f.name);x->set_limit_radians(45*deg);x->set_rate_radians_per_second(30*deg);put(x->mutable_position_meters(),f.x,f.y,0);
        auto* t=x->mutable_drag();t->add_mach(0);for(int a=-180;a<=180;a+=15)t->add_angle_degrees(a);for(int d=-45;d<=45;d+=15)t->add_deflection_degrees(d);
        for(int a=-180;a<=180;a+=15)for(int d=-45;d<=45;d+=15)t->add_values(flapCn*f.area/area*std::sin((45+d)*deg)*std::abs(std::sin(a*deg)));}
    for(const double x:{4.,46.})for(const double side:{-1.,1.})for(const double push:{-1.,1.}){
        auto* a=s->add_thrusters();a->set_name("rcs y");put(a->mutable_position_meters(),x,side*radius,0);put(a->mutable_direction(),0,0,push);a->set_thrust_newtons(1e4);a->set_exhaust_velocity_meters_per_second(700);
        auto* b=s->add_thrusters();b->set_name("rcs z");put(b->mutable_position_meters(),x,0,side*radius);put(b->mutable_direction(),0,push,0);b->set_thrust_newtons(1e4);b->set_exhaust_velocity_meters_per_second(700);}
    for(int i=0;i<6;++i){auto* n=s->add_thermal_nodes();n->set_name("tile "+std::to_string(i+1));put(n->mutable_position_meters(),4+8*i,0,radius);put(n->mutable_normal(),0,0,1);
        n->set_area_meters2(1);n->set_heat_capacity_joules_per_kelvin(6e3);n->set_emissivity(.85);n->set_absorptivity(.85);n->set_conductance_watts_per_kelvin(1);n->set_limit_kelvin(1700);
        n->set_failure_kelvin(1950);} // Silica tiles past ~1950 K: the steel under them goes, and the ship with it.
    auto* n=s->add_thermal_nodes();n->set_name("leeward steel");put(n->mutable_position_meters(),25,0,-radius);put(n->mutable_normal(),0,0,-1);
    n->set_area_meters2(1);n->set_heat_capacity_joules_per_kelvin(1.58e4);n->set_emissivity(.3);n->set_absorptivity(.4);n->set_limit_kelvin(1100);n->set_failure_kelvin(1300); // Stainless past ~1300 K: too weak for the tank pressure.
    for(const double y:{-pinOut,pinOut})put(s->add_hard_points_meters(),shipPinAt,y,0);
}
// The booster's grid fins as on V3 (2026): three, ~50% larger than the four before (27 m² each, C_Nδ 2.5) and lower, their
// spindles 7.5 m below the top, two over the catch fittings (body ±y) and one on -z. Three and asymmetric is what SpaceX
// shows; this layout is an assumption. Their normal force per radian on the stage's reference area, and the hard points
// (catch fittings) 9 m below the top, 5.5 m off the axis, under the ±y fins.
struct Fin {double y,z;}; // The spindle's direction (body y, z).
constexpr Fin fins[]{{1,0},{-1,0},{0,-1}};
constexpr double pinAt=9,finAt=7.5,finSpan=3.7,finWidth=6.1,finLimit=30*deg,finSlope=27*2.5/(pi*radius*radius),finDrag=.15;
// How they look (the scene only), from each one's nose `at` m along its stage: stainless steel; the ship's nose a tangent ogive
// (11 m) in sections, black tiles on its windward half (+z), four flaps (hinged on its sides along x: +45° square to the
// flow, -45° folded to the lee) turning with surfaces 1-4 when `moving`, else folded; the booster's vented hot-staging ring
// and its three grid fins (on their radial spindles, with surfaces 1-3 when `moving`); both vehicles' catch pins.
inline void put(api::Vector3* o,const Vec& v){put(o,v.x,v.y,v.z);}
inline void section(api::StageAppearance* a,double from,double to,double r0,double r1,std::uint32_t colour,double tiles){
    auto* x=a->add_sections();x->set_from_meters(from);x->set_to_meters(to);x->set_radius_from_meters(r0);x->set_radius_to_meters(r1);x->set_color(colour);x->set_tiles_radians(tiles);}
inline void tank(api::StageAppearance* a,double from,double to,bool full){auto* t=a->add_tanks();t->set_from_meters(from);t->set_to_meters(to);t->set_full(full);}
inline void hinged(api::StageAppearance* a,const char* name,Vec hinge,Vec axis,Vec out,double span,double width,double thick,unsigned surface,double gain,double offset,std::uint32_t colour,bool grid=false){
    auto* x=a->add_parts();x->set_name(name);put(x->mutable_hinge_meters(),hinge);put(x->mutable_axis(),axis);put(x->mutable_outward(),out);x->set_span_meters(span);x->set_width_meters(width);
    x->set_thickness_meters(thick);x->set_surface(surface);x->set_gain(gain);x->set_offset_radians(offset);x->set_color(colour);x->set_grid(grid);}
constexpr std::uint32_t steel=0xc9c9c6,shield=0x161616,vents=0x3c3c3c;
inline void shipLook(api::StageAppearance* a,double at,bool moving){
    const double l=11,rho=(radius*radius+l*l)/(2*radius);auto r=[&](double x){return std::sqrt(rho*rho-(l-x)*(l-x))+radius-rho;};
    const double cuts[]{0,.4,1.2,2.5,4,6,8.5,11};for(int i=0;i+1<8;++i)section(a,at+cuts[i],at+cuts[i+1],std::max(r(cuts[i]),.05),r(cuts[i+1]),steel,pi);
    section(a,at+l,at+shipLength,radius,radius,steel,pi);tank(a,at+14,at+48,!moving); // Frosted: full riding on the booster.
    for(const double side:{-1.,1.}){ // Forward flaps on the nose, aft ones by the engines; the left one turns the other way.
        const unsigned fwd=side<0?1:2,aft=side<0?3:4;const double g=moving?side:0,o=moving?-side*45*deg:-side*90*deg; // Folded: up to -z.
        hinged(a,side<0?"flap forward left":"flap forward right",{at+8,side*r(8),0},{1,0,0},{0,side,0},7,3.4,.35,moving?fwd:0,g,o,steel);
        hinged(a,side<0?"flap aft left":"flap aft right",{at+44.5,side*radius,0},{1,0,0},{0,side,0},10,5,.4,moving?aft:0,g,o,steel);
        hinged(a,"pin",{at+shipPinAt,side*radius,0},{0,side,0},{1,0,0},pinOut-radius,1,1,0,0,0,vents);
        hinged(a,"chine",{at+27.5,side*radius,0},{1,0,0},{0,side,0},33,.5,.25,0,0,0,steel);} // Along its sides, tiles to steel.
}
inline void boosterLook(api::StageAppearance* a,double at,bool moving){
    section(a,at,at+2,radius,radius,vents,0);section(a,at+2,at+boosterLength,radius,radius,steel,0);tank(a,at+6,at+69.3,false);
    for(unsigned i=0;i<3;++i){const Fin f=fins[i];const double r=radius+finSpan/2; // +δ turns it about its spindle, pushing along body x × spindle.
        hinged(a,"grid fin",{at+finAt,f.y*r,f.z*r},{0,f.y,f.z},{0,-f.z,f.y},finSpan,finWidth,.6,moving?i+1:0,1,0,0x2e2e2e,true);}
    for(const double side:{-1.,1.})hinged(a,"pin",{at+pinAt,side*radius,0},{0,side,0},{1,0,0},pinOut-radius,1,1,0,0,0,vents);
    hinged(a,"raceway",{at+37,0,-radius},{1,0,0},{0,0,-1},58,.35,.5,0,0,0,steel); // The cable run down its side.
    for(int i=0;i<20;++i){const double t=(i+.5)*18*deg; // The hot-staging ring's vents.
        hinged(a,"vent",{at+1,radius*std::cos(t),radius*std::sin(t)},{1,0,0},{0,std::cos(t),std::sin(t)},1.5,.04,.7,0,0,0,0x080808);}
}
// The stack (stage 0: booster burning with the full ship on top; stage 1: the ship), the ship alone and the booster alone.
inline api::VehicleDefinition stack(){
    api::VehicleDefinition v;v.set_name("Starship-class stack (approximate, public figures)");v.set_crash_speed_meters_per_second(5); /* A steel hull: striking anything faster breaks it up. */v.set_diameter_meters(2*radius);v.set_attitude_torque_newton_meters(5e7);
    auto parts=[](bool boosterFull){auto p=ship(true);for(auto x:booster(boosterFull)){x.from+=shipLength;x.to+=shipLength;p.push_back(x);}return p;};
    auto* s0=v.add_stages();cryogens(s0,20e3);s0->set_name("Super Heavy");mass(s0,combine(parts(true)),combine(parts(false)),boosterPropellant,shipLength+boosterLength);
    raptors(s0,shipLength+boosterLength,true);aerodynamics(s0,shipLength+boosterLength);shipLook(s0->mutable_appearance(),0,false);boosterLook(s0->mutable_appearance(),shipLength,false);
    auto* s1=v.add_stages();cryogens(s1,8e3);s1->set_name("Starship");mass(s1,combine(ship(true)),combine(ship(false)),shipPropellant,shipLength);raptors(s1,shipLength,false);aerodynamics(s1,shipLength);shipSystems(s1);shipLook(s1->mutable_appearance(),0,true);
    return v;
}
inline api::VehicleDefinition shipAlone(){
    api::VehicleDefinition v;v.set_name("Starship-class ship (approximate)");v.set_crash_speed_meters_per_second(5); /* A steel hull: striking anything faster breaks it up. */v.set_diameter_meters(2*radius);
    auto* s=v.add_stages();cryogens(s,8e3);s->set_name("Starship");mass(s,combine(ship(true)),combine(ship(false)),shipPropellant,shipLength);raptors(s,shipLength,false);aerodynamics(s,shipLength);shipSystems(s);
    shipLook(s->mutable_appearance(),0,true);
    return v;
}
inline api::VehicleDefinition superHeavy(){
    api::VehicleDefinition v;v.set_name("Super Heavy-class booster (approximate)");v.set_crash_speed_meters_per_second(5); /* A steel hull: striking anything faster breaks it up. */v.set_diameter_meters(2*radius);
    auto* s=v.add_stages();cryogens(s,20e3);s->set_name("Super Heavy"); /* Its RCS on the tanks' ullage gas: drawn from the propellant. */mass(s,combine(booster(true)),combine(booster(false)),boosterPropellant,boosterLength);raptors(s,boosterLength,true);aerodynamics(s,boosterLength);
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
// gimbals: engines given their own deflection (rad) past the common one, after it (a roll by differential gimbal).
struct Command {std::vector<int> ignite,cutoff;double throttle=-1,gimbalPitch=0,gimbalYaw=0;std::vector<double> surfaces;Vec torque;bool torqueOn=false;
    struct Gimbal {int engine;double pitch,yaw;};std::vector<Gimbal> gimbals;};
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
    int balanced(const State& s)const{int k=0;for(std::size_t i=0;i<order_.size()&&i<balance_;++i)k+=!failed(s,order_[i]);return k;} // Working among the steering ones.
    Vec centre()const{Vec c;for(int e:lit_)c=c+nozzle_(e);return lit_.empty()?c:c*(1./static_cast<double>(lit_.size()));} // Thrust centre (body y, z).
    // A roll torque (N m) by turning each lit engine off the axis across its radius, by at most `limit` (rad) over the common
    // gimbal (an engine at (y, z), r off the axis, deflected k y/r in pitch and k z/r in yaw gives T k r about x), net of the
    // roll the common gimbal itself gives off the axis (T (y pitch + z yaw): one or two engines lit); the torque given (the
    // common's included).
    double roll(Command& c,const State& s,double torque,double limit=6*deg)const{
        double lever=0,cy=0,cz=0;for(int e:lit_){const Vec n=nozzle_(e);lever+=std::hypot(n.y,n.z);cy+=n.y;cz+=n.z;}
        const double each=s.thrust/std::max<double>(static_cast<double>(lit_.size()),1);if(lit_.empty()||!(each*lever>0))return 0;
        // One engine cannot roll without yawing (the same deflection does both): the roll left to the RCS, the common's counted.
        const double common=each*(cy*c.gimbalPitch+cz*c.gimbalYaw),k=lit_.size()<2?0.:std::clamp((torque-common)/(each*lever),-limit,limit);
        for(int e:lit_){const Vec n=nozzle_(e);const double r=std::hypot(n.y,n.z);if(r>1e-3)c.gimbals.push_back({e,c.gimbalPitch+k*n.y/r,c.gimbalYaw+k*n.z/r});}
        return common+k*each*lever;
    }
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
    bool closeArms()const{return close_;} // Centred between them, going for the catch: the arms round it.
protected:
    double share(double p)const{return std::clamp(p/propellant_,0.,1.);}
    double cg(double p)const{return empty_.cg+(full_.cg-empty_.cg)*share(p);}
    double pitchInertia(double p)const{return empty_.iy+(full_.iy-empty_.iy)*share(p);}
    double rollInertia(double p)const{return empty_.ix+(full_.ix-empty_.ix)*share(p);}
    static Vec bodyX(const State& s){return s.toBody.r0;}
    static Vec gravity(Vec r){return r*(-earth.mu/std::pow(norm(r),3));}
    // Thrust along d (ECEF) by the gimbal, the gain from the thrust's lever on the inertia (natural frequency: bandwidth()); offset:
    // the lit engines' thrust centre off the axis (body y, z), its line turned through the centre of mass beforehand and the body
    // leaned so that line, not the body's axis, runs along d.
    // The natural frequency within what the gimbal can do: 1.2 rad/s, less once 15° of it cannot turn the body through a quarter
    // radian at that rate (few engines, low throttle), down to 0.4.
    double bandwidth(const State& s)const{
        const double lever=std::max(length_-cg(s.propellant),1.);return std::clamp(std::sqrt(s.thrust*lever*std::sin(15*deg)/(pitchInertia(s.propellant)*.25)),.4,1.2);
    }
    void aim(Command& c,const State& s,Vec d,Vec offset={})const{
        const double lever=std::max(length_-cg(s.propellant),1.),gain=pitchInertia(s.propellant)/std::max(s.thrust*lever,1.),f=bandwidth(s),k=f*f,b=1.67*f;
        d=unit(d+s.toBody.r1*(offset.y/lever)+s.toBody.r2*(offset.z/lever));const Vec e=mul(s.toBody,cross(bodyX(s),d)),w=s.rate;
        c.gimbalPitch=std::clamp(gain*(k*e.y-b*w.y)-offset.z/lever,-15*deg,15*deg);c.gimbalYaw=std::clamp(gain*(k*e.z-b*w.z)+offset.y/lever,-15*deg,15*deg);
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
    // The pins over the arms (0.6 rad/s, critically damped): by the lit engines' differential gimbal (by), the RCS for the rest.
    void roll(Command& c,const State& s,const Engines* by=nullptr)const{
        const double want=rollInertia(s.propellant)*(.36*rollError(s)-1.2*s.rate.x),given=by?by->roll(c,s,want):0;c.torque.x=want-given;c.torqueOn=true;}
    // Across onto the tower's axis without overshooting: toward it at min(0.15 d, √(2 · 1 m/s² · d)), that speed's error at 0.4/s
    // (a third of the attitude loop's bandwidth).
    // flat: from the axis to the vehicle; it heads for the approach() point.
    Vec toAxis(const State& s,Vec flat)const{return onto(s,approach(flat));}
    // The same law onto any point (to: from it to the vehicle, horizontal), its gains within a third of the attitude's bandwidth
    // (one or two engines turn the body slowly: a faster loop across beats it).
    Vec onto(const State& s,Vec to)const{
        const double d=norm(to),kv=std::min(.4,bandwidth(s)/3);const Vec across=s.velocity+up_*-dot(s.velocity,up_),want=d>0?to*(-std::min(.375*kv,std::sqrt(2/d))):Vec{};
        return (want+across*-1)*kv;
    }
    // From where to head across (an offset from it, like flat): the arms open eastward into a V and the hull must not cross
    // them, so off their line (more than 4 m to the side) first onto it at the corridor point (corridor m east of the axis),
    // then along it onto the axis; from west of that, never over the tower: out to 30 m beside its line, east along there,
    // then in.
    Vec approach(Vec flat)const{
        const Vec east=cross(north_,up_);const double e=dot(flat,east),n=dot(flat,north_);
        if(std::abs(n)<=4&&e>-8)return flat; // On their line (the hull clear of the tower's face down to 8 m west of the axis).
        if(e<-8)return std::abs(n)<25?north_*(n-(n>=0?30:-30)):east*(e-10);
        return flat+east*-corridor;
    }
    double corridor=15,towerTop=146; // towerTop: over its base (m).
    // Over the tower's axis, still and aligned with the arms: ready to go down onto them.
    bool settled(const State& s,Vec flat)const{
        return norm(flat)<.4&&norm(s.velocity+up_*-dot(s.velocity,up_))<.3&&std::abs(dot(s.velocity,up_))<.5&&std::abs(rollError(s))<3*deg&&std::abs(s.rate.x)<.01;
    }
    // On `engines` engines: the hover hoverAbove over the catch height, or the descent at `rate` m/s, over the axis and aligned; the
    // thrust it asks for (N).
    double overArms(Command& c,const State& s,Vec flat,double over,bool descend,int engines,Vec offset={},const Engines* by=nullptr,double rate=1)const{
        const double vz=dot(s.velocity,up_),vertical=std::max((descend?.7*(-rate-vz):.2*(catchHeight(s)+hoverAbove-over)-.8*vz)+norm(gravity(s.position)),0.);
        Vec side=toAxis(s,flat);const double limit=std::tan(12*deg)*vertical;if(norm(side)>limit)side=side*(limit/norm(side));
        const Vec thrust=up_*vertical+side;c.throttle=std::clamp(s.mass*norm(thrust)/(engines*raptorSea),.4,1.);aim(c,s,unit(thrust),offset);roll(c,s,by);return s.mass*norm(thrust);
    }
    // Centred between the arms' rails (0.8 m off the hull) on their line, at most 8 m east of the axis (inside their reach) and
    // coming in slowly: they may close round it (in the 4 s it takes them it gets over the axis); once closing, until it drifts
    // 0.6 m off their line.
    bool centred(const State& s,Vec flat)const{
        const Vec v=s.velocity+up_*-dot(s.velocity,up_);const double n=std::abs(dot(flat,north_)),e=dot(flat,cross(north_,up_));
        return close_?n<.6&&e>-1.5&&e<9:n<.4&&e>-1&&e<8&&std::abs(dot(v,north_))<.3&&norm(v)<3;
    }
    // Hanging on the pins with the engine on: the body upright and the thrust's line through the pins (the pivot), so nothing is
    // left to swing when it cuts.
    void hang(Command& c,const State& s,const Engines* by=nullptr)const{
        const Vec e=mul(s.toBody,cross(bodyX(s),up_)),w=s.rate,o=by?by->centre():Vec{};const double lever=length_-pin_,gain=pitchInertia(s.propellant)/std::max(s.thrust*lever,1.);
        c.gimbalPitch=std::clamp(gain*(1.44*e.y-2*w.y)-o.z/lever,-15*deg,15*deg);c.gimbalYaw=std::clamp(gain*(1.44*e.z-2*w.z)+o.y/lever,-15*deg,15*deg);c.torque={};roll(c,s,by);
    }
    Vec site_,north_,up_;double arms_,rest_,length_,pin_,propellant_;MassProperties full_,empty_;bool close_=false;
};
// The booster's way back from staging, at 50 Hz, deciding all the way whether the catch can still be made:
// - a flip on 3 engines, the boostback on 13 (3 for its last km) until the predicted landing is just off the coast east of
//   the tower, the coast engines first (the RCS above the air, held at the attitude it enters it at; the grid fins steering
//   the landing in it);
// - the landing burn on 13 engines, planned at 70% of their thrust (the margin rides out an engine lost), to the gate, then on
//   3 across onto the tower's axis (2 or 1 once 3 at their least lift too much, more while it falls short), the arms closing
//   round it, and down onto them at 1 m/s; the roll that puts the pins over the arms by the engines' differential gimbal;
// - once the catch cannot be made (the engines, the propellant, the reach, a fall it cannot stop above the arms, too long over
//   them) it is given up for good: down at sea, east of the tower, or when that is out of reach as far from the tower as it
//   gets, braking all the way.
class BoosterReturn:public TowerApproach {
public:
    enum class Phase {wait,flip,boostback,coast,landing,final,hover,descend,caught,divert,down};
    BoosterReturn(Vec site,double latitude,double longitude,double arms,double rest)
        :TowerApproach(site,latitude,longitude,arms,rest,boosterLength,pinAt,combine(booster(true)),combine(booster(false)),boosterPropellant){hoverAbove=4;}
    Phase phase()const{return phase_;}
    double predicted()const{return miss_;}  // The latest predicted landing's distance from where it is aimed (m).
    const char* gaveUp()const{return why_;} // Why the catch was given up (null while it is on).
    Command step(double t,const State& s){
        Command c;c.surfaces={0.,0.,0.};set_.keep(c,s);const Vec rel=s.position+site_*-1; // keep: a lit engine that failed, the next working one in its place.
        const double over=dot(rel,up_),vz=dot(s.velocity,up_);const Vec flat=rel+up_*-over,across=s.velocity+up_*-vz;
        if(catch_&&phase_>=Phase::landing&&phase_<=Phase::descend&&set_.balanced(s)<2)giveUp(s,"motores com gimbal insuficientes para manobrar");
        switch(phase_){
        case Phase::wait:if(t-start_>2){set_.use(c,s,3);c.throttle=.4;phase_=Phase::flip;}hold(c,s,unit(s.velocity));break; // Clear of the ship, then 3 engines.
        case Phase::flip:{ // Turn to point the thrust from the predicted landing back to the tower, boostbackPitch from the horizon.
            if(age_--<=0){aimed_=landing(s);age_=5;}const Vec d=boostbackDirection(aimed_);aim(c,s,d,set_.centre());if(dot(bodyX(s),d)>std::cos(8*deg)){set_.use(c,s,13);c.throttle=boostbackThrottle;phase_=Phase::boostback;back_=d;}break;}
        case Phase::boostback:{ // The prediction every 0.1 s, every step within 10 km of the aim.
            if(age_--<=0||dot(aimed_,back_)>-1e4){aimed_=landing(s);age_=5;}const Vec e=aimed_;aim(c,s,boostbackDirection(e),set_.centre());miss_=norm(e);
            if(set_.wanted()==13&&dot(e,back_)>-5e3)set_.use(c,s,3); // The last few km on 3, to cut finely.
            if(dot(e,back_)>=0||s.propellant<120e3){set_.use(c,s,0);phase_=Phase::coast;if(dot(e,back_)<-2e3)giveUp(s,"boostback incompleto: o pouso cai antes da costa");}break;}
        case Phase::coast:{ // Engines first through the descent: held by the RCS above the air, then the grid fins tilt the body so its
            // crossflow lift (away from the nose's tilt) carries the impact onto the aim; the landing burn when the stop needs it.
            if(catch_&&over<divert)aim_=east_*corridor; // Low enough: the fins carry it from off the coast onto the approach.
            if(age_--<=0){aimed_=landing(s,&seconds_);age_=25;miss_=norm(aimed_);if(over<15e3&&reach(s)>3e3)giveUp(s,"pouso previsto fora do alcance da torre");}
            const Vec e=aimed_;const double seconds=seconds_;const Vec vhat=unit(s.velocity);
            if(s.q<2e3)hold(c,s,entry(s));
            else{Vec want=e*(-6/std::max(seconds*seconds,1.));want=want+vhat*-dot(want,vhat);
                // The push across a radian of tilt: the model's (normalForce q S), then as the IMU measures it (the force across the
                // flow over the tilt held away from it).
                const double a=norm(want),model=normalForce*s.q*pi*radius*radius/s.mass;
                {const Vec side=unit(want),f=tmul(s.toBody,s.specific);const double held=-dot(bodyX(s),side);
                    if(a>0&&std::abs(held)>std::sin(.3*deg))lift_+=.05*(std::clamp(dot(f,side)/held,.2*model,3*model)-lift_);else lift_+=.05*(model-lift_);}
                const double tilt=std::min(a/std::max(lift_,1e-3),std::sin(12*deg));
                const Vec dd=a>0?unit(vhat*-std::sqrt(1-tilt*tilt)+unit(want)*-tilt):vhat*-1;steer(c,s,dd);}
            double burn=0;
            if(over<40e3&&gateHeight(s,set_.working(s,13)*plan*raptorSea/s.mass,&burn)<=top(s)+gate(s.mass,set_.working(s,3))+50){
                if(catch_){if(set_.working(s,13)<5)giveUp(s,"motores insuficientes para a captura");
                    else if(s.propellant-residual<need(s,burn))giveUp(s,"propelante insuficiente para a captura");else if(reach(s)>400)giveUp(s,"pouso previsto fora do alcance da torre");}
                set_.use(c,s,13);c.throttle=plan;phase_=Phase::landing;}
            break;}
        case Phase::landing:{ // To the gate (gateSpeed at the height 3 engines stop from) against the velocity, every 0.2 s the
            // thrust that gets there at that height (its gate height as the coast's prediction flies it, by the secant through two
            // thrusts) and that burn's gate point; the thrust leaned off retrograde to bring that onto the approach point (the sea
            // point once the catch is off), at most the angle of attack whose push across beats the crossflow lift it raises
            // (T sin α - q S C sin²α peaks at sin α = T/2qSC), within 20°.
            const double above=over-top(s),floor=gate(s.mass,set_.working(s,3));
            if(catch_&&age_<=0&&!stops(s,catchHeight(s)))giveUp(s,"a queda não para acima dos braços");
            const bool fast=-vz>gateSpeed+10; // At the gate's height still falling fast: on 13 at full until it slows to it.
            if(above<=floor&&fast)accel_=std::max(set_.count(),1)*raptorSea/s.mass;
            if(-vz<=gateSpeed||(above<=floor&&!fast)||above<=0){
                if(catch_&&s.propellant-residual<need(s,0)+around(s,flat,over))giveUp(s,"propelante insuficiente para a captura daqui");
                set_.use(c,s,3);if(phase_==Phase::landing)phase_=Phase::final;}
            const double most=std::max(set_.count(),1)*raptorSea/s.mass;
            if(age_--<=0&&!(above<=floor&&fast)){age_=10;const double aim=top(s)+floor,a1=std::clamp(accel_>0?accel_:plan*most,.4*most,most),a2=a1*.9;
                auto run=[&](double a,Vec& end,Vec& u,double& secs){Vec r=s.position,v=s.velocity;int n=0;
                    // The drag a little short (braking early), its last 3 s upright as flown.
                    fly(r,v,s.mass,a,.1,[&]{++n;return -dot(v,up_)<=gateSpeed||dot(r+site_*-1,up_)<aim;},.85,3*std::max(a-g0,1.));end=r;u=v;secs=.1*n;return dot(r+site_*-1,up_);};
                Vec e1,e2,u1,u2;double t1=0,t2=0;const double h1=run(a1,e1,u1,t1),h2=run(a2,e2,u2,t2),f=h1>h2+1e-3?(aim-h2)/(h1-h2):1,g=std::clamp(f,0.,2.);
                accel_=std::clamp(a2+(a1-a2)*f,.4*most,most);const Vec end=e2+(e1+e2*-1.)*g,rel=end+site_*-1,u=u2+(u1+u2*-1.)*g;
                gated_=rel+up_*-dot(rel,up_)+(catch_?east_*corridor:aim_)*-1.;gatedV_=u+up_*-dot(u,up_);tgo_=std::max(t2+(t1-t2)*g,2.);}
            // Leaning the thrust off retrograde by α pushes across by about (T - N) α, N the body's normal force a radian (normalForce
            // q S): thrust-steered (toward the push) once T > N, body-steered (away from it, within the tables' first 10°) while the
            // air is thick. The push a radian as measured (the IMU's force across the flow over the angle of attack held), once
            // there is one to measure, in place of that estimate.
            const Vec back=unit(s.velocity*-1.);Vec want=gated_*(-6/(tgo_*tgo_));want=want+back*-dot(want,back);
            const double model=accel_-normalForce*s.q*pi*radius*radius/s.mass;
            {const Vec side=unit(want),f=tmul(s.toBody,s.specific);const double held=dot(bodyX(s),side);
                if(norm(want)>0&&std::abs(held)>std::sin(1.5*deg))slope_+=.05*(std::clamp(dot(f,side)/held,-100.,100.)-slope_);else slope_+=.05*(model-slope_);}
            // Upright again by the gate (3 engines turn the body back slowly): the lean's limit down to a quarter in its last seconds.
            const double room=std::clamp((tgo_-1.5)/3,.25,1.),push=slope_*s.mass,lean=std::min(norm(want)*s.mass/std::max(std::abs(push),1e5),(push>0?20*deg:10*deg)*room)*(push>0?1:-1);
            Vec d=norm(want)>0?back*std::cos(lean)+unit(want)*std::sin(lean):back;
            if(tgo_<3){Vec side=across*-.4;const double v=accel_*std::max(dot(back,up_),.3),limit=std::tan(8*deg)*std::clamp((tgo_-1)/2,0.,1.)*v;
                if(norm(side)>limit)side=side*(limit/norm(side));d=unit(up_*v+side);}
            thrust(c,s,d*accel_);if(s.q>2e3)finsToward(c,s,d); // Its last 3 s: upright, the speed across braked, for 3 engines to take over.
            break;}
        case Phase::final:{ // On 3 to the hover point, still falling at 1 m/s there, not below a 27° slope down to it from the
            // approach (the hull clear of the arms off their line); across by E-guidance, the braking law in its last seconds; at
            // sea, down to the water.
            const Vec to=goal(flat);const double above=over-top(s)-(catch_?safe(s,flat):0);
            if(catch_&&age_--<=0){age_=10;if(!stops(s,catchHeight(s)))giveUp(s,"a queda não para acima dos braços");}
            if(phase_!=Phase::final)break;
            const double up=vertical(s,above,vz,-1),limit=std::tan(20*deg)*up;
            // Across onto the goal at the speed that brakes to rest there at 0.8 m/s² (at most 8 m/s, kv/2 of the distance a second),
            // that speed's error at kv, a third of the attitude's bandwidth (critically damped, no overshoot). Well below the height
            // that clears what is around it: held where it is while it climbs.
            const double kv=std::min(.6,bandwidth(s)/3);Vec side;
            if(above<-5)side=across*-std::max(kv,.4);else{const double d=norm(to);side=((d>0?to*(-std::min({8.,std::sqrt(1.6*d),kv/2*d})/d):Vec{})+across*-1)*kv;}
            if(norm(side)>limit)side=side*(limit/norm(side));
            count(c,s,thrust(c,s,up_*up+side));close_=catch_&&centred(s,flat);
            if(catch_&&above<1&&norm(flat)<3){phase_=Phase::hover;hover_=t;}
            else if(!catch_&&(norm(s.contact)>0||above<.3)){set_.use(c,s,0);phase_=Phase::down;}
            break;}
        case Phase::hover:case Phase::descend: // Over the axis; down at 1 m/s once the arms are round it and it is aligned with them.
            close_=centred(s,flat);
            if(phase_==Phase::hover&&armsClosed&&aligned(s,flat))phase_=Phase::descend;
            else if(phase_==Phase::descend&&(norm(flat)>1.5||std::abs(rollError(s))>6*deg))phase_=Phase::hover; // Off it: held again.
            // Short of what finishing takes (down at 1 m/s, 3 s more): away while that can still be done (up 20 s of hover), else
            // onto the arms anyway, faster (nothing left to get away on).
            {const double usable=s.propellant-residual,second=g0*s.mass/raptorExhaust,finish=(std::max(over-catchHeight(s),0.)+3)*second;
                if(usable<finish&&usable>20*second)giveUp(s,"propelante insuficiente para terminar a captura");else hurry_=usable<finish;}
            if(norm(s.contact)>0)hover_=t; // On the arms (or touching them): not stuck over them.
            if(catch_&&t-hover_>25&&!hurry_)giveUp(s,"tempo demais sobre os braços");
            if(phase_==Phase::divert)break;
            if(hurry_&&armsClosed&&norm(flat)<1)phase_=Phase::descend;
            if(phase_==Phase::descend&&norm(s.contact)>0&&norm(flat)<2){ // The pins on the arms: the weight handed to them (the thrust at its
                // least, engines out until that is under 70% of it), held upright; caught after 0.3 s.
                hang(c,s,&set_);c.throttle=.4;if(set_.count()>1&&set_.count()*.4*raptorSea>.7*s.mass*g0)set_.use(c,s,set_.count()-1);
                if(touch_<0)touch_=t;if(t-touch_>=.3){phase_=Phase::caught;caught_=t;}break;}
            touch_=-1;count(c,s,overArms(c,s,flat,over,phase_==Phase::descend,std::max(set_.count(),1),set_.centre(),&set_,hurry_?2:1));break;
        case Phase::caught: // On the arms, the engines at their least while they take the load, the thrust's line through the pins
            // until it hangs still (or 15 s), then off; slipping off in the first second: back down.
            if(!off_){if(norm(s.contact)==0&&t-caught_<1){phase_=Phase::descend;touch_=-1;break;}hang(c,s,&set_);c.throttle=.4;
                if(set_.count()>1&&set_.count()*.4*raptorSea>.85*s.mass*g0)set_.use(c,s,set_.count()-1); // Their least would lift it off the arms.
                if(t-caught_>2&&(std::hypot(s.rate.y,s.rate.z)<.002||t-caught_>15)){set_.use(c,s,0);off_=true;}}
            else{c.torqueOn=true;c.torque={};}break;
        case Phase::divert:{ // Away from the tower toward the aim as fast as a 20° tilt allows, its height held until 150 m clear
            // of the tower (or it has left only what landing takes), then down still heading there.
            // Down: at the speed that 60% of what its engines can brake stops at the ground (at most 40 m/s), its error at 1/s; the
            // propellant kept for that: a hover of h/20 + 12 s.
            const double h=std::max(over-stand(s),0.),brake=std::max(set_.working(s,13)*raptorSea/s.mass-g0,1.);
            const Vec to=flat+aim_*-1.;const double d=norm(to),clear=norm(flat)>150||s.propellant-residual<(h/20+12)*g0*s.mass/raptorExhaust;
            const double up=clear?std::max(norm(gravity(s.position))+1.5*(-std::min(30.,std::sqrt(2*.5*std::min(brake,3*raptorSea/s.mass-g0)*std::max(h-2,0.))+1)-vz),0.)
                :norm(gravity(s.position))+.5*(-vz)+.2*(held_-over);
            // Across toward the aim, slowing to a stop over the last 150 m of height (the thrust goes to the fall's braking first).
            const Vec want=d>0?to*(-std::min(15.,std::sqrt(2*2.5*d))*(clear?std::clamp(h/150,0.,1.):1)/d):Vec{};Vec side=(want+across*-1)*.5;
            // Within 30 m of the axis at most 5° (the hull's foot, 60 m under the pins, swings the other way, toward the tower).
            const double limit=std::tan((norm(flat)<30?5:20)*deg)*std::max(up,1.);if(norm(side)>limit)side=side*(limit/norm(side));
            count(c,s,thrust(c,s,up_*std::max(up,0.)+side));
            if(over-stand(s)<.5){set_.use(c,s,0);phase_=Phase::down;} // On the ground (touching the arms or the tower: still getting away).
            break;}
        case Phase::down:c.torqueOn=true;c.torque={};if(set_.count())set_.use(c,s,0);break; // Down: engines off.
        }
        if(s.propellant<=residual)c.ignite.clear(); // Dry: nothing left to light.
        return c;
    }
    void start(double t){start_=t;}
    // plan: the landing burn's planned share of its engines' thrust; boostbackThrottle: the boostback's (about 50 s, as flown).
    // normalForce: the body's normal force coefficient a radian off the flow, engines first, as flights measure it (the IMU
    // across the flow; the tables read near 3.5).
    static constexpr double gateSpeed=80,plan=.7,residual=20e3,boostbackThrottle=.6,normalForce=.8;
    double dt=.02,boostbackPitch=-5*deg; // boostbackPitch: above the horizon (negative: below).
    // The boostback and the fall aim this far east of the tower (over the sea, away from the tower's side), the landing
    // burn then bringing it onto the arms' axis from there while still high (m); without the catch, onto the sea this far east.
    // divert: the height (m) below which the fins carry it from there onto the approach point.
    double offshore=300,sea=1500,divert=30e3;
private:
    // Gives the catch up for good: the arms stay open, the aim goes out to sea (from west of the tower, inland away from it, north
    // of the tank farm); from low (the last of the landing burn, over the arms), away from the tower at once.
    void giveUp(const State& s,const char* why){
        if(!catch_)return;catch_=false;why_=why;close_=false;const Vec rel=s.position+site_*-1;const double over=dot(rel,up_);
        aim_=dot(rel,east_)>-22?sea_:east_*-400.+north_*300.;
        if(phase_==Phase::final||phase_==Phase::hover||phase_==Phase::descend||(phase_==Phase::landing&&over<1500)){phase_=Phase::divert;held_=over;}
    }
    double stand(const State& s)const{return boosterLength-cg(s.propellant);} // Its centre of mass over its base, standing.
    // How far over the hover point it keeps at flat off the axis (m): in the arms' corridor (on their line, east of the tower)
    // or out east past their tips, a 27° slope down to it; elsewhere its foot 10 m over the tower's top (it gets round the
    // tower and over the arms there, then down the corridor).
    double safe(const State& s,Vec flat)const{
        const double e=dot(flat,east_),n=dot(flat,north_),slope=.5*std::max(norm(flat)-3,0.);
        if((std::abs(n)<=4&&e>-8)||e>25)return slope;return std::max(slope,towerTop+10+stand(s)-top(s));
    }
    // What getting round to the corridor from off it costs (kg): up to that height at 3 m/s and across at 5 m/s, hovering.
    double around(const State& s,Vec flat,double over)const{
        const double up=std::max(top(s)+safe(s,flat)-over,0.);return (up/3+norm(goal(flat))/5)*g0*s.mass/raptorExhaust;
    }
    double top(const State& s)const{return catch_?catchHeight(s)+hoverAbove:stand(s);} // The height (over the site) the burns stop at.
    Vec goal(Vec flat)const{return catch_?approach(flat):flat+aim_*-1.;} // From the point it is going for to it.
    // The predicted landing's distance from the approach point (m): what the landing burn would have to divert.
    double reach(const State&)const{return norm(aimed_+aim_+cross(north_,up_)*-corridor);}
    // Whether braking now straight up on every working engine of the 13 at full stops the fall above `top` m over the site (the
    // drag as predicted, two thirds of it: on the safe side).
    bool stops(const State& s,double top)const{
        const double a=set_.working(s,13)*raptorSea/s.mass-norm(gravity(s.position)),k=2./3*.5*pi*radius*radius/s.mass;double h=dot(s.position+site_*-1,up_),v=dot(s.velocity,up_);
        for(int i=0;i<5000&&v<0&&h>=top;++i){v+=(a+k*density(h)*baseDrag(-v,h)*v*v)*.02;h+=v*.02;}return h>=top;
    }
    // The point to go for (from it to the vehicle) within what an acceleration across of `limit` m/s² can still cover in tgo s
    // (a quarter of the most E-guidance asks: a sixth of a tgo²): out of reach, as near it as it gets.
    static Vec reachable(Vec to,double limit,double tgo){const double most=limit*tgo*tgo/6;return norm(to)>most?to*(most/norm(to)):to;}
    // The propellant (kg) the landing burn needs: `burn` s on the 13 at the planned thrust, the 3's stop from the gate, 15 s more.
    double need(const State& s,double burn)const{
        const double a3=.8*set_.working(s,3)*raptorSea/s.mass;return s.mass/raptorExhaust*(plan*set_.working(s,13)*raptorSea/s.mass*burn+a3*gateSpeed/std::max(a3-g0,1.)+g0*15);
    }
    // Over the axis, its pins over the arms and still: ready to go down onto them.
    bool aligned(const State& s,Vec flat)const{
        return norm(flat)<.5&&norm(s.velocity+up_*-dot(s.velocity,up_))<.3&&std::abs(rollError(s))<3*deg&&std::abs(s.rate.x)<.02;
    }
    // The upward acceleration (m/s², the drag's share netted) that slows the fall from `above` m over a point to vf (≤ 0) there
    // at a constant rate (falling slower than vf: eased toward it).
    double vertical(const State& s,double above,double vz,double vf)const{
        const double b=above<0?.5*(std::min(3.,-.3*above)-vz):vz<vf?(vz*vz-vf*vf)/(2*std::max(above,.1)):.5*(vf-vz); // Below it: up at most 3 m/s.
        return std::max(b+norm(gravity(s.position))-drag(s)*(norm(s.velocity)>0?-vz/norm(s.velocity):0),0.);
    }
    // Thrust for the acceleration a (m/s², ECEF) on the lit engines, through the centre of mass, the pins rolled over the arms;
    // the thrust (N).
    double thrust(Command& c,const State& s,Vec a)const{
        const double f=s.mass*norm(a);c.throttle=std::clamp(f/(std::max(set_.count(),1)*raptorSea),.4,1.);aim(c,s,unit(a),set_.centre());roll(c,s,&set_);return f;
    }
    // The engines for a thrust (N): one fewer once they cannot throttle down to it, one more (up to the 13) once short of it.
    void count(Command& c,const State& s,double demand){
        const int n=set_.count();if(n>1&&demand<.42*n*raptorSea&&demand<.9*(n-1)*raptorSea)set_.use(c,s,n-1);else if(n<set_.working(s,13)&&demand>.97*n*raptorSea)set_.use(c,s,n+1);
    }
    // The attitude it meets the air at, engines first: against the velocity it will have at 40 km, the vertical speed it gains
    // falling there (held from the boostback on: no turn over through the apex).
    Vec entry(const State& s)const{
        const double h=norm(s.position)-norm(site_),vz=dot(s.velocity,up_);const Vec flat=s.velocity+up_*-vz;
        return unit(flat*-1.+up_*std::sqrt(vz*vz+2*norm(gravity(s.position))*std::max(h-40e3,0.)));
    }
    Vec boostbackDirection(Vec miss)const{const Vec h=unit(miss*-1);return h*std::cos(boostbackPitch)+up_*std::sin(boostbackPitch);} // miss: horizontal.
    // Body x along d by the grid fins (engines off, in the air), the roll by the RCS.
    void steer(Command& c,const State& s,Vec d)const{finsToward(c,s,d);c.torqueOn=true;c.torque={-2e6*s.rate.x,0,0};}
    // The fins' share of turning body x along d (alone in the coast, beside the gimbal in the landing burn). The air pulls the
    // body back onto the flow with a stiffness K (the body's normal force, ~3.1 a radian on the reference area at the stage's
    // middle, and the fins' own drag at their place, about the centre of mass): the loop's own stiffness three times that (so
    // the tilt asked is the one held), damped at 0.7, and 70% of K cancelled (an error in it leaves the body stable).
    void finsToward(Command& c,const State& s,Vec d)const{
        const Vec e=mul(s.toBody,cross(bodyX(s),d)),w=s.rate,off=mul(s.toBody,cross(bodyX(s),unit(s.velocity)));const double i=pitchInertia(s.propellant),g=cg(s.propellant);
        const double k=s.q*pi*radius*radius*std::max(3.1*(g-boosterLength/2)+3*finDrag*(g-finAt),0.),p=i*.3+3*k,v=1.4*std::sqrt(p*i);
        c.surfaces=finDeflections({0,p*e.y-v*w.y+.7*k*off.y,p*e.z-v*w.z+.7*k*off.z},s.q,g);
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
    // Where a burn lit now (thrust `accel` m/s² against the velocity) slows the fall to gateSpeed: the height over the tower's base;
    // seconds: how long it burns.
    double gateHeight(const State& s,double accel,double* seconds=nullptr)const{
        Vec r=s.position,v=s.velocity;int n=0;fly(r,v,s.mass,accel,.1,[&]{++n;return -dot(v,up_)<=gateSpeed||dot(r+site_*-1,up_)<0;});if(seconds)*seconds=.1*n;return dot(r+site_*-1,up_);
    }
    // The 3 engines' stop from gateSpeed at mass m (their net deceleration floored at 1 m/s²: too heavy to stop on 3).
    static double gate(double m,int engines=3){return gateSpeed*gateSpeed/(2*std::max(.8*engines*raptorSea/m-g0,1.));}
    // The mass to plan the landing with: the booster's now, at most its own with the propellant a boostback leaves.
    static double landingMass(double m){return std::min(m,boosterEmpty+170e3);}
    // Where the fall reaches the gate: ballistic until the planned burn lit there would reach it at its height (checked every
    // 0.5 s below 40 km, interpolated between the last two checks), then that burn: horizontal from the point aimed at (m);
    // seconds: the time to it.
    Vec landing(const State& s,double* seconds=nullptr)const{
        const double mass=landingMass(s.mass),accel=set_.working(s,13)*plan*raptorSea/mass,low=top(s)+gate(mass,set_.working(s,3))+50;Vec r=s.position,v=s.velocity;int n=0;
        auto height=[&](Vec p){return dot(p+site_*-1,up_);};
        auto gated=[&](Vec p,Vec u,double& t){int m=0;fly(p,u,mass,accel,.25,[&]{++m;return -dot(u,up_)<=gateSpeed||height(p)<0;});t=.25*m;return p;};
        Vec at,before;double h=1e300,hb=1e300,t=0,tb=0;
        fly(r,v,mass,0,.5,[&]{++n;if(dot(v,up_)<0&&height(r)<40e3){before=at;hb=h;tb=t;at=gated(r,v,t);h=height(at);if(h<=low)return true;}return height(r)<0&&dot(v,up_)<0;});
        if(!(h<=low)){at=gated(r,v,t);hb=h=height(at);}
        const double f=hb<1e299&&hb>h?std::clamp((hb-low)/(hb-h),0.,1.):1;const Vec p=hb<1e299&&hb>h?before+(at+before*-1.)*f:at;
        if(seconds)*seconds=.5*(n-1)-.5*(1-f)+(hb<1e299&&hb>h?tb+(t-tb)*f:t);const Vec rel=p+site_*-1;return rel+up_*-dot(rel,up_)+aim_*-1.;
    }
    // The drag coefficient engines first (the stage's base table and the fins').
    static double baseDrag(double v,double h){const double m=v/sound(h),x[]{0,.8,1.2,2,5},y[]{1,1.1,1.4,1.2,1};
        for(int i=0;i<4;++i)if(m<=x[i+1])return 3*finDrag+y[i]+(y[i+1]-y[i])*(m-x[i])/(x[i+1]-x[i]);return 1+3*finDrag;}
    double drag(const State& s)const{const double h=norm(s.position)-norm(site_),v=norm(s.velocity);return .5*density(h)*baseDrag(v,h)*pi*radius*radius*v*v/s.mass;}
    // RK4 in ECEF (gravity, the Earth's turning, drag engines first, thrust against the air) until done.
    // upright: within that many m/s of falling at gateSpeed, the thrust straight up (the landing burn's last seconds).
    template<class Done> void fly(Vec& r,Vec& v,double m,double accel,double h,Done done,double dragShare=1,double upright=0)const{
        const Vec w{0,0,7.292115e-5};const double area=pi*radius*radius*dragShare;
        auto acc=[&](Vec p,Vec u){const double rn=norm(p),ht=rn-norm(site_),speed=norm(u);const bool up=upright>0&&-dot(u,up_)-gateSpeed<upright;
            return p*(-earth.mu/(rn*rn*rn))+cross(w,u)*-2.+cross(w,cross(w,p))*-1.+u*(-.5*density(ht)*baseDrag(speed,ht)*area*speed/m)+(up?up_*accel:speed>0?u*(-accel/speed):Vec{});};
        for(int i=0;i<6000&&!done();++i){
            const Vec a1=acc(r,v),v1=v+a1*(h/2),r1=r+v*(h/2),a2=acc(r1,v1),v2=v+a2*(h/2),r2=r+v1*(h/2),a3=acc(r2,v2),v3=v+a3*h,r3=r+v2*h,a4=acc(r3,v3);
            r=r+(v+v1*2+v2*2+v3)*(h/6);v=v+(a1+a2*2+a3*2+a4)*(h/6);}
    }
    Phase phase_=Phase::wait;const Vec east_=cross(north_,up_);Vec back_,aimed_,offshore_=east_*offshore,sea_=east_*sea,aim_=offshore_;
    Vec gated_,gatedV_;double accel_=0,slope_=0,lift_=0,start_=0,miss_=0,seconds_=0,caught_=0,touch_=-1,hover_=0,held_=0,tgo_=10;int age_=0;bool off_=false,catch_=true,hurry_=false;const char* why_=nullptr;
    // The 13 gimballed engines first (the centre 3, then the middle ring), then the outer ones: the burns' engines, failed ones replaced.
    Engines set_{[]{std::vector<int> v;for(int i=1;i<=33;++i)v.push_back(i);return v;}(),boosterNozzle,13};
};
}
