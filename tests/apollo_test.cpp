// Apollo 11 against its flight: the Saturn V (AS-506) flown by its flight program (examples/saturn_v.hpp) in Albert's
// climatology for 1969-07-16 13:32 UTC at Launch Complex 39A (NRLMSISE-00 and HWM14), compared with the flight evaluation
// report (MPR-SAT-FE-69-9, Tables 4-1 to 4-5) and Apollo by the Numbers (NASA SP-2000-4029).
//  1. The S-IC from the pad. AS-506's pitch program is unpublished: the program flies Apollo 12's (AS-507), so the
//     path is checked against Apollo 12's flight with that program and the speeds against Apollo 11's.
//  2. The S-II and S-IVB from AS-506's reconstructed state at S-IC/S-II separation to the parking orbit.
//  3. Eagle's powered descent by Luminary 99's guidance and targets from the reconstructed state at ignition to Tranquility
//     Base, against the premission nominal (Bennett, NASA TN D-6846, Table I) and the flight (Apollo 11 Mission Report).
#include "apollo_descent.hpp"
#include "physics/msis.hpp"
#include "physics/vehicles.hpp"
#include "providers/gravity_models.hpp"
#include "saturn_v.hpp"
#include <fstream>
#include <iterator>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace albert::physics;
namespace ex=albert::example;
namespace {
constexpr double pi=3.14159265358979323846,deg=pi/180,nmi=1852;
int failures=0;
void within(double got,double want,double tolerance,const char* what){
    const bool ok=std::abs(got-want)<=tolerance;std::printf("  %-42s %12.3f  reference %12.3f  %s\n",what,got,want,ok?"":"<-- outside");
    if(!ok)++failures;
}
ex::Vec vec(V3 v){return{v.x,v.y,v.z};}
// Height, space-fixed speed, flight-path angle and heading (degrees from north) as the report gives them.
struct Conditions {double t,height,speed,path,heading,mass;};
Conditions conditions(const Flight& f,double range){
    const State& s=f.state();const Sample x=f.sample();const double r=norm(s.position),v=norm(s.velocity);
    const V3 n=matrix(x.nedAttitude)*(transpose(matrix(s.attitude))*s.velocity); // GCRF to NED through the body.
    return{range,x.geodetic.height,v,std::asin(dot(s.position,s.velocity)/(r*v))/deg,std::atan2(n.y,n.x)/deg,x.mass};
}
ex::Reading reading(const Flight& f,Tt t0){
    const Sample s=f.sample();const State& st=f.state();ex::Reading r;r.elapsed=secondsSinceJ2000(st.time-t0);r.height=s.geodetic.height;r.nedVelocity=vec(s.nedVelocity);
    r.qw=s.nedAttitude.w;r.qx=s.nedAttitude.x;r.qy=s.nedAttitude.y;r.qz=s.nedAttitude.z;r.iw=st.attitude.w;r.ix=st.attitude.x;r.iy=st.attitude.y;r.iz=st.attitude.z;
    r.rate=vec(st.rate);r.specificForce=vec(s.specificForce);r.position=vec(st.position);r.velocity=vec(st.velocity);
    r.stage=st.stage;r.stages=static_cast<int>(f.vehicle().stages.size());r.burning=st.burning;r.clamped=st.clamped;r.propellant=st.propellant;r.mass=s.mass;return r;
}
// Flies the program at 50 Hz up to range time `until` or orbit; `each` sees every reading first.
template<class Each> void fly(Flight& f,ex::SaturnV& g,Tt t0,double until,Each&& each){
    while(g.phase()!=ex::SaturnV::Phase::orbit&&!f.state().landed){
        const ex::Reading r=reading(f,t0);const double t=g.range(r);if(t>=until)break;
        each(t);const auto c=g.step(r);
        if(c.separate)f.separate();if(c.ignite)f.ignite();if(c.cutoff)f.cutoff();if(c.throttle>=0)f.throttle(c.throttle);
        f.torque({c.torque.x,c.torque.y,c.torque.z});
        for(Tt left=20*second/1000;left>0;)left-=f.advance(left);
    }
}
ex::SaturnV program(const Vehicle& v){ex::SaturnV g;for(std::size_t i=0;i<g.inertia.size();++i)g.inertia[i]=v.stages[i].inertiaFull.y;return g;}
}
int main(int argc,char** argv){try{
    if(argc<2)throw std::runtime_error("usage: apollo_test <tests directory>");
    const double zero=-14552880.; // 1969-07-16 13:32:00 UTC, range zero.
    Environment e;e.ground=0;e.rotation=Rotation::iau(secondsSinceJ2000(ttFromUtc(zero))-(zero-946728000.)); // TT-UTC for UT1, as Albert's flights.
    e.air=std::make_shared<const Msis>(SpaceWeather{},"NRLMSISE-00/HWM14 climatology"); // Nominal solar indices: they act above ~100 km.
    const Vehicle v=saturnV();
    std::printf("Saturn V AS-506 (Apollo 11)\n 1. S-IC from Launch Complex 39A with Apollo 12's pitch program:\n");
    {
        const Tt t0=ttFromUtc(zero+ex::SaturnV::release);
        Flight f(v,e,Flight::pad(e,v,t0,{28.6084*deg,-80.6041*deg,0},90*deg,90*deg));ex::SaturnV g=program(v);
        double maxq=0,maxqAt=0,mach1=0;Conditions ceco{},oeco{};
        fly(f,g,t0,162.3,[&](double t){const Sample s=f.sample();
            if(s.dynamicPressure>maxq){maxq=s.dynamicPressure;maxqAt=t;}if(mach1==0&&s.mach>=1)mach1=t;
            if(ceco.t==0&&t>=135.2)ceco=conditions(f,t);if(oeco.t==0&&t>=161.63)oeco=conditions(f,t);});
        within(mach1,66.3,1.5,"Mach 1 (s)");within(maxqAt,83.0,3,"maximum q (s)");within(maxq/1e3,35.2,1.8,"maximum q (kPa)");
        within(ceco.speed,1979.0,20,"centre cutoff: space-fixed speed (m/s)");within(oeco.speed,2764.1,28,"outboard cutoff: speed (m/s)");
        within(oeco.mass,827292,1500,"outboard cutoff: mass (kg)");
        std::printf("  against Apollo 12, which flew this pitch program (SP-2000-4029):\n");
        within(ceco.height/1e3,24.158*nmi/1e3,1.5,"centre cutoff: height (km)");within(ceco.path,23.944,.8,"centre cutoff: flight-path angle (°)");
        within(oeco.height/1e3,36.773*nmi/1e3,1.5,"outboard cutoff: height (km)");within(oeco.path,20.513,.8,"outboard cutoff: flight-path angle (°)");
        within(oeco.heading,75.231,.5,"outboard cutoff: heading (°)");
    }
    std::printf(" 2. S-II and S-IVB from AS-506's state at separation (162.3 s) to the parking orbit:\n");
    {
        // MPR Table 4-3 and SP-2000-4029: 66.7 km, 2,773.9 m/s space-fixed at 19.020° and heading 75.436°, 28.865° N, 79.676° W.
        const Tt t0=ttFromUtc(zero+162.3);const Geodetic site{28.865*deg,-79.676*deg,66.7e3};
        const double speed=2773.9,path=19.020*deg,heading=75.436*deg;
        const Orientation o=e.rotation.at(t0);const V3 fixed=fixedPosition(e.planet,site);
        const V3 ned{speed*std::cos(path)*std::cos(heading),speed*std::cos(path)*std::sin(heading),-speed*std::sin(path)};
        const V3 relative{ned.x,ned.y-norm(o.omega)*std::hypot(fixed.x,fixed.y),ned.z}; // Less Earth's rotation, east.
        State s=Flight::free(e,v,t0,site,relative,{heading,path,0});s.stage=1;s.propellant=v.stages[1].propellant;s.burning=false;
        Flight f(v,e,s);ex::SaturnV g=program(v);const V3 pole=transpose(o.fixed)*V3{0,0,1},along=s.velocity*(1/norm(s.velocity));
        g.resume(162.3,vec(along),vec(pole)); // Attitude held along the velocity until the steering starts at 204.1 s.
        Conditions iiCeco{},iiOeco{},end{};
        fly(f,g,t0,800,[&](double t){
            if(iiCeco.t==0&&t>=460.62)iiCeco=conditions(f,t);if(iiOeco.t==0&&t>=548.22)iiOeco=conditions(f,t);
            if(f.state().stage==4&&f.state().burning)end=conditions(f,t);});
        if(g.phase()!=ex::SaturnV::Phase::orbit)throw std::runtime_error("Saturn V did not reach orbit");
        const V3 rr=f.state().position,vv=f.state().velocity;const double mu=3.986004418e14,rn=norm(rr),a=1/(2/rn-dot(vv,vv)/mu),h=norm(cross(rr,vv));
        const double ecc=std::sqrt(std::max(0.,1-h*h/(mu*a))),sphere=6378165; // The report's apogee and perigee are above this sphere.
        const V3 date=transpose(e.rotation.at(f.state().time).fixed)*V3{0,0,1};const double inclination=std::acos(dot(cross(rr,vv),date)/h)/deg;
        const double flight[2][4]{{180.2,5707.5,.897,79.646},{187.3,6916.1,.608,82.389}};const Conditions* at[2]{&iiCeco,&iiOeco};
        const char* names[2]{"S-II centre cutoff","S-II outboard cutoff"};
        for(int i=0;i<2;++i){std::printf("  %s at %.2f s:\n",names[i],at[i]->t);
            within(at[i]->height/1e3,flight[i][0],6,"height (km)");within(at[i]->speed,flight[i][1],flight[i][1]*.01,"space-fixed speed (m/s)");
            within(at[i]->path,flight[i][2],.6,"flight-path angle (°)");within(at[i]->heading,flight[i][3],.6,"heading (°)");} // Path: not the IGM, 0.5° steeper midway.
        std::printf("  S-IVB cutoff:\n");
        within(end.t,699.34,5,"time (s)");within(end.mass,135182,1200,"mass (kg)");within(end.speed,7791.2,10,"space-fixed speed (m/s)");
        within((a*(1+ecc)-sphere)/1e3,186.0,3,"apogee (km)");within((a*(1-ecc)-sphere)/1e3,183.2,3,"perigee (km)");within(inclination,32.521,.05,"inclination (°)");
    }
    std::printf(" 3. Eagle's powered descent from ignition (1969-07-20 20:05:05 UTC) to Tranquility Base:\n");
    {
        std::ifstream in(std::string(argv[1])+"/moon_gravity_degree8.tab",std::ios::binary);const std::string table{std::istreambuf_iterator<char>(in),{}};
        Environment e=environment(Center::moon,albert::providers::gravityModel(Center::moon,table,8).gravity,8);
        // Mission report (best estimates): the site at 0.647° N, 23.505° E, 1,735.58 km from the centre; ignition 15,050 m above it
        // at 1.037° N, 39.371° E, 1,696.2 m/s space-fixed at +0.03°, westward along the great circle to the site.
        e.ground=1735.58e3-1737.4e3;const Geodetic site{.647*deg,23.505*deg,e.ground},pdi{1.037*deg,39.371*deg,e.ground+15050};
        const Tt t0=ttFromUtc(zero+369185.2);const Orientation o=e.rotation.at(t0);
        const V3 S=fixedPosition(e.planet,site),P=fixedPosition(e.planet,pdi),up=P*(1/norm(P)),motion=cross(cross(P,S),P);
        const V3 north{-std::sin(pdi.latitude)*std::cos(pdi.longitude),-std::sin(pdi.latitude)*std::sin(pdi.longitude),std::cos(pdi.latitude)},east=cross(north,up);
        const double heading=std::atan2(dot(motion,east),dot(motion,north)),speed=1696.2,path=.03*deg;
        const V3 inertial{speed*std::cos(path)*std::cos(heading),speed*std::cos(path)*std::sin(heading),-speed*std::sin(path)};
        const V3 relative{inertial.x,inertial.y-norm(o.omega)*std::hypot(P.x,P.y),inertial.z};
        const Vehicle v=apolloLunarModule();Flight f(v,e,Flight::free(e,v,t0,pdi,relative,{heading+pi,0,0}));
        ex::ApolloDescent g(vec(S),vec(P));g.inertia=v.stages[0].inertiaFull.y;
        double dv=0,touchdown=0,lowAt=0;V3 gate{},low{};ex::ApolloDescent::Phase last=g.phase();
        while(g.phase()!=ex::ApolloDescent::Phase::landed&&f.state().time-t0<900*second){
            const ex::Reading r=[&]{ex::Reading x=reading(f,t0);const Sample s=f.sample();x.fixedPosition=vec(s.fixedPosition);x.fixedVelocity=vec(s.fixedVelocity);x.contact=f.state().contact;return x;}();
            const ex::Vec R=g.frame(r.fixedPosition+vec(S)*-1),V=g.frame(r.fixedVelocity); // Height above the site, vertical and horizontal speed.
            if(g.phase()!=last){if(last==ex::ApolloDescent::Phase::braking)gate=V3{R.x,V.x,std::hypot(V.y,V.z)};last=g.phase();}
            if(lowAt==0&&g.phase()==ex::ApolloDescent::Phase::approach&&R.x<=512*.3048){lowAt=r.elapsed;low=V3{R.x,V.x,std::hypot(V.y,V.z)};} // Low gate: 512 ft.
            const auto c=g.step(r);if(c.ignite)f.ignite();if(c.cutoff){f.cutoff();touchdown=r.elapsed;}if(c.throttle>=0)f.throttle(c.throttle);
            f.torque({c.torque.x,c.torque.y,c.torque.z});
            for(Tt left=20*second/1000;left>0;){const Tt h=f.advance(left);left-=h;dv+=norm(f.sample().specificForce)*secondsSinceJ2000(h);}
        }
        if(g.phase()!=ex::ApolloDescent::Phase::landed)throw std::runtime_error("Eagle did not land");
        const double miss=norm(f.sample().fixedPosition-S); // From the aim point, on the ground.
        within(g.recovery(),386,12,"throttle recovery (s; plan 384)");
        within(g.highGate(),506,12,"high gate, P64 (s; flight 507)");within(gate.x,2291,250,"  height (m; flight 2,173)");
        within(gate.y,-44.2,6,"  vertical speed (m/s; flight -38.1)");within(gate.z,154.2,15,"  horizontal speed (m/s)");
        within(lowAt,606,15,"low gate, 512 ft (s)");within(low.y,-16*.3048,2,"  vertical speed (m/s)");within(low.z,55*.3048,6,"  horizontal speed (m/s)");
        within(touchdown,714,25,"touchdown (s; flight 754.7 flown by hand)");within(dv,6775*.3048,40,"velocity change (m/s; flight 2,112)");
        within(f.state().propellant,957*.4536,60,"propellant left (kg; flight 349)");within(miss,0,150,"from the aim point (m)");
    }
    if(failures)throw std::runtime_error(std::to_string(failures)+" quantities outside the flight's tolerance");
    std::cout<<"apollo_test: ok\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
