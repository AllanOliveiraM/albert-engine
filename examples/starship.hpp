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
#include "descent.hpp"
#include "gravity_turn.hpp"
#include <algorithm>
#include <bit>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace albert::example::starship {
namespace api=::albert::v1;
constexpr double pi=3.14159265358979323846,deg=pi/180,g0=9.80665,radius=4.5,shipLength=52.1,boosterLength=72.3;
constexpr double raptorSea=280e3*g0,raptorExit=1.33,raptorVacuum=raptorSea+101325*raptorExit,raptorExhaust=350*g0,rvacThrust=258e3*g0*280/230,rvacExhaust=380*g0;
constexpr double pinOut=radius+1; // The catch pins' ends off the axis (m): 1 m out of the hull.
// The axial drag coefficient engines first, by Mach: a blunt base, near a flat disc's above Mach 2 (~1.6), as the descents'
// telemetry shows (Flight 3's booster: 1206 m/s at 25 km down to 858 m/s at 10 km).
constexpr double dragMach[]{0,.8,1.2,2,5},baseDragTable[]{1,1.1,1.5,1.65,1.6};
// The booster's fall engines first as Flight 14's V3 slowed (its bigger fins, the tilt it falls at): 1.8 times that base drag,
// 13.5 km at ~3100 km/h and 7.4 km at ~2000 (the base table alone, from Flight 3's V1: ~3860 and ~2600).
constexpr double descentDrag=1.8;
constexpr double boosterEmpty=275e3,boosterPropellant=3800e3,shipEmpty=100e3,shipPropellant=1600e3,engineMass=1.72e3,reserve=.1*boosterPropellant;
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
inline void aerodynamics(api::StageDefinition* st,double length,double baseScale=1){ // baseScale: the base drag's (engines first).
    auto* a=st->mutable_aerodynamics();a->set_reference_area_meters2(pi*radius*radius);a->set_reference_length_meters(length);a->set_reference_span_meters(2*radius);
    a->set_pitch_damping(-20);a->set_yaw_damping(-20);a->set_roll_damping(-1);st->set_aerodynamic_reference_meters(length/2);
    const std::array<double,5> mach{dragMach[0],dragMach[1],dragMach[2],dragMach[3],dragMach[4]},nose{.35,.4,.65,.5,.35},base{baseDragTable[0]*baseScale,baseDragTable[1]*baseScale,baseDragTable[2]*baseScale,baseDragTable[3]*baseScale,baseDragTable[4]*baseScale};const double side=1.2*length*2*radius/(pi*radius*radius);
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
        e->set_exit_area_meters2(exit);e->set_min_throttle(.4);e->set_gimbal_radians(gimbal);e->set_gimbal_rate_radians_per_second(20*deg);e->set_start_kilograms(100); // A start's spin-up,
        e->set_spool_seconds(.35);}; // and its chamber filling: ~95% of the thrust a second after ignition (an estimate).
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
// shipHeader: the ship's header tanks (the landing's propellant: LOX in the nose, methane in the LOX tank), their centre
// shipHeaderAt m aft of its nose.
constexpr double flapCn=1.2,shipPinAt=12,rcsPulse=.1,shipHeader=50e3,shipHeaderAt=20; // rcsPulse: each RCS valve's shortest opening (s): its jets fire in whole pulses.
inline void shipSystems(api::StageDefinition* s){
    const double area=pi*radius*radius;s->set_thruster_gas_kilograms(6e3); // Its RCS on the tanks' gas (an estimate).
    for(const auto& f:flaps){auto* x=s->add_surfaces();x->set_name(f.name);x->set_limit_radians(45*deg);x->set_rate_radians_per_second(20*deg);put(x->mutable_position_meters(),f.x,f.y,0); // 20°/s: an estimate for actuators this size.
        auto* t=x->mutable_drag();t->add_mach(0);for(int a=-180;a<=180;a+=15)t->add_angle_degrees(a);for(int d=-45;d<=45;d+=15)t->add_deflection_degrees(d);
        for(int a=-180;a<=180;a+=15)for(int d=-45;d<=45;d+=15)t->add_values(flapCn*f.area/area*std::sin((45+d)*deg)*std::abs(std::sin(a*deg)));}
    // The RCS in two groups, by the forward flaps and by the aft ones, never on the heat shield (+z): on each side jets along
    // ±z (pitch, and roll in opposed pairs) and one inward (yaw); on the steel side (-z) jets along ±y (yaw, roll) and one
    // toward the shield (pitch: down in the belly flop).
    for(const double x:{9.,44.}){const double r=x<11?radius*.93:radius;
        for(const double side:{-1.,1.}){
            for(const double push:{-1.,1.}){auto* a=s->add_thrusters();a->set_name("rcs side");put(a->mutable_position_meters(),x,side*r,0);put(a->mutable_direction(),0,0,push);a->set_thrust_newtons(1e4);a->set_exhaust_velocity_meters_per_second(700);a->set_min_pulse_seconds(rcsPulse);}
            auto* b=s->add_thrusters();b->set_name("rcs side in");put(b->mutable_position_meters(),x,side*r,0);put(b->mutable_direction(),0,-side,0);b->set_thrust_newtons(1e4);b->set_exhaust_velocity_meters_per_second(700);b->set_min_pulse_seconds(rcsPulse);
            auto* c=s->add_thrusters();c->set_name("rcs lee");put(c->mutable_position_meters(),x,0,-r);put(c->mutable_direction(),0,side,0);c->set_thrust_newtons(1e4);c->set_exhaust_velocity_meters_per_second(700);c->set_min_pulse_seconds(rcsPulse);}
        auto* d=s->add_thrusters();d->set_name("rcs lee in");put(d->mutable_position_meters(),x,0,-r);put(d->mutable_direction(),0,0,1);d->set_thrust_newtons(1e4);d->set_exhaust_velocity_meters_per_second(700);d->set_min_pulse_seconds(rcsPulse);}
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
constexpr double finAt=7.5,pinAt=finAt+.8,finSpan=4.4,finWidth=5.4,finLimit=30*deg,finSlope=27*2.5/(pi*radius*radius),finDrag=.2;
// How they look (the scene only), from each one's nose `at` m along its stage: stainless steel; the ship's nose a tangent ogive
// (11 m) in sections, black tiles on its windward half (+z), four flaps (hinged on its sides along x: +45° square to the
// flow, -45° folded to the lee) turning with surfaces 1-4 when `moving`, else straight out, square to the hull (riding up the stack, as flown); the booster's vented hot-staging ring
// and its three grid fins (on their radial spindles, with surfaces 1-3 when `moving`); both vehicles' catch pins.
inline void put(api::Vector3* o,const Vec& v){put(o,v.x,v.y,v.z);}
inline void section(api::StageAppearance* a,double from,double to,double r0,double r1,std::uint32_t colour,double tiles){
    auto* x=a->add_sections();x->set_from_meters(from);x->set_to_meters(to);x->set_radius_from_meters(r0);x->set_radius_to_meters(r1);x->set_color(colour);x->set_tiles_radians(tiles);}
inline void tank(api::StageAppearance* a,double from,double to,bool full){auto* t=a->add_tanks();t->set_from_meters(from);t->set_to_meters(to);t->set_full(full);}
inline void hinged(api::StageAppearance* a,const char* name,Vec hinge,Vec axis,Vec out,double span,double width,double thick,unsigned surface,double gain,double offset,std::uint32_t colour,bool grid=false,
    double tipSpan=-1,double tipShift=0,double tipThick=-1){ // tip: the plate's outer edge (negative: as at the hinge).
    auto* x=a->add_parts();if(tipSpan>=0)x->set_tip_span_meters(tipSpan);x->set_tip_shift_meters(tipShift);if(tipThick>=0)x->set_tip_thickness_meters(tipThick);x->set_name(name);put(x->mutable_hinge_meters(),hinge);put(x->mutable_axis(),axis);put(x->mutable_outward(),out);x->set_span_meters(span);x->set_width_meters(width);
    x->set_thickness_meters(thick);x->set_surface(surface);x->set_gain(gain);x->set_offset_radians(offset);x->set_color(colour);x->set_grid(grid);}
constexpr std::uint32_t steel=0xc9c9c6,shield=0x161616,vents=0x3c3c3c;
inline void shipLook(api::StageAppearance* a,double at,bool moving){
    const double l=11,rho=(radius*radius+l*l)/(2*radius);auto r=[&](double x){return std::sqrt(rho*rho-(l-x)*(l-x))+radius-rho;};
    const double cuts[]{0,.4,1.2,2.5,4,6,8.5,11};for(int i=0;i+1<8;++i)section(a,at+cuts[i],at+cuts[i+1],std::max(r(cuts[i]),.05),r(cuts[i+1]),steel,pi);
    section(a,at+l,at+shipLength,radius,radius,steel,pi);tank(a,at+14,at+48,!moving); // Frosted: full riding on the booster.
    for(const double side:{-1.,1.}){ // Forward flaps on the nose, aft ones by the engines; the left one turns the other way.
        const unsigned fwd=side<0?1:2,aft=side<0?3:4;const double g=moving?side:0,o=moving?-side*45*deg:0.; // Still: straight out (deflection +45°, square to the hull); folded: up to -z.
        // Trapezoids thinning out, their leading edges swept back to a shorter tip, the trailing edges square to the body.
        // The forward pair on the nose's ogive: their hinges along its surface (its radius 3.1 m at their front to 4.5 m at their
        // back: 11° in from the axis), turned 20° about the axis toward the lee (V3's: a V seen from the front), each under a fixed
        // fairing along its root over the hinges and actuators. A part's axis is a direction (x forward) where its hinge is a place
        // (x aft of the nose): forward along the ogive, inward. The hinge line a chord of the ogive raised half its sag (0.2 m proud
        // at its ends, in at its middle, under the fairing), so a folded flap lies on the nose instead of sinking into it.
        {const double x0=4.5,x1=11.5,dr=r(x1)-r(x0),len=std::hypot(x1-x0,dr),lee=20*deg,cy=std::cos(lee),sz=std::sin(lee),rm=((r(x0)+r(x1))/2+r((x0+x1)/2))/2;
            const Vec axis{(x1-x0)/len,-side*cy*dr/len,sz*dr/len},out{0,side*cy,-sz},hinge{at+(x0+x1)/2,side*cy*rm,-sz*rm};
            hinged(a,side<0?"flap forward left":"flap forward right",hinge,axis,out,7,3.4,.35,moving?fwd:0,g,o,steel,false,2.6,2.2,.15);
            hinged(a,"flap fairing",hinge+out*-.2,axis,out,7.4,.55,.75,0,0,0,steel,false,6.4,0,.45);}
        hinged(a,side<0?"flap aft left":"flap aft right",{at+44.5,side*radius,0},{1,0,0},{0,side,0},10,5,.4,moving?aft:0,g,o,steel,false,5.5,2.25,.18);
        hinged(a,"pin",{at+shipPinAt,side*(radius+.06),0},{0,side,0},{1,0,0},pinOut-radius-.06,1,1,0,0,0,vents); // Rooted 3 cm beyond the tanks' frost (4.527 m): from the hull it shimmered through it.
        hinged(a,"chine",{at+27.5,side*radius,0},{1,0,0},{0,side,0},33,.5,.25,0,0,0,steel);} // Along its sides, tiles to steel.
}
inline void boosterLook(api::StageAppearance* a,double at,bool moving){
    section(a,at,at+2,radius,radius,vents,0);section(a,at+2,at+boosterLength,radius,radius,steel,0);tank(a,at+6,at+69.3,false);
    for(unsigned i=0;i<3;++i){const Fin f=fins[i];const double r=radius+finSpan/2; // +δ turns it about its spindle, pushing along body x × spindle.
        hinged(a,"grid fin",{at+finAt,f.y*r,f.z*r},{0,f.y,f.z},{0,-f.z,f.y},finSpan,finWidth,.6,moving?i+1:0,1,0,0x2e2e2e,true);}
    for(const double side:{-1.,1.})hinged(a,"catch fitting",{at+finAt+.3,side*(radius+.06),0},{0,side,0},{1,0,0},pinOut-radius-.06,1,1.6,0,0,0,vents); // V3: under the ±y fins' roots.
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
    auto* s1=v.add_stages();cryogens(s1,8e3);s1->set_name("Starship");s1->set_header_kilograms(shipHeader);s1->set_header_meters(shipHeaderAt);mass(s1,combine(ship(true)),combine(ship(false)),shipPropellant,shipLength);raptors(s1,shipLength,false);aerodynamics(s1,shipLength);shipSystems(s1);shipLook(s1->mutable_appearance(),0,true);
    return v;
}
inline api::VehicleDefinition shipAlone(){
    api::VehicleDefinition v;v.set_name("Starship-class ship (approximate)");v.set_crash_speed_meters_per_second(5); /* A steel hull: striking anything faster breaks it up. */v.set_diameter_meters(2*radius);
    auto* s=v.add_stages();cryogens(s,8e3);s->set_name("Starship");s->set_header_kilograms(shipHeader);s->set_header_meters(shipHeaderAt);mass(s,combine(ship(true)),combine(ship(false)),shipPropellant,shipLength);raptors(s,shipLength,false);aerodynamics(s,shipLength);shipSystems(s);
    shipLook(s->mutable_appearance(),0,true);
    return v;
}
inline api::VehicleDefinition superHeavy(){
    api::VehicleDefinition v;v.set_name("Super Heavy-class booster (approximate)");v.set_crash_speed_meters_per_second(5); /* A steel hull: striking anything faster breaks it up. */v.set_diameter_meters(2*radius);
    auto* s=v.add_stages();cryogens(s,20e3);s->set_name("Super Heavy"); /* Its RCS on the tanks' ullage gas: drawn from the propellant. */mass(s,combine(booster(true)),combine(booster(false)),boosterPropellant,boosterLength);raptors(s,boosterLength,true);aerodynamics(s,boosterLength,descentDrag);
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
        auto* a=s->add_thrusters();a->set_name("rcs y");put(a->mutable_position_meters(),3,side*radius,0);put(a->mutable_direction(),0,0,push);a->set_thrust_newtons(3e4);a->set_exhaust_velocity_meters_per_second(700);a->set_min_pulse_seconds(rcsPulse);
        auto* b=s->add_thrusters();b->set_name("rcs z");put(b->mutable_position_meters(),3,0,side*radius);put(b->mutable_direction(),0,push,0);b->set_thrust_newtons(3e4);b->set_exhaust_velocity_meters_per_second(700);b->set_min_pulse_seconds(rcsPulse);}
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
// navSigma: the navigation's position uncertainty (m, 1σ; 0: truth).
// thrustBody: the engines' thrust as they read it, gimballed as their actuators report (body axes, N).
struct State {Vec position,velocity,rate,contact,specific,thrustBody,wind;Mat toBody;double mass=0,propellant=0,thrust=0,height=0,q=0,navSigma=0,windAge=1e9;std::vector<double> surfaces;std::vector<bool> failed;}; // wind: the tower's anemometer's (ECEF, horizontal), windAge s old (1e9: none).
// surfaces: deflections (rad) by surface, in the stage's order; none sent when empty.
// gimbals: engines given their own deflection (rad) past the common one, after it (a roll by differential gimbal).
struct Command {std::vector<int> ignite,cutoff;double throttle=-1,gimbalPitch=0,gimbalYaw=0,vent=-1;std::vector<double> surfaces;Vec torque;bool torqueOn=false,terminate=false; // vent: kg/s (negative: unchanged).
    struct Gimbal {int engine;double pitch,yaw;};std::vector<Gimbal> gimbals;};
// Where an engine's nozzle sits off the axis (body y, z; m), by its number (1-based), as raptors() lays them out.
inline Vec boosterNozzle(int i){const double r=i<=3?.75:i<=13?2.2:3.9,a=(i<=3?(i-1)*120.:i<=13?(i-4)*36.:(i-14)*18+9.)*deg;return{0,r*std::cos(a),r*std::sin(a)};}
inline Vec shipNozzle(int i){const double r=i<=3?1:3.2,a=(i<=3?90+(i-1)*120.:30+(i-4)*120.)*deg;return{0,r*std::cos(a),r*std::sin(a)};}
// The engines a burn uses: the first n still working of an order of preference (1-based), lit and put out by commands; one
// that fails while lit (the engines report it) is left out and the next working one lit in its place on the next use(). Up to
// 3, out of the working ones among the first `balance`, the set whose thrust centre is nearest the body's z axis (the line
// across the catch pins: leaning to bring the thrust through the centre of mass then keeps both pins level), then nearest the
// axis, then the most preferred. Fewer than are lit: the best of those lit (none lit anew, so no start to wait for: an engine
// takes about a second to reach its thrust); more: those lit and the best of the rest. Super Heavy also needs half its gimbal
// moment free after trimming the thrust offset; fixed nozzles only contribute axial thrust.
class Engines {
public:
    Engines(std::vector<int> order,Vec(*nozzle)(int),std::size_t balance=0):order_(std::move(order)),nozzle_(nozzle),balance_(balance),trim_(nozzle==boosterNozzle){}
    void use(Command& c,const State& s,int n){
        std::vector<int> want=choose(s,n);
        for(int e:want)if(std::find(lit_.begin(),lit_.end(),e)==lit_.end()){std::erase(c.cutoff,e);if(std::find(c.ignite.begin(),c.ignite.end(),e)==c.ignite.end())c.ignite.push_back(e);}
        for(int e:lit_)if(std::find(want.begin(),want.end(),e)==want.end()){std::erase(c.ignite,e);if(std::find(c.cutoff.begin(),c.cutoff.end(),e)==c.cutoff.end())c.cutoff.push_back(e);}
        lit_=std::move(want);n_=n;
    }
    void keep(Command& c,const State& s){if(!acceptable(s,lit_)||std::any_of(lit_.begin(),lit_.end(),[&](int e){return failed(s,e);}))use(c,s,n_);} // Fuel changes the trim lever too.
    int count()const{return static_cast<int>(lit_.size());}
    int wanted()const{return n_;}
    int working(const State& s,int n)const{if(trim_)return static_cast<int>(choose(s,n).size());int k=0;for(int e:order_)k+=k<n&&!failed(s,e);return k;} // Only thrust it can balance counts.
    int balanced(const State& s)const{int k=0;for(std::size_t i=0;i<order_.size()&&i<balance_;++i)k+=!failed(s,order_[i]);return k;} // Working among the steering ones.
    int gimballed()const{int n=0;for(int e:lit_)n+=steers(e);return n;}
    Vec centre()const{Vec c;for(int e:lit_)c=c+nozzle_(e);return lit_.empty()?c:c*(1./static_cast<double>(lit_.size()));} // Thrust centre (body y, z).
    // A roll torque (N m) by turning each lit engine across its place about their thrust centre: (y - ȳ, z - z̄) k in pitch and
    // yaw, the least deflections that give it with no net force across (so no pitch or yaw with it, whatever the set), T k Σ
    // |(y, z) - (ȳ, z̄)|² about x; at most `limit` (rad) and what the common gimbal leaves of the 15°, net of the roll the common
    // gimbal itself gives with the thrust centre off the axis (T (ȳ pitch + z̄ yaw)); the torque given (the common's included).
    double roll(Command& c,const State& s,double torque,double limit=6*deg)const{
        if(lit_.empty())return 0;Vec m;int g=0;for(int e:lit_)if(steers(e)){m=m+nozzle_(e);++g;}if(!g)return 0;m=m*(1./g);double spread=0,widest=0;
        for(int e:lit_)if(steers(e)){const Vec d=nozzle_(e)+m*-1.;spread+=d.y*d.y+d.z*d.z;widest=std::max(widest,std::hypot(d.y,d.z));}
        const double each=s.thrust/static_cast<double>(lit_.size()),common=g*each*(m.y*c.gimbalPitch+m.z*c.gimbalYaw);
        if(g<2||!(each*spread>0))return common; // Fixed nozzles give no roll authority.
        const double room=std::max(std::min(limit,15*deg-std::max(std::abs(c.gimbalPitch),std::abs(c.gimbalYaw))),0.)/widest,k=std::clamp((torque-common)/(each*spread),-room,room);
        for(int e:lit_)if(steers(e)){const Vec d=nozzle_(e)+m*-1.;c.gimbals.push_back({e,c.gimbalPitch+k*d.y,c.gimbalYaw+k*d.z});}
        return common+k*each*spread;
    }
private:
    static bool failed(const State& s,int e){return e>=1&&static_cast<std::size_t>(e)<=s.failed.size()&&s.failed[static_cast<std::size_t>(e-1)];}
    bool steers(int e)const{if(!trim_)return true;for(std::size_t i=0;i<order_.size()&&i<balance_;++i)if(order_[i]==e)return true;return false;}
    bool acceptable(const State& s,const std::vector<int>& engines)const{
        if(!trim_)return true;Vec sum;int g=0;for(int e:engines){sum=sum+nozzle_(e);g+=steers(e);}
        static const double full=combine(booster(true)).cg,empty=combine(booster(false)).cg;const double L=boosterLength-(empty+(full-empty)*std::clamp(s.propellant/boosterPropellant,0.,1.));
        return norm(sum)<=.5*g*std::sin(15*deg)*L+1e-9; // Equal T cancels; half the gimbal moment stays for steering, tolerance only for symmetric roundoff.
    }
    std::vector<int> choose(const State& s,int n)const{
        std::vector<int> on,best,pool,base;for(int e:lit_)if(!failed(s,e))on.push_back(e);
        if(n<1)return best;
        const bool fewer=!on.empty()&&n<=static_cast<int>(on.size()); // From those lit; else those and some of the rest.
        if(fewer)pool=on;else{base=on;for(std::size_t i=0;i<order_.size()&&i<balance_;++i)if(!failed(s,order_[i])&&std::find(on.begin(),on.end(),order_[i])==on.end())pool.push_back(order_[i]);}
        if(n>3){auto fill=acceptable(s,base)?base:std::vector<int>{};best=fill; // Rebuild an unsafe survivor set; only a safe snapshot can be lit.
            const auto& from=fewer?on:order_;
            while(static_cast<int>(fill.size())<n){Vec c;for(int e:fill)c=c+nozzle_(e);int chosen=-1;double least=1e300;
                for(std::size_t i=0;i<from.size();++i){const int e=from[i];if(failed(s,e)||std::find(fill.begin(),fill.end(),e)!=fill.end())continue;
                    const Vec d=c+nozzle_(e);const double x=10*std::abs(d.y)+std::abs(d.z)+1e-3*static_cast<double>(i);if(x<least){least=x;chosen=e;}}
                if(trim_&&static_cast<int>(fill.size())+2<=n&&(chosen<0||!steers(chosen))){int a=-1,b=-1;double score=1e300;
                    for(std::size_t i=0;i<from.size();++i)for(std::size_t j=i+1;j<from.size();++j){const int e=from[i],f=from[j];
                        if(steers(e)||steers(f)||failed(s,e)||failed(s,f)||std::find(fill.begin(),fill.end(),e)!=fill.end()||std::find(fill.begin(),fill.end(),f)!=fill.end()||norm(nozzle_(e)+nozzle_(f))>1e-9)continue;
                        const Vec d=c+nozzle_(e)+nozzle_(f);const double x=10*std::abs(d.y)+std::abs(d.z)+1e-3*static_cast<double>(i+j);
                        if(x<score){score=x;a=e;b=f;}}
                    if(a>0){fill.push_back(a);fill.push_back(b);if(acceptable(s,fill))best=fill;continue;}} // Prefer opposed fixed nozzles to a one-sided addition.
                if(chosen<0)break;fill.push_back(chosen);if(acceptable(s,fill))best=fill;}
            return best;}
        auto exhaustive=[&](const std::vector<int>& from,const std::vector<int>& held,int target){double least=1e300;std::vector<int> trial=held;Vec sum;for(int e:held)sum=sum+nozzle_(e);best.clear();
            auto search=[&](auto&& self,std::size_t at,int left,Vec c,double rank)->void{if(left==0){if(!acceptable(s,trial))return;const double x=(10*std::abs(c.y)+std::abs(c.z))/target+1e-3*rank;if(x<least){least=x;best=trial;}return;}
                for(std::size_t i=at;i<from.size()&&from.size()-i>=static_cast<std::size_t>(left);++i){trial.push_back(from[i]);self(self,i+1,left-1,c+nozzle_(from[i]),rank+static_cast<double>(i));trial.pop_back();}};
            const int left=target-static_cast<int>(held.size());if(left>=0)search(search,0,left,sum,0.);};
        exhaustive(pool,base,n);if(!best.empty())return best; // Preserve the nominal 13-to-3 choice and avoid needless starts.
        pool.clear();for(int e:order_)if(!failed(s,e))pool.push_back(e);
        for(int target=std::min(n,static_cast<int>(pool.size()));target>0;--target){exhaustive(pool,{},target);if(!best.empty())return best;}return best; // Replacements before a smaller set; no safe pair means no ignition.
    }
    std::vector<int> order_,lit_;Vec(*nozzle_)(int);std::size_t balance_;bool trim_;int n_=0;
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
    // The arms as their angle sensors read them (rad about the up axis from closed: the south one's negative, the north one's
    // positive opening); each a beam from its hinge (east, north from the site's base) as long, as round (its rails above it
    // included) and as high (bottom, top) as the site gives: never struck off their line (on it, where the pins go on them, the
    // catch's own laws keep the hull clear of them).
    // open, rate: a catch arm's open angle and swing rate (the first two; others fixed): it swings toward closed while the
    // guidance has them close round it (closeArms()), else back open.
    struct Arm {double e,n,length,radius,bottom,top,angle=0;const char* name="";double open=0,rate=0;};std::vector<Arm> arms;
    // What a collision course runs into, by collides()'s index: a column's or an arm's reason for getting away.
    const char* into(int k)const{return k<0?nullptr:static_cast<std::size_t>(k)<columns.size()?columns[static_cast<std::size_t>(k)].name:static_cast<std::size_t>(k)-columns.size()<arms.size()?arms[static_cast<std::size_t>(k)-columns.size()].name:"";}
    bool closeArms()const{return close_;} // Centred between them, going for the catch: the arms round it.
    // A disturbance observer: across, what pushes it besides its engines (the IMU's specific force less their thrust as they
    // read it, gimballed as their actuators report), over 0.5 s (m/s²): the wind's push and the air's, an outside force; the
    // low phases ask that much less. Nought outside them; touching the arms (their push is not the air's), kept as it was.
    void observe(const State& s,double dt,bool low){
        if(dt>0){clock_+=dt;dt_=dt;}if(armsClosed){if(shutAt_<0)shutAt_=clock_;}else shutAt_=-1; // When the arms came to rest shut round it.
        if(!(dt>0)||!low){push_={};seenV_=0;return;}
        if(norm(s.contact)>0||!(s.mass>0))return;
        const Vec f=tmul(s.toBody,s.specific+s.thrustBody*(-1/s.mass)),h=f+up_*-dot(f,up_);
        push_=seenV_>0?push_+(h+push_*-1.)*std::min(dt/.5,1.):h;seenV_=1;
    }
    Vec disturbance()const{return push_;}
    // The site's works the hull must never strike, upright cylinders (east and north of their axis from the site's base, radius,
    // bottom and top over it; m): the tower's column with its crown and mast, the ship's arm, the tank farm; and those it must
    // never come down on, boxes (east and north spans, m): the pad with the mount on it, the tank farm. The mission's site.
    struct Column {double e,n,radius,bottom,top;const char* name;};struct Works {double e0,e1,n0,n1;};
    std::vector<Column> columns;std::vector<Works> works;
    // Over the ground's works, or within `margin` m of them (flat: from the site's base, horizontal).
    bool overWorks(Vec flat,double margin=0)const{const Vec east=cross(north_,up_);const double e=dot(flat,east),n=dot(flat,north_);
        return std::any_of(works.begin(),works.end(),[&](const Works& w){return e>w.e0-margin&&e<w.e1+margin&&n>w.n0-margin&&n<w.n1+margin;});}
protected:
    static constexpr double clearance=1.5; // The hull kept that far off the columns (m).
    // The least gap (m) between its hull and a column (which: `which`), the body leaning `a` along u (horizontal unit: the nose
    // along u, the base against it, swinging about the centre of mass) after moving d m along w and rising `rise` m.
    // ahead: the catch arms where their swing has them that many seconds on.
    double gap(const State& s,Vec flat,Vec u,double a,Vec w={},double d=0,double rise=0,int* which=nullptr,double ahead=0)const{
        const Vec east=cross(north_,up_);const double e=dot(flat,east)+dot(w,east)*d,n=dot(flat,north_)+dot(w,north_)*d,over=dot(s.position+site_*-1.,up_)+rise;
        const bool line=std::abs(n)<3&&e>-6&&e<10; // Between the arms where the catch hovers (the open arms 7 m off the hull centred, 4 m at 3 m off).
        const double ue=dot(u,east),un=dot(u,north_),nose=cg(s.propellant),base=length_-nose,sa=std::sin(a),ca=std::cos(a);double m=1e9;
        for(double x=-base;x<=nose+1e-9;x+=2){const double z=over+x*ca,pe=e+ue*x*sa,pn=n+un*x*sa;
            for(std::size_t i=0;i<columns.size();++i){const auto& c=columns[i];
                if(z>=c.bottom-radius&&z<=c.top+radius){const double g=std::hypot(pe-c.e,pn-c.n)-c.radius-radius;if(g<m){m=g;if(which)*which=static_cast<int>(i);}}}
            // The catch arms (the first two) but on their line, in their grip, or while the catch closes them round it (it has them
            // open again once it leaves the line: they are then predicted opening, away from it).
            for(std::size_t i=0;i<arms.size();++i){const auto& r=arms[i];if((i<2&&(line||close_||grip_))||z<r.bottom-radius||z>r.top+radius)continue;
                const double turn=r.rate*ahead,at=r.open==0?r.angle:close_?(r.open>0?std::max(r.angle-turn,0.):std::min(r.angle+turn,0.)):(r.open>0?std::min(r.angle+turn,r.open):std::max(r.angle-turn,r.open));
                const double de=std::cos(at),dn=std::sin(at),t=std::clamp((pe-r.e)*de+(pn-r.n)*dn,0.,r.length); // The beam's nearest point.
                const double g=std::hypot(pe-r.e-de*t,pn-r.n-dn*t)-r.radius-radius;if(g<m){m=g;if(which)*which=static_cast<int>(columns.size()+i);}}}
        return m;
    }
    // The most the body may lean along u (rad, within `most`) without its swinging hull coming within `clearance` of a column (or
    // nearer than it already is, when nearer).
    double lean(const State& s,Vec flat,Vec u,double most)const{
        if(columns.empty()||!(norm(u)>0))return most;const Vec h=unit(u);const double need=std::min(clearance,gap(s,flat,h,0));double a=0;
        for(double t=.5*deg;t<=most+1e-9;t+=.5*deg){if(gap(s,flat,h,t)<need)break;a=t;}return a;
    }
    // Whether its course runs the hull within `margin` m of a column or an arm in the next `horizon` s (by default the hard
    // clearance and a metre's warning: the course already brakes after the lag, and the open arms leave the hull 7 m on the
    // axis by design): pushed by what the observer measures all along, moving as it does for `lag` s (the attitude turning a
    // lean of the other way round: ~2 s at its bandwidth), then braking against its way across at `brake` m/s², upright, its
    // vertical speed held: that one's index, or -1. floor: the height (over the site) it is braking its fall to, coming down no
    // further. stopping(): what it can brake across with, the engines it has lit, leaned as the hull's swing allows here and
    // where the lag takes it (the nearer the tower, the less).
    double stopping(const State& s,Vec flat,int lit,double most,double lag=2)const{
        const Vec across=s.velocity+up_*-dot(s.velocity,up_),way=norm(across)>0?across*-1.:cross(north_,up_);
        return std::min(most,lit*raptorSea/s.mass)*std::sin(std::min(lean(s,flat,way,30*deg),lean(s,flat+across*lag,way,30*deg)));
    }
    int collides(const State& s,Vec flat,double brake,double floor,double margin=clearance+1,double horizon=12)const{const double react=escapeReact,swing=escapeSwing;
        if(columns.empty()&&arms.empty())return -1;const double vz=dot(s.velocity,up_),over=dot(s.position+site_*-1.,up_);Vec v=s.velocity+up_*-vz,at;
        const Vec f=tmul(s.toBody,s.specific),thrust=norm(s.contact)>0||!laggedThrust?Vec{}:f+up_*-dot(f,up_)+push_*-1.; // The thrust's push across now, kept up while the attitude turns (less and less over the lag): laggedThrust.
        for(double t=0;t<=horizon+1e-9;t+=.1){int k=-1;const double d=norm(at),rise=vz<0?std::max(vz*t,std::min(floor-over,0.)):vz*t;
            if(gap(s,flat,{},0,d>0?at*(1/d):Vec{},d,rise,&k,t)<margin){hitAt_=flat+at;hitT_=t;hitRise_=rise;return k;}
            const double speed=norm(v);if(t>react&&speed<.05&&norm(push_)<brake)break; // Stopped, and held there.
            // The escape as flown: its push now kept up for `react` s, then the braking taken up as the attitude swings onto it (time
            // constant `swing`), each vehicle's as its escapes were measured (escapeReact, escapeSwing).
            const double onto=t<react?0:1-std::exp(-(t-react)/swing);
            const Vec a=push_+thrust*(1-onto)+(speed>0?v*(-std::min(brake*onto,speed/.1)/speed):Vec{});at=at+v*.1+a*.005;v=v+a*.1;}
        return -1;
    }
    // Which way to get away (horizontal unit): toward `aim` (from it to the vehicle: flat - aim, as goal() gives it), pushed off
    // the columns within 30 m of the hull, the nearer the harder.
    Vec away(const State& s,Vec flat,Vec to)const{if(grip_)return {}; // In the arms' grip: straight up (escape() with no way across).
        Vec d=norm(to)>0?unit(to*-1.):cross(north_,up_);const Vec east=cross(north_,up_);const double e=dot(flat,east),n=dot(flat,north_);
        for(const auto& c:columns){const Vec off=east*(e-c.e)+north_*(n-c.n);const double r=norm(off)-c.radius-radius;if(r<30&&norm(off)>0)d=d+unit(off)*(2*(1-std::max(r,0.)/30));}
        for(std::size_t i=0;i<arms.size();++i){const auto& r=arms[i];if(i<2&&std::abs(n)<3&&e>-6&&e<10)continue;const double de=std::cos(r.angle),dn=std::sin(r.angle),t=std::clamp((e-r.e)*de+(n-r.n)*dn,0.,r.length);
            const Vec off=east*(e-r.e-de*t)+north_*(n-r.n-dn*t);const double g=norm(off)-r.radius-radius;if(g<15&&norm(off)>0)d=d+unit(off)*(2*(1-std::max(g,0.)/15));}
        (void)s;return unit(d);
    }
    // Getting away with all it has (up to `most` m/s² of thrust):
    // - able to hold itself up: across away along `dir` (horizontal unit) at up to 15 m/s (that speed's error at 0.5/s) with
    //   the observer's push netted; up, climbing at 1 m/s, more only as the lean (as far as the hull's swing lets it, within
    //   30°) needs the vertical thrust to give that much across (a climb is no harm; striking the tower is);
    // - short of it (it falls whatever it does): all of it at cos θ = T/W along dir, the lean that carries the fall farthest
    //   across (h k / √(1 - k²) from height h).
    Vec escape(const State& s,Vec flat,Vec dir,double most)const{
        const double g=norm(gravity(s.position)),vz=dot(s.velocity,up_);
        if(most<g){const double a=std::min(lean(s,flat,dir,30*deg),std::acos(std::clamp(most/g,0.,1.)));return up_*(most*std::cos(a))+dir*(most*std::sin(a));}
        const Vec across=s.velocity+up_*-vz,side=(dir*15.+across*-1.)*.5+push_*-1.;const double want=norm(side);if(!(want>0))return up_*std::min(g+.5*(1-vz),most);
        const double a=std::max(lean(s,flat,side,30*deg),.5*deg),up=std::clamp(std::max(g+.5*(1-vz),want/std::tan(a)),0.,most*std::cos(a));
        const double l=std::min(want,up*std::tan(a));const Vec thrust=up_*up+side*(l/want);
        // More than about its weight only once the body points there (within 5°): a lean still left over from what it was
        // doing, under a sudden 3 g, would throw it across, maybe at what it is getting away from; but never less than what
        // stops its fall, with 20% to spare (g + v²/2h, on the body's own lean), over what is under its base: the ground, or the
        // top of a column or an arm within its reach (beside it by less than the hull and 10 m).
        const double off=std::acos(std::clamp(dot(bodyX(s),unit(thrust)),-1.,1.)),h=std::max(dot(s.position+site_*-1.,up_)-(length_-cg(s.propellant))-under(flat),1.);
        const double stop=vz<0?1.2*(g+vz*vz/(2*h))/std::max(dot(bodyX(s),up_),.5):0.;
        const double cap=std::max(g*(1.1+(most/g-1.1)*std::clamp((10*deg-off)/(5*deg),0.,1.)),std::min(stop,most));
        return norm(thrust)>cap?thrust*(cap/norm(thrust)):thrust;
    }
    // The highest top within the hull's reach (m over the site's base): a column's or an arm's beside it by less than its radius
    // and 10 m; else the ground (0).
    double under(Vec flat)const{
        const Vec east=cross(north_,up_);const double e=dot(flat,east),n=dot(flat,north_);double top=0;
        for(const auto& c:columns)if(std::hypot(e-c.e,n-c.n)<c.radius+radius+10)top=std::max(top,c.top);
        for(const auto& r:arms){const double de=std::cos(r.angle),dn=std::sin(r.angle),t=std::clamp((e-r.e)*de+(n-r.n)*dn,0.,r.length);
            if(std::hypot(e-r.e-de*t,n-r.n-dn*t)<r.radius+radius+10)top=std::max(top,r.top);}
        return top;
    }
    // The escape's thrust before the cap on a lean not yet turned (escape()'s): what its engines are lit for.
    Vec escapeFull(const State& s,Vec flat,Vec dir,double most)const{
        const double g=norm(gravity(s.position)),vz=dot(s.velocity,up_);if(most<g)return escape(s,flat,dir,most);
        const Vec across=s.velocity+up_*-vz,side=(dir*15.+across*-1.)*.5+push_*-1.;const double want=norm(side);if(!(want>0))return up_*std::min(g+.5*(1-vz),most);
        const double a=std::max(lean(s,flat,side,30*deg),.5*deg),up=std::clamp(std::max(g+.5*(1-vz),want/std::tan(a)),0.,most*std::cos(a));
        return up_*up+side*(std::min(want,up*std::tan(a))/want);
    }
    double share(double p)const{return std::clamp(p/propellant_,0.,1.);}
    // Its mass properties with p kg left, as the physics places it: header tanks (header_ kg at headerAt_) hold the last of it.
    MassProperties massAt(double p)const{
        auto blend=[](const MassProperties& a,const MassProperties& b,double f){return MassProperties{a.mass+(b.mass-a.mass)*f,a.cg+(b.cg-a.cg)*f,a.ix+(b.ix-a.ix)*f,a.iy+(b.iy-a.iy)*f};};
        if(header_>0&&header_<propellant_){
            auto headed=[&](double q){const double m=empty_.mass+q,c=(empty_.mass*empty_.cg+q*headerAt_)/m,de=empty_.cg-c,dh=headerAt_-c;return MassProperties{m,c,empty_.ix,empty_.iy+empty_.mass*de*de+q*dh*dh};};
            return p<=header_?headed(std::max(p,0.)):blend(headed(header_),full_,std::clamp((p-header_)/(propellant_-header_),0.,1.));}
        return blend(empty_,full_,share(p));
    }
    double cg(double p)const{return massAt(p).cg;}
    double pitchInertia(double p)const{return massAt(p).iy;}
    double rollInertia(double p)const{return massAt(p).ix;}
    double header_=0,headerAt_=0; // Header tanks (kg) and their centre (m aft of the nose).
    static Vec bodyX(const State& s){return s.toBody.r0;}
    static Vec gravity(Vec r){return r*(-earth.mu/std::pow(norm(r),3));}
    // Thrust along d (ECEF) by the gimbal, the gain from the thrust's lever on the inertia (natural frequency: bandwidth()); offset:
    // the lit engines' thrust centre off the axis (body y, z), its line turned through the centre of mass beforehand and the body
    // leaned so that line, not the body's axis, runs along d.
    // The natural frequency within what the gimbal can do: 1.2 rad/s, less once 15° of it cannot turn the body through a quarter
    // radian at that rate (few engines, low throttle), down to 0.4. fine: sized for the small corrections of a precision phase
    // (a twentieth of a radian), up to 1.6 rad/s (the gimbals swing at 20°/s: at 2 the lean fell into a limit cycle): what aim()'s fine law flies.
    double bandwidth(const State& s,double thrust=-1,bool fine=false)const{ // thrust: another than its own now (N).
        const double lever=std::max(length_-cg(s.propellant),1.);return std::clamp(std::sqrt((thrust<0?s.thrust:thrust)*lever*std::sin(15*deg)/(pitchInertia(s.propellant)*(fine?.05:.25))),.4,fine?1.6:1.2);
    }
    // fine: the same PD at the fine bandwidth, as a rate asked (k/b of the error) within what it can still stop from at half its
    // gimbals' turning (√(α e)) and b of that rate's error: quick on small corrections (the lean's lag ~1.1 s, not ~1.8: the
    // loops across it were damped 0.33 and swung the ship a metre over the arms), a large one not overshot.
    void aim(Command& c,const State& s,Vec d,Vec offset={},bool fine=false)const{
        const double lever=std::max(length_-cg(s.propellant),1.),inertia=pitchInertia(s.propellant),gain=inertia/std::max(s.thrust*lever,1.),f=bandwidth(s,-1,fine),k=f*f,b=1.67*f;
        d=unit(d+s.toBody.r1*(offset.y/lever)+s.toBody.r2*(offset.z/lever));const Vec e=mul(s.toBody,cross(bodyX(s),d)),w=s.rate;
        const double most=std::max(s.thrust,1.)*lever*std::sin(15*deg)/inertia;
        auto turn=[&](double x,double r){if(!fine)return k*x-b*r;const double lim=std::sqrt(most*std::abs(x));return b*(std::clamp(k/b*x,-lim,lim)-r);};
        const double py=gain*turn(e.y,w.y),pz=gain*turn(e.z,w.z);
        c.gimbalPitch=std::clamp(py-offset.z/lever,-15*deg,15*deg);c.gimbalYaw=std::clamp(pz+offset.y/lever,-15*deg,15*deg);
        c.torqueOn=true;c.torque={-2e6*w.x,0,0}; // The RCS keeps the roll still while the engines steer.
        unmet_=Vec{0,py-offset.z/lever-c.gimbalPitch,pz+offset.y/lever-c.gimbalYaw}*(s.thrust*lever); // Past the gimbal's 15° (N m): the fins' share.
    }
    void hold(Command& c,const State& s,Vec d)const{ // By the RCS alone (engines off): body x along d, no roll; from nearly
        // opposite it, over in pitch (the turn's axis is lost there).
        Vec e=mul(s.toBody,cross(bodyX(s),d));const Vec w=s.rate;const double i=pitchInertia(s.propellant);if(dot(bodyX(s),d)<0&&norm(e)<.5)e={0,1,0};
        c.torqueOn=true;c.torque={rollInertia(s.propellant)*(.04*rollError(s)-.4*w.x),i*(.04*e.y-.4*w.y),i*(.04*e.z-.4*w.z)}; // The roll too: its pins to the arms by the time the fins bite.
    }
    // Unit a turned toward unit b by at most `most` (rad): an attitude asked, eased.
    static Vec turned(Vec a,Vec b,double most){
        const double c=std::clamp(dot(a,b),-1.,1.),t=std::acos(c);if(t<=most||!(norm(b+a*-c)>1e-9))return b;return a*std::cos(most)+unit(b+a*-c)*std::sin(most);}
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
    // (one or two engines turn the body slowly: a faster loop across beats it): toward it at the speed it can still brake to rest
    // there, a third of what leaning against its way (within `most` and the hull's swing clear of the tower) gives, from where its
    // velocity loop's lag (1/kv) leaves it; at most `fastest` m/s and `gain` kv of the distance a second. hard: braking at that
    // (m/s²) while farther than `mouth` m, the gentle braking only within it.
    Vec onto(const State& s,Vec to,double kv=-1,double fastest=8,double most=12*deg,double hard=0,double mouth=0,double gain=.375)const{
        if(kv<0)kv=std::min(.4,bandwidth(s)/3);const double d=norm(to);const Vec across=s.velocity+up_*-dot(s.velocity,up_);if(!(d>0))return across*-kv;
        const Vec u=to*(1/d);const double toward=std::max(-dot(across,u),0.),brake=norm(gravity(s.position))*std::tan(lean(s,s.position+site_*-1.+up_*-dot(s.position+site_*-1.,up_),u,most))/3;
        const double left=std::max(d-toward/kv,0.),near=hard>0?std::min(left,mouth):left;
        const double speed=std::min({fastest,gain*kv*d,std::sqrt(2*brake*near+2*hard*(left-near))});return (u*-speed+across*-1)*kv;
    }
    // From where to head across (an offset from it, like flat): the arms open eastward into a V and the hull must not cross
    // them, so off their line (more than 4 m to the side) first onto it at the corridor point (corridor m east of the axis),
    // then along it onto the axis; from west of that, never over the tower: out to 30 m beside its line, east along there,
    // then in.
    // Inside the open arms' V (their inner edges from the hinges hingeNorth m off the line hingeEast m east of the axis,
    // openAngle out from it, and their lines on past the tips; the hull's radius, the arm's beam and the collision monitor's
    // margin clear: collides()'s 2.5 m): free to head straight onto the axis (the line there runs between the arms).
    // How much over the hover point it keeps at flat (m): off the arms' line and out of their V (or within 5 m of an arm's beam,
    // or of where one swings), its base 5 m over them (stand: its centre of mass over its base); between them, nothing more.
    double aloft(const State& s,Vec flat,double stand)const{
        const double e=dot(flat,cross(north_,up_)),n=std::abs(dot(flat,north_)),edge=hingeNorth+(e-hingeEast)*std::tan(openAngle)-n-radius-1.5;
        const bool between=(n<=4&&e>-8&&(n<=1||edge>5))||(in_&&edge>5); // Past the arms' tips is not between them: onto their line first.
        return between?0.:std::max(arms_+armTop+5+stand-(catchHeight(s)+hoverAbove),0.);
    }
    // margin: that much further in (m; negative: out).
    bool inside(Vec flat,double margin=0)const{
        const double e=dot(flat,cross(north_,up_)),n=std::abs(dot(flat,north_));
        return e>-8+margin&&n<hingeNorth+(e-hingeEast)*std::tan(openAngle)-radius-1.5-(clearance+1)-margin;
    }
    // Inside the V or not, decided once a step with a metre either way (the edge crossed back and forth does not flip the
    // way it heads or the height it keeps).
    void route(Vec flat){in_=inside(flat,in_?-1.:1.);}
    double hingeEast=-15.75,hingeNorth=6.3,openAngle=30*deg; // Starbase's (starship_flight.hpp's armPivot, the arms' places, armOpen).
    // Off their line east of the tower: past the arms' tips (their beams swing out to 13 m east; the hull, a beam and the
    // monitor's margin clear of that) straight across onto the line where it is, then along it; nearer than that, out east past
    // them first (never across a tip on the slant).
    Vec approach(Vec flat)const{
        const Vec east=cross(north_,up_);const double e=dot(flat,east),n=dot(flat,north_),past=std::max(corridor,13+radius+1.5+clearance+1);
        if((std::abs(n)<=4&&e>-8)||in_)return flat; // On their line or inside their V (the hull clear of the tower's face down to 8 m west of the axis).
        if(e<-8)return std::abs(n)<25?north_*(n-(n>=0?30:-30)):east*(e-10);
        return e>=past?north_*n:flat+east*-past+north_*-(std::abs(n)>4?0:n); // Across onto the line where it is; else out east past the tips.
    }
    // The hover's velocity loop at the attitude's bandwidth over this: half (its slow motion over the arms leaves the attitude
    // loop the room a third gives a moving vehicle; the landing bench's sweep: 8 m/s of wind across caught at 2, not at 3).
    double hoverSplit=2;
    bool holdAlong=false; // Hovering within 3 m of the axis along the arms: held there, only centred across them (the booster's).
    double slide=8; // The fastest it goes across while hovering (m/s): the ship's from in front of the V into it, slower.
    double corridor=15,towerTop=146,armTop=.25; // towerTop: over its base; armTop: the arms' rails over their height (m).
    // Over the arms, ready to go down onto them: its pins within 0.18 m of the grooves' line (inside the bars' crests 0.2 m out a
    // pin slides down into the groove: what the arms catch, not a finer aim held over them on its engines; its drift a second on
    // asked as well held a booster swaying in a gust over them until its propellant ran out); along the rails within 4 m (they are
    // 20 m long); drifting under 0.4 m/s, its roll within 5° (the pins along the rails then, 0.5 m at most).
    bool aligned(const State& s,Vec flat)const{const Vec p=pinsAt(s,flat);
        return std::abs(dot(p,north_))<.18&&std::abs(dot(p,cross(north_,up_)))<4&&norm(s.velocity+up_*-dot(s.velocity,up_))<.4&&std::abs(rollError(s))<5*deg&&std::abs(s.rate.x)<.05;
    }
    // On `engines` engines: the hover hoverAbove over the catch height, or the descent at `rate` m/s, over the axis and aligned; the
    // thrust it asks for (N).
    // aloft: that much higher (m), its base over the arms (aloft()) while it is not yet where it may come down to them.
    double overArms(Command& c,const State& s,Vec flat,double over,bool descend,int engines,Vec offset={},const Engines* by=nullptr,double rate=1,double aloft=0)const{
        const double vz=dot(s.velocity,up_),vertical=std::max((descend?.7*(-rate-vz):.2*(catchHeight(s)+hoverAbove+aloft-over)-.8*vz)+norm(gravity(s.position)),0.);
        // Its centre of mass onto where its pins come over the arms' line with the leans it holds: against what the observer
        // measures pushing it (push/g: push (cg - pin)/g downwind) and to bring an off-axis thrust centre's line through the
        // centre of mass (offset/lever: the pins carried offset (cg - pin)/lever that way). Steering the pins themselves would
        // read the lean's own swing of them as the way done, and drift with the wind. Its lean within 12° and the hull's swing
        // clear of the tower. Going down onto them, along the rails (20 m: anywhere on them will do) held where it is: still
        // along them as the pins land, nothing to swing on them after.
        const double above=cg(s.propellant)-pin_,lever=std::max(length_-cg(s.propellant),1.);const Vec tilt=s.toBody.r1*offset.y+s.toBody.r2*offset.z;
        Vec at=flat+push_*(-above/std::max(norm(gravity(s.position)),1.))+(tilt+up_*-dot(tilt,up_))*(above/lever);if(const Vec east=cross(north_,up_);descend||(holdAlong&&std::abs(dot(at,east))<3))at=at+east*-dot(at,east); // holdAlong: within 3 m of the axis along them, held there too: only centred across.
        // And a slow integral of how far it sits off that point (10 s against the position loop's own gain, 0.375 kv²; at most
        // 0.5 m/s² of push): the steady error a lag or an unmodelled push leaves (a PD attitude loop under the wind's moment on the
        // hull leans the thrust a little short of what is asked). Begun afresh each time it comes over the arms; gathered only
        // within two seconds' slide of that point (the ship's hover begins in front of the V, tens of metres out); the
        // booster's within 1.5 m (gathered on its quicker way in, it held the pins half a metre off the line half a minute in a gust).
        const double kv=std::min(.5,bandwidth(s,-1,fineAim)/hoverSplit),ki=.375*kv*kv/10;if(clock_-settleAt_>.1)settle_={};settleAt_=clock_;
        if(const Vec off=at+up_*-dot(at,up_);norm(off)<(holdAlong?1.5:std::max(3.,2*slide))){settle_=settle_+off*dt_;const double most=.5/ki;if(norm(settle_)>most)settle_=settle_*(most/norm(settle_));} // Only within 2 s of it at its slide: not wound up on the way there.
        // And the acceleration across it really gets (the IMU's, its lean and the air's push together) held to what it asks, by an
        // integral of the shortfall at 0.15/s (at most 1 m/s²; within the attitude's lag): a PD attitude under the wind's moment on the hull leans the thrust
        // short of what is asked, which the slow integral above takes half a minute to make up (a hover in an 8 m/s wind spent its
        // propellant so, a metre off the line).
        const Vec asked=onto(s,approach(at),kv,slide)+settle_*-ki;
        {const Vec f=tmul(s.toBody,s.specific),got=f+up_*-dot(f,up_);if(clock_-gotAt_>.1)gotInt_={};gotAt_=clock_;
            if(norm(s.contact)==0){gotInt_=gotInt_+(asked+got*-1.)*(.15*dt_);if(norm(gotInt_)>1)gotInt_=gotInt_*(1/norm(gotInt_));}}
        Vec side=asked+push_*-1.+gotInt_;
        const double limit=std::tan(lean(s,flat,side,12*deg))*vertical;
        if(std::getenv("ALBERT_HOVER")&&std::fmod(clock_+1e-9,1.)<dt_){const Vec east=cross(north_,up_);std::printf("HOVER at %.2f %.2f side %.3f %.3f limit %.3f lean %.2f° push %.3f %.3f settle %.3f %.3f ki %.4f kv %.2f\n",dot(at,east),dot(at,north_),dot(side,east),dot(side,north_),limit,lean(s,flat,side,12*deg)/deg,
            dot(push_,east),dot(push_,north_),dot(settle_,east)*ki,dot(settle_,north_)*ki,ki,kv);}
        if(norm(side)>limit)side=side*(limit/norm(side));
        const Vec thrust=up_*vertical+side;const double want=s.mass*norm(thrust);c.throttle=std::clamp(want/(engines*raptorSea),.4,1.);
        aim(c,s,unit(thrust),offset,fineAim);roll(c,s,by);return want;
    }
    // Centred between the arms' rails (0.8 m off the hull) on their line, within their span along it (4 m west to 8 m east of
    // the axis: the pins then land on the 20 m rails) and
    // coming in slowly: they may close round it (in the 4 s it takes them it gets over the axis); once closing, until it drifts
    // 0.6 m off their line.
    // To start closing: where it drifts while they swing shut (closing: as long as they take) still between the rails' inner
    // bars and the hull (0.55 m each side), its drift across slow; closing, until its pins leave the rails, or its hull rubs
    // them with the pins not yet in the grooves (they open again for another go).
    double closing=30*deg/(8*deg); // How long the arms take to swing shut (s).
    // descending: going down onto them (a touch then is the pins').
    bool centred(const State& s,Vec flat,bool descending=false,double room=.6)const{flat=pinsAt(s,flat);
        const Vec v=s.velocity+up_*-dot(s.velocity,up_);const double n=dot(flat,north_),e=dot(flat,cross(north_,up_)),vn=dot(v,north_);
        if(close_)return grip_||(onRails(flat)&&!(norm(s.contact)>0&&armsClosed&&!inGrooves(flat)&&!descending)); // In their grip: kept shut.
        // Sliding in from the front of their V (as the real catches: the arms swing as it comes in), where it will be once they
        // are shut: across, still on the line; along, braking to rest (half its speed over that time) within the rails' span.
        const double ve=dot(v,cross(north_,up_)),along=e+ve*closing/2;
        // Across, within the rails' room round the hull (0.55 m each side) with half its drift over that time (the loop takes the
        // rest out as they swing): the pins' grooves (0.15 m) matter only going down.
        if(!holdAlong)return std::abs(n)<.3&&std::abs(n+vn*closing)<.3&&std::abs(vn)<.1&&e>-4&&e<8&&norm(v)<3; // The ship: over the axis, still.
        return std::abs(n)<room&&std::abs(n+vn*closing/2)<room&&std::abs(vn)<room*2/3&&along>-4&&along<8&&e<25&&norm(v)<7; // 0.6 m: inside the rails' room round the hull; the loop across takes the rest out as they swing.
    }
    // The pins over the rails: across them within 0.6 m, along them 5 m west to 9 m east of the axis (a touch there is the
    // pins on them, not an arm on the hull); in the grooves: within 0.15 m across (the bars' crests 0.2 m out).
    bool onRails(Vec flat)const{const double n=std::abs(dot(flat,north_)),e=dot(flat,cross(north_,up_));return n<.6&&e>-5&&e<9;}
    bool inGrooves(Vec flat,double within=.15)const{return std::abs(dot(flat,north_))<within&&onRails(flat);} // within: of the line (the crests 0.2 m out).
    // In the arms' grip: its pins within 1.2 m of their line over the rails' span, within 4 m over them. There the catch arms
    // are the catch's own business, not something to get away from: they stay shut round it (a bounce on their shock absorbers
    // or a slip does not open them), the collision monitor leaves them out, and any getting away is straight up, never across
    // them. Set each step by grip() (the vehicle's guidance).
    bool gripped(const State& s,Vec flat)const{const Vec p=pinsAt(s,flat);const double e=dot(p,cross(north_,up_));
        return std::abs(dot(p,north_))<1.2&&e>-5&&e<9&&dot(s.position+site_*-1.,up_)-catchHeight(s)<4;}
    void grip(const State& s,Vec flat){grip_=gripped(s,flat);}
    // The arms shut and still for half a second: only then are the pins let down onto their rails (rails still swinging drag a
    // pin set on them off its groove: a ship let go on them so fell through the arms when its engines cut).
    bool settled()const{return armsClosed&&shutAt_>=0&&clock_-shutAt_>=.5;}
    // The catch flown in one sweep, as the real ones (no hover on the way, no stop in front of the arms): down to settleHeight
    // over the arms, falling there at settleSpeed with nothing across, its pins over the axis (the observer's push and an
    // off-axis thrust's lean allowed for, as overArms()), by the burn its planner (PoweredDescent: G-FOLD's convex way) lays out
    // every 2 s from where it is (tracked in between at 0.4 rad/s: replanned faster, it chases its own lean's lag and swings): within its engines' throttle (on `engines`, down to `fewest`), a 16° lean, a glide
    // slope up from the arms (tan 0.5: never low far out) and a speed across of at most 2 m/s and 0.1 m/s a metre of height over
    // the settling (never low and fast), never closing on the tower's face (8 m west of the axis) faster than braking at 1.5 m/s²
    // stops it (what the collision monitor's lag and braking still save: always a way out), its push across, as the thrust follows it
    // (lagging 1.8 over the attitude's bandwidth, as measured: the plan leads it), asked to change at most 3 m/s² a second and costing six times the upward (gentle ways across), never down faster
    // than 2 m/s and 0.35 m/s a metre over the settling (no last plunge for a hard braking to save), never more than 0.5 m and
    // the arms' line by 5 m once its hull's foot comes down near them (its length below its centre of mass, 20 m more over the
    // settling; higher, free: across while high, along the line it may slide in late, as the real catches), and within 30 m of the settling (the hull among the arms: its lean's
    // swing near them) at most 1 m off the line and leaning 8°: lined up while high, only small corrections low; its time to there the
    // least propellant among five laid afresh (the gravity it fights all the while), then the cheapest among the last plan's and its
    // neighbours'; flown with
    // what it now asks plus the plan's state's error (0.16/s², 0.7/s); the air's push left to the replanning from where it is
    // (mostly the crossflow its own lean raises, against it: netted through the observer's lag it fights the lean's reversal and
    // carries it on toward the tower; the plan's lean 16°, the rest of the 20° room for it); then settling onto the arms, the descent easing linearly with
    // the height to touchSpeed at the touch, across by E-guidance onto the axis (its T at least what keeps the loop within half
    // the attitude's bandwidth) with a slow integral of the pins' miss within 2 m (what the attitude's lag leaves). drag: the air's
    // braking now along the velocity (m/s²). The acceleration asked of the thrust (ECEF, m/s²; within the lean and the hull's swing
    // clear of the tower), the time to the touch (s), whether it is settling, and how far the plan misses what it must meet (m,
    // m/s or m/s²: over ~1, no way in).
    struct Sweep {Vec a;double touch=0,miss=0;bool settling=false;};
    double settleHeight=6,settleSpeed=3,touchSpeed=.5,handover=25; // handover: over the settling, where the plan hands the last metres to the bandwidth-bound laws (m).
    // The least time (s) to bring x (m) moving at v (m/s) to rest at nought accelerating at most a (m/s²): bang-bang.
    static double quickest(double x,double v,double a){const double k=x+v*std::abs(v)/(2*a)>=0?1:-1;return k*v/a+2*std::sqrt(std::max(k*x/a+v*v/(2*a*a),0.));}
    Sweep sweep(const State& s,Vec flat,double over,Vec offset={},double drag=0,int engines=3,int fewest=1)const{
        const double g=norm(gravity(s.position)),vz=dot(s.velocity,up_),z=over-catchHeight(s),speed=norm(s.velocity),k=(settleSpeed-touchSpeed)/settleHeight,settling=std::log(settleSpeed/touchSpeed)/k;
        const Vec east=cross(north_,up_),v=s.velocity+up_*-vz;Sweep w;
        const double above=cg(s.propellant)-pin_,lever=std::max(length_-cg(s.propellant),1.);const Vec tilt=s.toBody.r1*offset.y+s.toBody.r2*offset.z;
        const Vec shift=push_*(-above/std::max(g,1.))+(tilt+up_*-dot(tilt,up_))*(above/lever),at=flat+shift; // Where its pins come over the ground, from its centre of mass.
        const double e=dot(at,east),n=dot(at,north_),ve=dot(v,east),vn=dot(v,north_);
        if(z>settleHeight+handover&&!terminal_){
            PoweredDescent::Problem p;p.r0={dot(flat,east),dot(flat,north_),z};p.v0={ve,vn,vz};p.rt={-dot(shift,east),-dot(shift,north_),settleHeight};p.vt={0,0,-settleSpeed};
            p.g={0,0,-g};p.d={0,0,.5*(speed>0?drag*-vz/speed:0.)};const Vec u=tmul(s.toBody,s.thrustBody)*(1/std::max(s.mass,1.));p.uNow={dot(u,east),dot(u,north_),dot(u,up_)};
            p.floor=.4*fewest*raptorSea/s.mass;p.ceiling=.9*engines*raptorSea/s.mass;p.lean=std::tan(16*deg);p.slope=.5;p.radius=3;p.climbRate=.1;p.speedFloor=2;p.wall=-8;p.brake=1.5;p.jerk=3;p.across=6;p.lag=1.8/bandwidth(s,-1,fineAim);p.sinkRate=.5;p.wedge=.1;p.wedgeFloor=.5;p.low=30;p.lowAcross=1;p.lowLean=std::tan(8*deg);p.mid=length_-cg(s.propellant)+20;p.midAcross=5;
            const double since=clock_-plannedAt_;
            // Its last 8 s on the plan it has (laid afresh with seconds to go, the plans jumped: a longer time, the push across the other
            // way, which a quick lean followed into 30° by the arms).
            const bool late=plan_.ok&&since>=0&&since<plan_.T&&plan_.T-since<8;
            if(plan_.u.empty()||since<0||(since>=2&&!late)){ // Its plan laid out again every 2 s (a steady reference, tracked in between): about the last one's time to go (the first: as the braking and the way across take).
                double T0;if(!plan_.u.empty()&&since>=0&&since<5)T0=plan_.T-since;else{const double dz=z-settleHeight,each=.8*std::tan(20*deg)*g/std::sqrt(2.);
                    T0=std::max({2*dz/std::max(settleSpeed-vz,1.),1.2*quickest(e,ve,each),1.2*quickest(n,vn,each)+4.5});}
                // Laid out afresh: the time among these whose plan takes the least propellant (Σ|u| Δt: the gravity it fights all the
                // while, against the harder braking of a short one; by its smoothness alone the longest won, a 30 s burn and more).
                const bool fresh=plan_.u.empty()||since<0||since>=5;std::vector<double> tries;if(fresh)tries={.7*T0,.85*T0,T0,1.2*T0,1.45*T0};else tries={T0,T0+(++turn_%2?1.:-1.)};
                auto spent=[](const PoweredDescent::Plan& q){double sum=0;for(const Vec& u:q.u)sum+=norm(u);return sum*q.T/PoweredDescent::N;};
                PoweredDescent::Plan best;for(double T:tries){T=std::max(T,1.5);PoweredDescent::Plan q=planner_.solve(p,T,plan_.u.empty()?nullptr:&plan_,std::max(since,0.));
                    if(!q.ok&&!plan_.u.empty())q=planner_.solve(p,T); // Afresh when the last plan leads it astray.
                    if(std::getenv("ALBERT_PLAN")&&fresh)std::printf("TRY T %.1f ok %d miss %.2f (%c) cost %.0f dv %.0f\n",q.T,q.ok,q.miss,q.worst?q.worst:'-',q.cost,spent(q));
                    if((q.ok&&(!best.ok||(fresh?spent(q)<spent(best):q.cost<best.cost)))||(!best.ok&&!q.ok&&q.miss<best.miss))best=q;}
                for(double more:{3.,7.,12.,20.})if(!best.ok){const PoweredDescent::Plan q=planner_.solve(p,std::max(T0,1.5)+more);if(q.ok||q.miss<best.miss)best=q;}
                plan_=best;asked_=p;plannedAt_=clock_;
                if(std::getenv("ALBERT_PLAN")){Vec r1,v1,u1;PoweredDescent::at(p,best,1,r1,v1,u1);std::printf("PLAN %.1f T %.1f ok %d miss %.2f cost %.0f r %.1f %.1f %.1f v %.1f %.1f %.1f u0 %.2f %.2f %.2f now %.2f %.2f %.2f v@1 %.1f %.1f %.1f\n",clock_,best.T,best.ok,best.miss,best.cost,
                    p.r0.x,p.r0.y,p.r0.z,p.v0.x,p.v0.y,p.v0.z,best.u.empty()?0.:best.u[0].x,best.u.empty()?0.:best.u[0].y,best.u.empty()?0.:best.u[0].z,p.uNow.x,p.uNow.y,p.uNow.z,v1.x,v1.y,v1.z);}}
            Vec r,pv,pu;PoweredDescent::at(asked_,plan_,clock_-plannedAt_,r,pv,pu);
            const Vec now{dot(flat,east),dot(flat,north_),z},want=pu+(r+now*-1.)*.16+(pv+Vec{ve,vn,vz}*-1.)*.7; // Tracked at 0.4 rad/s: within what its lean's lag holds.
            const Vec a=east*want.x+north_*want.y+up_*want.z;w.touch=std::max(plan_.T-(clock_-plannedAt_),0.)+settling;w.miss=plan_.miss;
            const double lift=std::max(dot(a,up_),.3*g);Vec side=a+up_*-dot(a,up_);const double limit=std::tan(lean(s,flat,side,20*deg))*lift;if(norm(side)>limit)side=side*(limit/norm(side));
            w.a=up_*lift+side;return w;}
        // Its last metres (from `handover` over the settling: the hull among the arms): by laws whose gains stay within half the
        // attitude's bandwidth (a plan laid afresh with seconds to go asks more than a lean lagging ~1.5 s can give: it swings
        // across): braked at a constant rate to the settling, then settling; across onto the axis by E-guidance, T at least
        // √6·2/bandwidth and 8 s, and a slow integral of the pins' miss.
        plan_={};terminal_=true;double up;
        if(z>settleHeight+.3){const double dz=z-settleHeight,ref=-std::sqrt(settleSpeed*settleSpeed+2*dz);double T; // At least 1 m/s² of braking's way down: it never dawdles.
            if(vz<ref){up=(vz*vz-settleSpeed*settleSpeed)/(2*dz);T=2*dz/(settleSpeed-vz);}else{up=.5*(ref-vz);T=-ref-settleSpeed;}
            w.touch=T+settling;}
        else{ // Settling: slower the further its pins are off the arms' line (down at 15% of it at 0.5 m off); its last metre only once
            // they are over the grooves and the arms shut round it (begun late, they take their 3.75 s), else held 0.8 m over them.
            const double np=dot(pinsAt(s,flat),north_),off=std::abs(np),f=std::clamp(1.2-off/.5,.15,1.),ref=z<2.5&&(!settled()||off>.18||std::abs(np+vn)>.15)?std::clamp(1.2*(1-z),-1.,.5):-(touchSpeed+k*std::max(z,0.))*f; // Its last metres only over the grooves (±0.18 m, ±0.15 m a second on: within their crests a pin slides into the groove) in arms shut and still: else held a metre over them (pins over the rails: the arms never swung shut over them) while the loop across brings it there.
            up=-k*vz*f+(ref-vz);w.settling=true;w.touch=std::log((touchSpeed+k*std::max(z,0.))/touchSpeed)/k; // (Not slowed by its offset: the arms' cue, which waited on it.)
            if(std::getenv("ALBERT_SETTLE")&&std::fmod(clock_+1e-9,.5)<dt_){const Vec p=pinsAt(s,flat);std::printf("SETTLE %.1f z %.2f vz %.2f pins n %.3f e %.2f cg n %.3f e %.2f vn %.3f ve %.3f off %.3f settled %d closed %d close %d contact %.0f lean %.2f°\n",clock_,z,vz,dot(p,north_),dot(p,east),n,e,vn,ve,off,settled(),armsClosed,close_,norm(s.contact),std::acos(std::clamp(dot(bodyX(s),up_),-1.,1.))/deg);}}
        const double bw=bandwidth(s,-1,fineAim),least=std::max(std::sqrt(6.)*hoverSplit/bw,8.),te=std::max(w.touch,least),tn=std::max(w.touch-4.5,least); // 8 s at 1 rad/s: the hover's own loop (0.3 rad/s), what a lean lagging ~1.8 s holds steady (quicker on the fine law, 4 s, swung the ship's lean into a limit cycle).
        const double kv=std::min(.5,bandwidth(s,-1,fineAim)/hoverSplit),ki=.375*kv*kv/10;if(clock_-settleAt_>.1)settle_={};settleAt_=clock_;
        if(const Vec miss=east*e+north_*n;norm(miss)<2){settle_=settle_+miss*dt_;const double most=.5/ki;if(norm(settle_)>most)settle_=settle_*(most/norm(settle_));}
        Vec side=east*(-6*e/(te*te)-4*ve/te)+north_*(-6*n/(tn*tn)-4*vn/tn)+push_*-1.+settle_*-ki;
        const double lift=std::max(g+up-(speed>0?drag*-vz/speed:0.),.3*g),limit=std::tan(lean(s,flat,side,20*deg))*lift;if(norm(side)>limit)side=side*(limit/norm(side));
        w.a=up_*lift+side;return w;
    }
    // The arms shut round it as it settles: once it will touch within their closing time (and 2 s more: shut before it gets there), its hull
    // at the rails' height (its pins') centred between them by the time they get there (half its drift across over that time: the rails
    // reach within a metre of the hull only in their swing's last degree, its last fifth of a second) and over their span along; where
    // it is now only as far as that reckoning holds (0.9 m, 0.5 m/s across: waiting for it to be centred already cost the ship ten
    // seconds and more on its engines over the arms).
    bool shutting(const State& s,Vec flat,double touch)const{
        const Vec p=pinsAt(s,flat),v=s.velocity+up_*-dot(s.velocity,up_);const double n=dot(p,north_),e=dot(p,cross(north_,up_)),vn=dot(v,north_);
        return touch<=closing+2&&std::abs(n)<.9&&std::abs(n+vn*closing/2)<.4&&std::abs(vn)<.5&&e>-4&&e<8;
    }
    // Where its pins are over the ground (flat: its centre of mass's), the body's lean carrying them off it (they are cg - pin
    // over it along the body: 1° of lean, half a metre): what must be over the rails.
    Vec pinsAt(const State& s,Vec flat)const{const Vec x=bodyX(s),h=x+up_*-dot(x,up_);return flat+h*(cg(s.propellant)-pin_);}
    // Hanging on the pins with the engine on: the body upright and the thrust's line through the pins (the pivot), so nothing is
    // left to swing when it cuts. still(): so, upright within 0.2° and turning under 0.004 rad/s (its rate alone is nought at
    // each end of a swing).
    bool still(const State& s)const{return std::acos(std::clamp(dot(bodyX(s),up_),-1.,1.))<.2*deg&&std::hypot(s.rate.y,s.rate.z)<.004;}
    void hang(Command& c,const State& s,const Engines* by=nullptr)const{
        const Vec e=mul(s.toBody,cross(bodyX(s),up_)),w=s.rate,o=by?by->centre():Vec{};const double lever=length_-pin_,gain=pitchInertia(s.propellant)/std::max(s.thrust*lever,1.);
        c.gimbalPitch=std::clamp(gain*(1.44*e.y-2*w.y)-o.z/lever,-15*deg,15*deg);c.gimbalYaw=std::clamp(gain*(1.44*e.z-2*w.z)+o.y/lever,-15*deg,15*deg);c.torque={};roll(c,s,by);
    }
    bool grip_=false; // In the arms' grip (gripped()).
public:
    // How its escape takes hold, as measured (s): the ship's from 20 m out at 5.9 m/s toward the tower, pushed in at 3.7 m/s², went
    // 17.7 m (0.7, 0.6 at all of the braking: 18 m); the booster's from 107 m out at 41 m/s, 136 m (0.5, 2.5: 143 m; its lean takes
    // ~3 s to reach 25°). The monitor credits 90% of the braking, as the lean allows it where it is once that braking takes hold
    // (2 s on regardless, by the tower's face, gave the ship 2 m/s² and aborted catches it then made untouched).
    double escapeReact=.5,escapeSwing=2.5;
    bool laggedThrust=false; // The collision monitor keeps the thrust's push across now up over its lag (the ship's planned way in: it leads its lean).
    bool fineAim=false; // Its precision phases (the planned burn, the settling, over the arms) on aim()'s fine law and bandwidth (the ship).
protected:
    double shutAt_=-1;mutable Vec gotInt_;mutable double gotAt_=-1; // overArms(): the shortfall of the acceleration across it gets, integrated.
    mutable bool terminal_=false; // Its last metres flown by the bandwidth-bound laws (sweep()).
    mutable Vec hitAt_;mutable double hitT_=0,hitRise_=0; // Where the last collision course was found to run into something, how soon, at what rise (m, s, m).
    PoweredDescent planner_;mutable PoweredDescent::Plan plan_;mutable PoweredDescent::Problem asked_;mutable double plannedAt_=-1;mutable int turn_=0;
    Vec site_,north_,up_,push_;mutable Vec settle_,unmet_;mutable double settleAt_=-1;double clock_=0,dt_=.02,seenV_=0,arms_,rest_,length_,pin_,propellant_,gp_=0,gy_=0;MassProperties full_,empty_;bool close_=false,in_=false;
};
// The booster's way back from staging, at 50 Hz, deciding all the way whether the catch can still be made:
// - a flip on 3 engines, the boostback on all 33 (Flights 13 and 14), 13 for its last 25 km and 3 for the last few, until the predicted landing is just off the coast east of
//   the tower, the coast engines first (the RCS above the air, held at the attitude it enters it at; the grid fins steering
//   the landing in it);
// - the landing burn on 13 engines, planned at 70% of their thrust (the margin rides out an engine lost), to the gate, then on
//   5 and 3 (Flight 14's down-select) across onto the tower's axis (2 or 1 once 3 at their least lift too much, more while it falls short), the arms closing
//   round it, and down onto them at 1 m/s; the roll that puts the pins over the arms by the engines' differential gimbal;
// - once the catch cannot be made (the engines, the propellant, the reach, a fall it cannot stop above the arms, too long over
//   them) it is given up for good: down at sea, east of the tower, or when that is out of reach as far from the tower as it
//   gets, braking all the way.
class BoosterReturn:public TowerApproach {
public:
    enum class Phase {wait,flip,boostback,coast,landing,final,hover,descend,caught,divert,down};
    BoosterReturn(Vec site,double latitude,double longitude,double arms,double rest)
        :TowerApproach(site,latitude,longitude,arms,rest,boosterLength,pinAt,combine(booster(true)),combine(booster(false)),boosterPropellant){hoverAbove=4;holdAlong=true;}
    Phase phase()const{return phase_;}
    double predicted()const{return miss_;}  // The latest predicted landing's distance from where it is aimed (m).
    const char* gaveUp()const{return why_;} // Why the catch was given up (null while it is on).
    const char* escaping()const{return escape_?threat_:nullptr;} // Getting away with all it has: from what (null otherwise).
    const char* termination()const{return termination_.empty()?nullptr:termination_.c_str();}
    Command step(double t,const State& s){
        if(termination()){Command c;c.terminate=true;return c;} // Latched: a rejected command cannot disarm AFTS.
        if(s.windAge<5)wind_=s.wind; // The tower's wind as its link last sent it (kept when the link drops).
        observe(s,t-seen_,(phase_>=Phase::final&&phase_<=Phase::descend)||phase_==Phase::divert);seen_=t;Command c;c.surfaces={0.,0.,0.};set_.keep(c,s);const Vec rel=s.position+site_*-1; // keep: a lit engine that failed, the next working one in its place.
        grip(s,rel+up_*-dot(rel,up_));
        const double over=dot(rel,up_),vz=dot(s.velocity,up_);const Vec flat=rel+up_*-over,across=s.velocity+up_*-vz;route(flat);
        if(catch_&&phase_>=Phase::flip&&phase_<=Phase::descend){const int n=phase_<=Phase::boostback?3:phase_<=Phase::landing?5:static_cast<int>(std::ceil(s.mass*norm(gravity(s.position))/(.9*raptorSea)));
            if(set_.working(s,phase_<=Phase::landing?13:3)<n||(phase_>=Phase::coast&&set_.balanced(s)<2))giveUp(s,balanceWhy);}
        // The arms leave 0.8 m: going for them needs the navigation to a third of that (RTK's centimetres; drifting without it).
        // Its pins under the rails with nothing bearing them (a lean or a sway put them past the bars): never on down between the
        // arms; given up, away while it can (as the ship's).
        if(catch_&&(phase_==Phase::hover||phase_==Phase::descend||phase_==Phase::caught)&&norm(s.contact)==0&&over-catchHeight(s)<-1.5)giveUp(s,"pinos abaixo dos trilhos");
        if(catch_&&phase_>=Phase::final&&phase_<=Phase::descend&&s.navSigma>.3){if(++unsure_>25)giveUp(s,"navegação imprecisa para a captura");}else unsure_=0;
        // On the way in: short of finishing from here (its Δv: the speed it has, and gravity's over the braked way down, 2 h / |vz|,
        // and the 17 s over the arms to the catch; 10% kept), away while it can, else on; at the bingo (what is left down to
        // getting clear, escapeNeed(), and 3 s more), away now (it is not over the arms yet).
        if(catch_&&phase_==Phase::final&&!hurry_){const double g=norm(gravity(s.position)),left=std::max(over-catchHeight(s),0.),usable=s.propellant-residual;
            const double finish=1.1*s.mass*(norm(s.velocity)+g*(2*left/std::max(-vz,1.)+17))/raptorExhaust,clear=escapeNeed(s,flat),soon=3*s.mass*g/raptorExhaust;
            if(usable<finish){if(escapes(s,flat))giveUp(s,"propelante insuficiente para terminar a captura");else hurry_=true;}
            else if(usable<clear+soon)giveUp(s,"propelante no limite: captura abandonada enquanto a fuga é possível");}
        // Never into the tower, never down onto the ground's works: a course the hull cannot be braked off a column on (the
        // escape's braking at 70%, after a second's lag), or a fall over the works its engines cannot stop with what is left:
        // the catch given up, and away with all it has (escape_).
        if((phase_>=Phase::final&&phase_<=Phase::descend)||phase_==Phase::divert||(phase_==Phase::landing&&over<400)){
            const double brake=stopping(s,flat,std::max(set_.count(),1),rush(s),escapeReact+escapeSwing); // What leaning against its way gives (any way, still), where it is once that braking takes hold.
            const char* why=into(collides(s,flat,.9*brake,catch_?top(s)+(phase_==Phase::final?safe(s,flat):0):phase_==Phase::divert?std::min(held_,over):stand(s)));
            if(!why&&!stops(s,stand(s),set_.working(s,33),true)&&overWorks(flat,length_))why="queda sobre a infraestrutura de solo";
            if(why&&!escape_&&!(phase_==Phase::descend&&norm(s.contact)>0)){if(catch_)giveUp(s,why);if(phase_==Phase::divert){escape_=true;threat_=why;}}}
        switch(phase_){
        case Phase::wait:if(t-start_>.5){set_.use(c,s,3);c.throttle=.4;phase_=Phase::flip;}hold(c,s,unit(s.velocity));break; // Clear of the ship (half a second), then 3 engines: turning at once, as the real ones after hot staging.
        case Phase::flip:{ // Turn to point the thrust from the predicted landing back to the tower, boostbackPitch from the horizon.
            if(age_--<=0){aimed_=landing(s);age_=5;}const Vec d=boostbackDirection(aimed_);point(c,s,d);finsToward(c,s,d,true);if(dot(bodyX(s),d)>std::cos(8*deg)){ // The grid fins thrown over the way it turns (taking what the gimbals cannot).
                set_.use(c,s,33);c.throttle=boostbackThrust(s.mass,set_.count());phase_=Phase::boostback;back_=d;}break;}
        case Phase::boostback:{ // The prediction every 0.1 s, every step within 10 km of the aim.
            if(age_--<=0||dot(aimed_,back_)>-1e4){aimed_=landing(s);age_=5;}const Vec e=aimed_;point(c,s,boostbackDirection(e));miss_=norm(e);
            if(set_.wanted()==33){if(dot(e,back_)>-25e3)set_.use(c,s,13);else c.throttle=boostbackThrust(s.mass,set_.count());} // The high-thrust part on all 33 (Flights 13, 14),
            if(set_.wanted()==13&&dot(e,back_)>-5e3)set_.use(c,s,3); // the 13 that steer for its last 25 km, 3 for the last few, to cut finely.
            if(dot(e,back_)>=0||s.propellant<120e3){set_.use(c,s,0);phase_=Phase::coast;if(dot(e,back_)<-2e3)giveUp(s,"boostback incompleto: o pouso cai antes da costa");}break;}
        case Phase::coast:{ // Engines first through the descent: held by the RCS above the air, then the grid fins tilt the body so its
            // crossflow lift (away from the nose's tilt) carries the impact onto the aim; the landing burn when the stop needs it.
            if(catch_&&over<divert)aim_=east_*front; // Low enough: the fins carry it from off the coast onto the approach.
            if(age_--<=0){aimed_=landing(s,&seconds_,catch_?leadBrake:0.);age_=25;miss_=norm(aimed_);if(over<15e3&&reach(s)>3e3)giveUp(s,"pouso previsto fora do alcance da torre");}
            const Vec e=aimed_;const double seconds=seconds_;const Vec vhat=unit(s.velocity);double burn=0;
            const double margin=over<40e3?gateHeight(s,set_.working(s,13)*plan*raptorSea/s.mass,&burn)-(top(s)+gate(s.mass,set_.working(s,3))+50):1e9; // Lit at 0.
            if(s.q<2e3){if(!(norm(slewed_)>.5))slewed_=bodyX(s); // Engines first slowly (1.5°/s, the RCS's gentle turn after the boostback), faster only as the
                // air nears (what is left of the turn by 45 km, 30% to spare, up to 6°/s), held right at the entry attitude within 20 s of
                // the air; the fins take it over from where it then is.
                const Vec to=entry(s);const double left=std::acos(std::clamp(dot(slewed_,to),-1.,1.)),soon=std::max((norm(s.position)-norm(site_)-45e3)/std::max(-vz,1.),1.);
                slewed_=soon>20?turned(slewed_,to,std::clamp(1.3*left/soon,1.5*deg,6*deg)*dt_):to;hold(c,s,slewed_);} // Within 20 s of the air: held right at it.
            else{Vec want=e*(-6/std::max(seconds*seconds,1.));want=want+vhat*-dot(want,vhat);
                // The push across a radian of tilt: the model's (normalForce q S), then as the IMU measures it (the force across the
                // flow over the tilt held away from it); the model's to start from.
                const double a=norm(want),model=normalForce*s.q*pi*radius*radius/s.mass;if(!(lift_>0))lift_=model;
                {const Vec side=unit(want),f=tmul(s.toBody,s.specific);const double held=-dot(bodyX(s),side);
                    if(a>0&&std::abs(held)>std::sin(.3*deg))lift_+=.05*(std::clamp(dot(f,side)/held,.2*model,3*model)-lift_);else lift_+=.05*(model-lift_);}
                // Across the arms' line first (the final slides along it but only creeps across it: its lean answers ~2 s late), along
                // with what is left; upright by the burn (the tilt that lifts it toward the line would push it the other way once
                // 13 engines thrust along it), from 5 s before it to 1 s, at the 3°/s it turns.
                const double cap=std::sin(12*deg)*std::clamp((margin/std::max(-vz,1.)-1)/4,0.,1.),l=std::max(lift_,1e-3);const Vec side=unit(north_+vhat*-dot(north_,vhat)),rest=want+side*-dot(want,side);
                const double tn=std::clamp(dot(want,side)/l,-cap,cap);const Vec tw=side*tn+(norm(rest)>0?unit(rest)*std::min(norm(rest)/l,std::sqrt(cap*cap-tn*tn)):Vec{});const double tilt=norm(tw);
                const Vec dd=tilt>0?unit(vhat*-std::sqrt(1-tilt*tilt)+tw*-1.):vhat*-1;if(!(norm(pointed_)>.5))pointed_=bodyX(s);pointed_=turned(pointed_,dd,3*deg*dt_);steer(c,s,pointed_);if(std::getenv("ALBERT_COAST")&&std::fmod(clock_+1e-9,1.)<dt_)std::printf("COAST %.1f h %.0f t %.1f miss e %.1f n %.1f at n %.1f vn %.2f want e %.3f n %.3f tilt %.3f lift %.2f model %.2f\n",clock_,over,seconds,dot(e,east_),dot(e,north_),dot(flat,north_),dot(across,north_),dot(want,east_),dot(want,north_),tilt,lift_,model);} // Turning at most 3°/s (from where it is, taken up in the air).
            if(margin<=0){
                if(catch_){if(set_.working(s,13)<5)giveUp(s,balanceWhy);
                    else if(s.propellant-residual<need(s,burn))giveUp(s,"propelante insuficiente para a captura");else if(reach(s)>400)giveUp(s,"pouso previsto fora do alcance da torre");}
                if(phase_==Phase::divert)break;
                set_.use(c,s,13);c.throttle=plan;phase_=Phase::landing; // Its estimates from the plan: the burn's thrust, its push a radian, solved at once.
                accel_=plan*set_.count()*raptorSea/s.mass;slope_=accel_-normalForce*s.q*pi*radius*radius/s.mass;age_=0;}
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
                if(phase_==Phase::landing){set_.use(c,s,5);phase_=Phase::final;if(std::getenv("ALBERT_COAST"))std::printf("FINAL %.1f h %.0f e %.1f n %.1f ve %.1f vn %.2f vz %.1f\n",clock_,over,dot(flat,east_),dot(flat,north_),dot(across,east_),dot(across,north_),vz);}else set_.use(c,s,3);} // Down-selected to 5 for the fine-tuning (Flight 14's 13, 5, 3).
            const double most=std::max(set_.count(),1)*raptorSea/s.mass;
            if(age_--<=0&&!(above<=floor&&fast)){age_=10;const double aim=top(s)+floor,a1=std::clamp(accel_>0?accel_:plan*most,.4*most,most),a2=a1*.9;
                auto run=[&](double a,Vec& end,Vec& u,double& secs){Vec r=s.position,v=s.velocity;int n=0;
                    // The drag a little short (braking early), its last 3 s upright as flown.
                    fly(r,v,s.mass,a,.1,[&]{++n;return -dot(v,up_)<=gateSpeed||dot(r+site_*-1,up_)<aim;},.85,3*std::max(a-g0,1.));end=r;u=v;secs=.1*n;return dot(r+site_*-1,up_);};
                Vec e1,e2,u1,u2;double t1=0,t2=0;const double h1=run(a1,e1,u1,t1),h2=run(a2,e2,u2,t2),f=h1>h2+1e-3?(aim-h2)/(h1-h2):1,g=std::clamp(f,0.,2.);
                accel_=std::clamp(a2+(a1-a2)*f,.4*most,most);const Vec end=e2+(e1+e2*-1.)*g,rel=end+site_*-1,u=u2+(u1+u2*-1.)*g;
                gated_=rel+up_*-dot(rel,up_)+(catch_?east_*front:aim_)*-1.;gatedV_=u+up_*-dot(u,up_);tgo_=std::max(t2+(t1-t2)*g,2.);}
            // Leaning the thrust off retrograde by α pushes across by about (T - N) α, N the body's normal force a radian (normalForce
            // q S): thrust-steered (toward the push) once T > N, body-steered (away from it, within the tables' first 10°) while the
            // air is thick. The push a radian as measured (the IMU's force across the flow over the angle of attack held), once
            // there is one to measure, in place of that estimate.
            // Across, E-guidance on the gate's miss (a gate it cannot brake from, toward the tower, the collision monitor takes:
            // away, its catch given up).
            const Vec back=unit(s.velocity*-1.);Vec want=gated_*(-6/(tgo_*tgo_));want=want+back*-dot(want,back);
            const double model=accel_-normalForce*s.q*pi*radius*radius/s.mass;
            {const Vec side=unit(want),f=tmul(s.toBody,s.specific);const double held=dot(bodyX(s),side);
                if(norm(want)>0&&std::abs(held)>std::sin(1.5*deg))slope_+=.05*(std::clamp(dot(f,side)/held,-100.,100.)-slope_);else slope_+=.05*(model-slope_);}
            // Upright again by the gate (3 engines turn the body back slowly): the lean's limit down to a quarter in its last seconds.
            const double room=std::clamp((tgo_-1.5)/3,.25,1.),push=slope_*s.mass,lean=std::min(norm(want)*s.mass/std::max(std::abs(push),1e5),(push>0?20*deg:10*deg)*room)*(push>0?1:-1);
            Vec d=norm(want)>0?back*std::cos(lean)+unit(want)*std::sin(lean):back;
            // Braking what it will be moving across 1.5 s on (the push its lean gives now carried that long: the body turns back
            // slowly, so braking what it moves now overshoots into a push the other way, a back out).
            if(tgo_<3){const Vec f=tmul(s.toBody,s.specific),lead=across+(f+up_*-dot(f,up_))*1.5;Vec side=lead*-.4;const double v=accel_*std::max(dot(back,up_),.3),limit=std::tan(8*deg)*std::clamp((tgo_-1)/2,0.,1.)*v;
                if(norm(side)>limit)side=side*(limit/norm(side));d=unit(up_*v+side);}
            thrust(c,s,d*accel_);if(s.q>2e3)finsToward(c,s,d,true); // Its last 3 s: upright, the speed across braked, for 3 engines to take over.
            break;}
        case Phase::final:{ // On 3 to the hover point, still falling at 1 m/s there, not below a 27° slope down to it from the
            // approach (the hull clear of the arms off their line); within 3 m of the axis, on down to 1.5 m over the arms at 0.5
            // m/s (no hover: the arms are closing round it as it comes, and it goes straight on down onto them, aligned in the shut
            // arms; else held there); across by E-guidance, the braking law in its last seconds; at sea, down to the water.
            const Vec to=goal(flat);const bool over_=catch_&&norm(flat)<3;const double above=over-(over_?catchHeight(s)+1.5:top(s))-(catch_?safe(s,flat):0);
            if(catch_&&age_--<=0){age_=10;if(!stops(s,catchHeight(s)))giveUp(s,"a queda não para acima dos braços");}
            if(phase_!=Phase::final)break;
            const double up=vertical(s,above,vz,over_?-.5:-1),limit=std::tan(20*deg)*up; // Its lean below: within the hull's swing clear of the tower.
            // Across onto the goal at the speed it can still brake to rest there (braking.(): a third of what leaning against its way
            // gives, within the hull's swing clear of the tower, its velocity loop's lag allowed for), at most 8 m/s and kv/2 of the
            // distance a second, that speed's error at kv, a third of the attitude's bandwidth. Well below the height that clears
            // what is around it: held where it is while it climbs.
            // On the arms' line or in their V, going for the axis: along the line the velocity loop onto it (onto(): closing no
            // faster than it can still brake there, its attitude's lag allowed for, at most 8 m/s); across it, onto the line with
            // no speed across on arrival (-6 n/T² - 4 v/T, T the time its fall takes to the hover point at the braking it flies),
            // solved again every cycle: it slides in from the front of the V as the real catches do, centred between the rails.
            const double kv=std::min(.6,bandwidth(s)/3);Vec side;
            const bool line=catch_&&(std::abs(dot(flat,north_))<=4||in_);const double T=std::max(2*std::max(above,0.)/std::max(1-vz,1.),std::sqrt(6.)*3/bandwidth(s)); // At least what keeps it within a third of its attitude's bandwidth.
            if(above<-5)side=across*-std::max(kv,.4);
            else if(line){const double n=dot(to,north_),vn=dot(across,north_);side=onto(s,to+north_*-n,kv,30,20*deg,finalBrake,finalMouth,finalGain);side=side+north_*(-dot(side,north_)-6*n/(T*T)-4*vn/T);}
            else side=onto(s,to,kv,30,20*deg);
            side=side+push_*-1.;{const double most=limit*std::tan(lean(s,flat,side,20*deg))/std::tan(20*deg);if(norm(side)>most)side=side*(most/norm(side));} // The observer's push netted.
            {const Vec a=up_*up+side;if(set_.wanted()==5&&s.mass*norm(a)<.42*5*raptorSea)set_.use(c,s,3); // Then to 3 once 5 at their least would lift too much.
                count(c,s,thrust(c,s,a),std::min(set_.count(),2));} // 2 once 3 at their least would lift it (a light booster: Raptor 3's 40% of 280 t each).
            close_=catch_&&centred(s,flat,false,.3); // On the 3 it took over with: none put out on the way in. Still coming in, within 0.3 m of the line (in a crosswind the loop across did not take 0.6 m out as they swung: an arm shut against the hull, the pins never over their grooves).
            if(over_&&over-catchHeight(s)<3){if(armsClosed&&aligned(s,flat))phase_=Phase::descend;else if(above<.3)phase_=Phase::hover;} // On down if aligned in the shut arms; else held there.
            else if(!catch_&&(norm(s.contact)>0||above<.3)){set_.use(c,s,0);phase_=Phase::down;}
            break;}
        case Phase::hover:case Phase::descend: // Over the axis; down at 1 m/s once the arms are round it and it is aligned with them.
            close_=centred(s,flat,phase_==Phase::descend);
            if(phase_==Phase::hover&&settled()&&aligned(s,flat))phase_=Phase::descend;
            else if(const Vec p=pinsAt(s,flat);phase_==Phase::descend&&(norm(s.contact)==0?std::abs(dot(p,north_))>(hurry_?.3:.25)||std::abs(dot(p,east_))>5||std::abs(rollError(s))>6*deg:!inGrooves(p,hurry_?.2:.15)))phase_=Phase::hover; // Off it, or the pins on the bars' crests (they would slide off): held again, up off them.
            // Short of finishing (down at 1 m/s, 3 s more): away while it can, else onto the arms anyway, faster. Bingo (what is left
            // down to getting clear, escapeNeed(), and 3 s more: past it there is no getting away): it decides for good, onto the arms
            // now when it is about to (aligned, or going down onto them) and can finish, else away while it still can.
            {const double usable=s.propellant-residual,second=g0*s.mass/raptorExhaust,finish=(std::max(over-catchHeight(s),0.)+3)*second,clear=escapeNeed(s,flat);
                if(!hurry_&&usable<finish){if(escapes(s,flat))giveUp(s,"propelante insuficiente para terminar a captura");else hurry_=true;}
                else if(!hurry_&&usable<clear+3*second){if(phase_==Phase::descend||aligned(s,flat))hurry_=true;else giveUp(s,"propelante no limite: captura abandonada enquanto a fuga é possível");}}
            if(phase_==Phase::divert)break;
            if(const Vec p=pinsAt(s,flat);hurry_){if(onRails(p))close_=true;if(settled()&&inGrooves(p,.2))phase_=Phase::descend;} // Its pins over their grooves (inside the crests): round it now, and down.
            if(phase_==Phase::descend&&norm(s.contact)>0&&settled()&&inGrooves(pinsAt(s,flat),hurry_?.2:.15)){ // The pins in the arms' grooves (hurrying: on the rails), the arms at rest: the weight handed to them (the thrust at its
                // least, engines out until that is under 70% of it), held upright; caught after 0.3 s.
                hang(c,s,&set_);c.throttle=.4;if(set_.count()>1&&set_.count()*.4*raptorSea>.7*s.mass*g0)set_.use(c,s,set_.count()-1);
                if(touch_<0)touch_=t;if(t-touch_>=.3){phase_=Phase::caught;caught_=t;}break;}
            touch_=-1;count(c,s,overArms(c,s,flat,over,phase_==Phase::descend,std::max(set_.count(),1),set_.centre(),&set_,hurry_?2:1),std::min(set_.count(),2)); // None put out over the arms (a cutoff kicks it across: its thrust's centre jumps; the throttle does it) but the third, once 3 at their least would lift it.
            break;
        case Phase::caught: // On the arms, the engines at their least while they take the load, the thrust's line through the pins
            // until it hangs still (a second on, or 8 s at most: off within seconds, its swing about the pins damped first), then off; slipping off
            // in the first second, or its pins off the grooves while it can still lift (onto the bars' crests, where they would
            // slide off): back off them and onto them again.
            if(norm(s.contact)>0)held_at_=t; // Last seen on them (a bounce on the shock absorbers is not a slip: half a second off them is).
            if(!off_){if((norm(s.contact)==0&&t-held_at_>.5)||!inGrooves(pinsAt(s,flat),hurry_?.2:.15)){phase_=Phase::descend;touch_=-1;break;}hang(c,s,&set_);c.throttle=.4; // Off them while lit, whenever: down onto them again.
                if(set_.count()>1&&set_.count()*.4*raptorSea>.85*s.mass*g0)set_.use(c,s,set_.count()-1); // Their least would lift it off the arms.
                if(t-caught_>1&&t-held_at_<.1&&(still(s)||t-caught_>8)){set_.use(c,s,0);off_=true;}} // Out only while they bear it.
            else if(norm(s.contact)==0&&t-held_at_>.3){off_=false;set_.use(c,s,3);c.throttle=1;phase_=Phase::hover;touch_=-1;} // Slipping off them with its engines out: lit again at once, back up over them.
            else{c.torqueOn=true;c.torque={};}break;
        case Phase::divert:{ // Away from the tower toward the aim as fast as its lean allows (20°, less where its hull's swing would
            // bring it near the tower), its height held until clear of the ground's works by its length (however little it has left: dry,
            // it falls on along its way), then down still heading there. Escaping (a collision course, a fall over the works): with all it has
            // (escape()) until the columns are behind it.
            if(escape_){const double most=rush(s);const Vec dir=away(s,flat,flat+aim_*-1.),a=escape(s,flat,dir,most);const int all=std::max(set_.working(s,33),1);
                // The engines that thrust takes at 90% (all that work when even they fall short of holding it up), more at once
                // (each takes a second to come up), one fewer once the rest carry it at 80%.
                const int n=most<norm(gravity(s.position))?all:std::clamp(static_cast<int>(std::ceil(s.mass*norm(escapeFull(s,flat,dir,most))/(.9*raptorSea))),1,all); // For the thrust it is about to ask.
                if(n>set_.count()){for(int k=n;k<=all;++k){set_.use(c,s,k);if(set_.count()>=n)break;}}else if(set_.count()>1&&s.mass*norm(a)<.8*(set_.count()-1)*raptorSea)set_.use(c,s,set_.count()-1);
                thrust(c,s,a);
                if(collides(s,flat,1.,over)<0&&gap(s,flat,dir,0)>10&&dot(across,dir)>=0&&stops(s,stand(s),set_.working(s,33),true)){escape_=false;held_=std::max(held_,over);} // Clear: on as a divert, the height it got to held.
                if(over-stand(s)<.5){set_.use(c,s,0);phase_=Phase::down;}
                break;}
            const double h=std::max(over-stand(s),0.),brake=std::max(set_.working(s,13)*raptorSea/s.mass-g0,1.);
            const Vec to=flat+aim_*-1.;const double d=norm(to);const bool clear=!overWorks(flat,length_);
            // Down: falling at up to 30 m/s until the constant braking that stops it at the ground (v² / 2h) reaches 35% of what its
            // engines can, then that (a hoverslam, as the landing burn's).
            const double braking=.35*std::min(brake,3*raptorSea/s.mass-g0),stop=vz<-1?(vz*vz-1)/(2*std::max(h,.5)):0.,g=norm(gravity(s.position));
            const double up=clear?(stop>=braking?g+stop:std::max(g+1.5*(-30-vz),0.)):g+.5*(-vz)+.2*(held_-over);
            // Across toward the aim, stopped over the last 120 m of height (it goes down still: a soft splashdown).
            const Vec want=d>0&&!(clear&&h<120)?to*(-std::min(15.,std::sqrt(2*2.5*d))*(clear?std::clamp((h-120)/150,0.,1.):1)/d):Vec{};Vec side=(want+across*-1)*.5+push_*-1.; // The observer's push netted.
            // Its braking engines lit ahead of need (about a second before the hoverslam; one alone falling to it: three at their
            // least would hold it up).
            const int least=clear&&h<150&&(stop>.25*braking||vz<-12)?std::min(3,set_.working(s,13)):set_.balanced(s)?1:2;if(set_.count()<least)set_.use(c,s,least);
            // At most 20°, less where the hull's swing (its foot, 40 m under its centre of mass, the other way) would near the tower;
            // the lit engines give at least 40% (more than asked: the lean that gives the side asked on it).
            const double floor=.4*set_.count()*raptorSea/s.mass,lift=std::max(up,.3*g0),limit=std::tan(lean(s,flat,side,20*deg))*std::max(lift,floor*std::cos(20*deg));
            if(norm(side)>limit)side=side*(limit/norm(side));
            count(c,s,thrust(c,s,up_*(lift*lift+dot(side,side)<floor*floor?std::sqrt(floor*floor-dot(side,side)):lift)+side),least); // Upright even falling: never the thrust laid across.
            if(over-stand(s)<.5){set_.use(c,s,0);phase_=Phase::down;} // On the ground (touching the arms or the tower: still getting away).
            break;}
        case Phase::down:c.torqueOn=true;c.torque={};if(set_.count())set_.use(c,s,0);break; // Down: engines off.
        }
        if(s.propellant<=residual)c.ignite.clear(); // Dry: nothing left to light.
        if(!catch_&&phase_!=Phase::down&&norm(s.contact)==0&&over>stand(s)+5)flightSafety(c,s); // Check immediately, including above 5 km; never wait for a low gate.
        return c;
    }
    void start(double t){start_=t;}
    void abort(const State& s){giveUp(s,"aborto pedido pelo operador");} // The operator's ABORT uplink: the catch given up, away to sea (from low, away from the tower at once).
    // What its way back takes (kg) from a staging now (s: the stack's state; propellant: what the booster would have; engines:
    // its working gimballed ones): the flip (~10 s on 3 at their least, coasted), the boostback on those at boostbackThrottle
    // toward where its fall comes down short of the aim (pitched boostbackPitch) until it comes down there (each 0.5 s, its fall
    // predicted as the coast flies it), then landingShare for the landing (its burn, the final approach, a margin: Flights 7 and 8 lit it with ~75 t). A cutoff short of it leaves the catch out of reach:
    // the stack's main engines cut once what is left comes down to it (or at the planned speed, before).
    double returnNeed(const State& s,double propellant,int engines)const{
        const Vec w{0,0,7.292115e-5};Vec r=s.position,v=s.velocity;double m=boosterEmpty+propellant,used=3*.4*raptorSea/raptorExhaust*10;
        auto step=[&](Vec a,double h){const Vec acc=gravity(r)+cross(w,v)*-2.+cross(w,cross(w,r))*-1.+a;v=v+acc*h;r=r+v*h;};
        for(int i=0;i<50;++i)step({},.2); // The flip, coasted.
        int all=0;for(std::size_t i=0;i<33;++i)all+=!(i<s.failed.size()&&s.failed[i]); // On all that work while 25 km short, then on the 13 that steer.
        Vec first;bool begun=false;
        for(int i=0;i<300&&used<propellant;++i){State x=s;x.position=r;x.velocity=v;x.mass=m-used;x.propellant=propellant-used;
            const Vec miss=landing(x);if(!begun){first=miss;begun=true;}if(dot(miss,first)<=0)break; // Back at the aim.
            const int n=dot(miss,unit(first))<25e3?std::max(engines,1):std::max(all,1);const double F=boostbackThrust(m-used,n)*n*raptorSea;
            step(boostbackDirection(miss)*(F/(m-used)),.5);used+=F/raptorExhaust*.5;}
        return used+landingShare;
    }
    // The boostback's throttle on n engines at mass m: boostbackThrottle, at most boostbackG (never under the Raptors' 40%).
    static double boostbackThrust(double m,int n){return std::clamp(boostbackG*m/(std::max(n,1)*raptorSea),.4,boostbackThrottle);}
    void enter(Phase p){phase_=p;} // Its guidance taken up at a phase (a test starting it there, e.g. falling toward its landing burn).
    // plan: the landing burn's planned share of its engines' thrust; boostbackThrottle: the boostback's (40-50 s, as Flights 9 and 10 flew it).
    // normalForce: the body's normal force coefficient a radian off the flow, engines first, as flights measure it (the IMU
    // across the flow; the tables read near 3.5).
    static constexpr double gateSpeed=80,plan=.7,residual=20e3,boostbackThrottle=.8,boostbackG=5*g0,normalForce=.8,landingShare=140e3;
    // lead: the coast's aim moved upstream along the arms' line by the stopping distance of what it reaches the gate moving along it, braked at this (m/s²: the
    // final's braking out in front of the V, finalBrake; 2.5–6 on the bench, 4 the quickest that every wind rides out); the final approach then carrying it in at up
    // to 30 m/s (a short burn kept retrograde leaves 20-30 m/s across).
    static constexpr double leadBrake=4;
    // The final along the arms' line as Flight 7's: braking hard (finalBrake m/s²) out in front of the V, then entering it no faster than the gentle braking
    // from finalMouth m out allows (the speed an escape that takes ~3 s to brake can still stop from before the arms' roots), within finalGain kv of the
    // distance a second. Harder, or a shorter mouth, and its escapes run into the arms (2.5–3 m/s², 40–60 m, .55–.7 on the bench).
    static constexpr double finalBrake=2.5,finalMouth=60,finalGain=.55;
    double dt=.02,boostbackPitch=5*deg; // boostbackPitch: above the horizon (negative: below): up a little, as the flights' (apogee 83-99 km, the landing burn ~4 min after staging).
    // The boostback and the fall aim this far east of the tower (over the sea, away from the tower's side), the landing
    // burn then bringing it onto the arms' axis from there while still high (m); without the catch, onto the sea this far east.
    // divert: the height (m) below which the fins carry it from there onto the approach point.
    // front: the landing burn's point that far east of the axis, in front of the arms' V: the final approach slides in from there
    // as the real catches do (Flight 5's: down outside the tips, then in across them).
    double offshore=300,sea=1500,divert=30e3,front=70;
private:
    static constexpr const char* balanceWhy="conjunto de motores impossível de equilibrar";
    // Gives the catch up for good: the arms stay open, the aim goes out to sea (from west of the tower, inland away from it, north
    // of the tank farm); from low (the last of the landing burn, over the arms), away from the tower at once.
    void giveUp(const State& s,const char* why){
        if(!catch_)return;catch_=false;why_=why;close_=false;const Vec rel=s.position+site_*-1;const double over=dot(rel,up_);
        aim_=dot(rel,east_)>-22?sea_:east_*-400.+north_*300.;
        if(phase_==Phase::final||phase_==Phase::hover||phase_==Phase::descend||(phase_==Phase::landing&&over<1500)){phase_=Phase::divert;held_=over;
            const Vec flat=rel+up_*-over,across=s.velocity+up_*-dot(s.velocity,up_);
            if(collides(s,flat,.7*stopping(s,flat,std::max(set_.count(),1),rush(s)),over)>=0||(!stops(s,stand(s),set_.working(s,33),true)&&overWorks(flat,length_))){escape_=true;threat_=why;}}
    }
    double stand(const State& s)const{return boosterLength-cg(s.propellant);} // Its centre of mass over its base, standing.
    void flightSafety(Command& c,const State& s){
        Vec r=s.position,v=s.velocity;double seconds=0;fly(r,v,s.mass,0,.5,[&]{if(dot(r+site_*-1.,up_)<=0&&dot(v,up_)<0)return true;seconds+=.5;return false;});
        if(dot(r+site_*-1.,up_)>0)return;const Vec rel=r+site_*-1.,flat=rel+up_*-dot(rel,up_);const double e=dot(flat,east_),n=dot(flat,north_);double depth=std::max(1000.-norm(flat),0.);
        for(std::size_t i=0;i<columns.size();++i){const auto& p=columns[i];depth=std::max(depth,(i<2?1000.:p.radius+length_)-std::hypot(e-p.e,n-p.n));}
        if(overWorks(flat,length_))for(const auto& w:works)if(e>w.e0-length_&&e<w.e1+length_&&n>w.n0-length_&&n<w.n1+length_)depth=std::max(depth,std::min({e-w.e0+length_,w.e1+length_-e,n-w.n0+length_,w.n1+length_-n}));
        if(!(depth>0))return; // An uncontrollable fall offshore needs no FTS.
        const int engines=set_.working(s,33);const double usable=std::max(s.propellant-residual,0.),a=engines*raptorSea/s.mass,dv=raptorExhaust*std::log(s.mass/std::max(s.mass-usable,1.));
        const Vec now=s.position+site_*-1.,here=now+up_*-dot(now,up_);if(engines>0&&usable>0&&stops(s,stand(s),engines,true)&&escapes(s,here))return; // A recoverable fall keeps every chance to divert.
        seconds+=2*dv/norm(gravity(s.position));const double reach=std::min(.5*a*seconds*seconds,dv*seconds); // Allow even an optimistic delay from spending the impulse upward.
        if(engines>0&&usable>0&&reach>=depth)return; // Even optimistic all-sideways thrust cannot clear this keep-out: no safe powered divert remains.
        char why[512];std::snprintf(why,sizeof why,"%s; impacto previsto a %.0f m a leste e %.0f m ao norte, na área protegida; %d motores equilibráveis, desvio máximo %.0f m < %.0f m; h %.0f m",why_?why_:balanceWhy,e,n,engines,reach,depth,dot(s.position+site_*-1.,up_));termination_=why;c={};c.terminate=true;
    }
    // The thrust it gets away with (m/s²): 3 g, or all its working engines give when that is less.
    double rush(const State& s)const{return std::min(set_.working(s,33)*raptorSea/s.mass,3*g0);}
    // Enough left to get clear of the ground's works by its length with all it has, from rest: across at 20° of the escape's
    // thrust the way out to the aim (the distance marched in 5 m steps), then 3 s more, 20% kept (the propellant at its flow).
    // escapeNeed: that propellant (kg), as the divert flies it, by its Δv (20% kept): held up while it crosses (at up to 15 m/s,
    // ~5 s getting up to it), coming down meanwhile to 60 m over the works (their tanks and halls; the tower the collision monitor's)
    // at up to 30 m/s, the rest of the way down past them, the fall it has stopped, and the last 30 m/s braked.
    double escapeNeed(const State& s,Vec flat)const{ // Between the arms (away(): straight up first), out over them toward the aim.
        Vec dir=away(s,flat,flat+aim_*-1.);if(!(norm(dir)>0))dir=norm(aim_+flat*-1.)>0?unit(aim_+flat*-1.):cross(north_,up_);double d=0;while(d<600&&overWorks(flat+dir*d,length_))d+=5;
        const double g=norm(gravity(s.position)),h=std::max(dot(s.position+site_*-1.,up_)-stand(s),0.),vz=dot(s.velocity,up_),over=std::min(h,60.);
        return 1.2*s.mass*(g*(std::max(d>0?d/15+5:0.,(h-over)/30)+over/30)+std::max(-vz,0.)+std::min(30.,std::sqrt(2*g*h)))/raptorExhaust;
    }
    bool escapes(const State& s,Vec flat)const{return s.propellant-residual>escapeNeed(s,flat);}
    // How far over the hover point it keeps at flat off the axis (m): in the arms' corridor (on their line, east of the tower)
    // or out east past their tips, a 27° slope down to it; elsewhere its foot 10 m over the tower's top (it gets round the
    // tower and over the arms there, then down the corridor).
    // Inside the V within 5 m of an arm (or of where an arm swings), its base 5 m over the arms until it is further in.
    double safe(const State& s,Vec flat)const{
        const double e=dot(flat,east_),n=dot(flat,north_),slope=.5*std::max(norm(flat)-3,0.);
        if((std::abs(n)<=4&&e>-8)||in_){const double edge=hingeNorth+(e-hingeEast)*std::tan(openAngle)-std::abs(n)-radius-1.5; // The hull to the open arm's beam.
            return std::abs(n)<=1||edge>5||e>13+radius?slope:std::max(slope,std::max(arms_+armTop+5+stand(s)-top(s),0.)*std::clamp((5-edge)/3,0.,1.));}
        return std::max(slope,towerTop+10+stand(s)-top(s));
    }
    // What getting round to the corridor from off it costs (kg): up to that height at 3 m/s and across at 5 m/s, hovering (or
    // at 70% of what it already moves that way: a burn kept retrograde brings it in moving).
    double around(const State& s,Vec flat,double over)const{
        const Vec g=goal(flat),v=s.velocity+up_*-dot(s.velocity,up_);const double toward=norm(g)>0?std::max(-dot(v,unit(g)),0.):0.;
        const double up=std::max(top(s)+safe(s,flat)-over,0.);return (up/3+norm(g)/std::max(5.,.7*toward))*g0*s.mass/raptorExhaust;
    }
    double top(const State& s)const{return catch_?catchHeight(s)+hoverAbove:stand(s);} // The height (over the site) the burns stop at.
    Vec goal(Vec flat)const{return catch_?approach(flat):flat+aim_*-1.;} // From the point it is going for to it.
    // The predicted landing's distance from the approach point (m): what the landing burn would have to divert.
    double reach(const State&)const{return norm(aimed_+aim_+cross(north_,up_)*-front);}
    // Whether braking now straight up on `engines` working engines (of the 13 by default) at full stops the fall above `top` m
    // over the site (the drag as predicted, two thirds of it: on the safe side); dry: only while what is left lasts at their flow.
    bool stops(const State& s,double top,int engines=-1,bool dry=false)const{
        const int n=engines<0?set_.working(s,13):engines;const double g=norm(gravity(s.position)),a=n*raptorSea/s.mass,k=2./3*.5*pi*radius*radius/s.mass,flow=n*raptorSea/raptorExhaust;
        double h=dot(s.position+site_*-1,up_),v=dot(s.velocity,up_),left=std::max(s.propellant-residual,0.);if(h<top)return v>-1; // Below it already: not falling through it, it stops.
        for(int i=0;i<5000&&v<0&&h>=top;++i){const bool on=!dry||left>0;v+=((on?a:0.)-g+k*density(h)*baseDrag(-v,h)*v*v)*.02;h+=v*.02;if(on)left-=flow*.02;}return h>=top;
    }
    // The point to go for (from it to the vehicle) within what an acceleration across of `limit` m/s² can still cover in tgo s
    // (a quarter of the most E-guidance asks: a sixth of a tgo²): out of reach, as near it as it gets.
    static Vec reachable(Vec to,double limit,double tgo){const double most=limit*tgo*tgo/6;return norm(to)>most?to*(most/norm(to)):to;}
    // The propellant (kg) the landing burn needs: `burn` s on the 13 at the planned thrust, the 3's stop from the gate, 15 s more.
    double need(const State& s,double burn)const{
        const double a3=.8*set_.working(s,3)*raptorSea/s.mass;return s.mass/raptorExhaust*(plan*set_.working(s,13)*raptorSea/s.mass*burn+a3*gateSpeed/std::max(a3-g0,1.)+g0*15);
    }
    // The upward acceleration (m/s², the drag's share netted) that slows the fall from `above` m over a point to vf (≤ 0) there
    // at a constant rate (falling slower than vf: eased toward it).
    double vertical(const State& s,double above,double vz,double vf)const{
        const double b=above<0?.5*(std::min(3.,-.3*above)-vz):vz<vf?(vz*vz-vf*vf)/(2*std::max(above,.1)):.5*(vf-vz); // Below it: up at most 3 m/s.
        return std::max(b+norm(gravity(s.position))-drag(s)*(norm(s.velocity)>0?-vz/norm(s.velocity):0),0.);
    }
    // Thrust for the acceleration a (m/s², ECEF) on the lit engines, through the centre of mass, the pins rolled over the arms;
    // the thrust (N).
    void point(Command& c,const State& s,Vec d)const{
        const int g=set_.gimballed();if(!g){hold(c,s,d);unmet_={0,c.torque.y,c.torque.z};return;}aim(c,s,d,set_.centre());const double share=static_cast<double>(set_.count())/g,pitch=c.gimbalPitch,yaw=c.gimbalYaw;
        c.gimbalPitch=std::clamp(c.gimbalPitch*share,-15*deg,15*deg);c.gimbalYaw=std::clamp(c.gimbalYaw*share,-15*deg,15*deg); // Only the gimballed share trims the whole cluster.
        unmet_=unmet_+Vec{0,pitch-c.gimbalPitch/share,yaw-c.gimbalYaw/share}*(s.thrust*stand(s));
    }
    double thrust(Command& c,const State& s,Vec a)const{
        const double f=s.mass*norm(a);c.throttle=std::clamp(f/(std::max(set_.count(),1)*raptorSea),.4,1.);point(c,s,unit(a));roll(c,s,&set_);return f;
    }
    // The engines for a thrust (N): one fewer once they cannot throttle down to it, one more (up to the 13) once short of it.
    void count(Command& c,const State& s,double demand,int least=1){
        const int n=set_.count(),most=set_.working(s,13);if(n>least&&demand<.42*n*raptorSea&&demand<.9*(n-1)*raptorSea)set_.use(c,s,n-1);else if(n<most&&demand>.97*n*raptorSea)for(int k=n+1;k<=13;++k){set_.use(c,s,k);if(set_.count()>n)break;} // A fixed pair may need two starts together.
        if(catch_&&demand>set_.working(s,13)*raptorSea)giveUp(s,balanceWhy); // A balanced cluster must also carry the requested burn.
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
    // the tilt asked is the one held), damped at 0.7, and 70% of K cancelled (an error in it leaves the body stable). The roll
    // too (its inertia some fifty times less): the pins toward the arms, as roll() asks it. beside: the engines steer it (a burn):
    // one law, the gimbal's (aim()); the fins give what its 15° cannot and take the air's pull off it (two laws closing one error
    // each in full outrun their actuators' rates: a limit cycle).
    void finsToward(Command& c,const State& s,Vec d,bool beside=false)const{
        const Vec e=mul(s.toBody,cross(bodyX(s),d)),w=s.rate,off=mul(s.toBody,cross(bodyX(s),unit(s.velocity)));const double i=pitchInertia(s.propellant),g=cg(s.propellant);
        const double k=s.q*pi*radius*radius*std::max(3.1*(g-boosterLength/2)+3*finDrag*(g-finAt),0.),p=i*.3+3*k,v=1.4*std::sqrt(p*i);
        const Vec own=beside?unmet_:Vec{0,p*e.y-v*w.y,p*e.z-v*w.z};c.surfaces=finDeflections({rollInertia(s.propellant)*(.36*rollError(s)-1.2*w.x),own.y+.7*k*off.y,own.z+.7*k*off.z},s.q,g);
    }
    // The fins' deflections that give moment m (body, N m; x: the roll) at dynamic pressure q: fin i gives, a radian, q S finSlope
    // (r, -L u_y, -L u_z) (its force along x × u at r off the axis, L ahead of the centre of mass), the 3 x 3 system solved by
    // Cramer's rule. Past their limits the roll is kept (scaled down only beyond their reach) and the pitch and yaw scaled to what
    // is left, each in its direction: clamping each fin alone would roll the body, its least inertia.
    static std::vector<double> finDeflections(Vec m,double q,double cg){
        const double k=std::max(q,1.)*pi*radius*radius*finSlope,r=radius+finSpan/2,l=cg-finAt;Vec col[3];
        for(int i=0;i<3;++i)col[i]=Vec{r,-l*fins[i].y,-l*fins[i].z}*k;
        auto det=[](Vec a,Vec b,Vec c){return dot(a,cross(b,c));};const double d=det(col[0],col[1],col[2]);
        auto solve=[&](Vec t){return std::array<double,3>{det(t,col[1],col[2])/d,det(col[0],t,col[2])/d,det(col[0],col[1],t)/d};};
        const auto x=solve({m.x,0,0}),y=solve({0,m.y,m.z});double a=1,b=1;
        for(int i=0;i<3;++i)if(std::abs(x[i])>finLimit)a=std::min(a,finLimit/std::abs(x[i]));
        for(int i=0;i<3;++i){const double u=a*x[i];if(y[i]>0&&u+y[i]>finLimit)b=std::min(b,(finLimit-u)/y[i]);else if(y[i]<0&&u+y[i]<-finLimit)b=std::min(b,(-finLimit-u)/y[i]);}
        b=std::max(b,0.);return{a*x[0]+b*y[0],a*x[1]+b*y[1],a*x[2]+b*y[2]};
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
    // seconds: the time to it. The burn flown from every 8th check, then from each since the last one ending above the gate:
    // the first to end low enough, as flying it from every check finds it (its end only falls as it waits), 1/5 of the burns.
    // lead (m/s², 0: none): the aim moved upstream of its point, along the arms' line, by the stopping distance of the speed along
    // it the gate is reached with, braked at that (a burn kept retrograde keeps its path's slant: what it carries in, the final
    // approach takes in).
    Vec landing(const State& s,double* seconds=nullptr,double lead=0)const{
        const double mass=landingMass(s.mass),accel=set_.working(s,13)*plan*raptorSea/mass,low=top(s)+gate(mass,set_.working(s,3))+50;Vec r=s.position,v=s.velocity;int n=0;
        auto height=[&](Vec p){return dot(p+site_*-1,up_);};
        auto gated=[&](Vec p,Vec u,double& t,Vec& w){int m=0;fly(p,u,mass,accel,.25,[&]{++m;return -dot(u,up_)<=gateSpeed||height(p)<0;},.85,3*std::max(accel-g0,1.));t=.25*m;w=u;return p;}; // As the burn flies it.
        Vec at,before,atV,beforeV;double h=1e300,hb=1e300,t=0,tb=0;struct Check{Vec r,v;int n;};std::vector<Check> span;span.reserve(8);
        fly(r,v,mass,0,.5,[&]{++n;if(dot(v,up_)<0&&height(r)<40e3){span.push_back({r,v,n});
            if(span.size()==8){double te;Vec ve;const Vec end=gated(r,v,te,ve);if(height(end)<=low)return true;at=end;atV=ve;h=height(end);t=te;span.clear();}}
            return height(r)<0&&dot(v,up_)<0;});
        for(const auto& k:span){before=at;beforeV=atV;hb=h;tb=t;at=gated(k.r,k.v,t,atV);h=height(at);if(h<=low){n=k.n;break;}}
        if(!(h<=low)){at=gated(r,v,t,atV);hb=h=height(at);}
        const double f=hb<1e299&&hb>h?std::clamp((hb-low)/(hb-h),0.,1.):1;const Vec p=hb<1e299&&hb>h?before+(at+before*-1.)*f:at,pv=hb<1e299&&hb>h?beforeV+(atV+beforeV*-1.)*f:atV;
        if(seconds)*seconds=.5*(n-1)-.5*(1-f)+(hb<1e299&&hb>h?tb+(t-tb)*f:t);const Vec rel=p+site_*-1,across=pv+up_*-dot(pv,up_);
        const double along=dot(across,cross(north_,up_)); // Along the arms' line only: across it the final approach brings it onto the line.
        return rel+up_*-dot(rel,up_)+aim_*-1.+(lead>0?cross(north_,up_)*(along*std::abs(along)/(2*lead)):Vec{});
    }
    // The drag coefficient engines first (the stage's base table and the fins').
    static double baseDrag(double v,double h){const double m=v/sound(h);const auto& x=dragMach;const auto& y=baseDragTable;
        constexpr double k=descentDrag;for(int i=0;i<4;++i)if(m<=x[i+1])return 3*finDrag+k*(y[i]+(y[i+1]-y[i])*(m-x[i])/(x[i+1]-x[i]));return k*y[4]+3*finDrag;}
    double drag(const State& s)const{const double h=norm(s.position)-norm(site_),v=norm(s.velocity+windAt(h)*-1.);return .5*density(h)*baseDrag(v,h)*pi*radius*radius*v*v/s.mass;}
    // The wind it reckons with at a height over the site: the tower's anemometer's (146 m up) to 1.5 km, faded out by 4 km (aloft
    // its own air, which the tower cannot tell).
    Vec windAt(double h)const{return wind_*std::clamp((4e3-h)/2.5e3,0.,1.);}
    Vec wind_;
    // RK4 in ECEF (gravity, the Earth's turning, drag engines first, thrust against the air) until done.
    // upright: within that many m/s of falling at gateSpeed, the thrust straight up (the landing burn's last seconds).
    template<class Done> void fly(Vec& r,Vec& v,double m,double accel,double h,Done done,double dragShare=1,double upright=0)const{
        const Vec w{0,0,7.292115e-5};const double area=pi*radius*radius*dragShare;
        auto acc=[&](Vec p,Vec u){const double rn=norm(p),ht=rn-norm(site_),speed=norm(u);const bool up=upright>0&&-dot(u,up_)-gateSpeed<upright;
            const Vec air=u+windAt(ht)*-1.;const double airspeed=norm(air); // Its drag through the air the tower's anemometer measures (its wind faded out aloft).
            return p*(-earth.mu/(rn*rn*rn))+cross(w,u)*-2.+cross(w,cross(w,p))*-1.+air*(-.5*density(ht)*baseDrag(airspeed,ht)*area*airspeed/m)+(up?up_*accel:speed>0?u*(-accel/speed):Vec{});};
        for(int i=0;i<6000&&!done();++i){
            const Vec a1=acc(r,v),v1=v+a1*(h/2),r1=r+v*(h/2),a2=acc(r1,v1),v2=v+a2*(h/2),r2=r+v1*(h/2),a3=acc(r2,v2),v3=v+a3*h,r3=r+v2*h,a4=acc(r3,v3);
            r=r+(v+v1*2+v2*2+v3)*(h/6);v=v+(a1+a2*2+a3*2+a4)*(h/6);}
    }
    Phase phase_=Phase::wait;const Vec east_=cross(north_,up_);Vec back_,aimed_,pointed_,slewed_,offshore_=east_*offshore,sea_=east_*sea,aim_=offshore_;
    int unsure_=0;bool escape_=false;const char* threat_=nullptr;Vec gated_,gatedV_;double seen_=0,accel_=0,slope_=0,lift_=0,start_=0,miss_=0,seconds_=0,caught_=0,touch_=-1,held_=0,tgo_=10,held_at_=0;int age_=0;bool off_=false,catch_=true,hurry_=false;const char* why_=nullptr;
    // The 13 gimballed engines first (the centre 3, then the middle ring), then the outer ones: the burns' engines, failed ones replaced.
    Engines set_{[]{std::vector<int> v;for(int i=1;i<=33;++i)v.push_back(i);return v;}(),boosterNozzle,13};std::string termination_;
};
}
