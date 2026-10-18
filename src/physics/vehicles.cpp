#include "physics/vehicles.hpp"
#include <cmath>
namespace albert::physics {
namespace {
constexpr double ft=.3048,slug=14.59390293720636,slugft2=slug*ft*ft,g0Nesc=9.8066,g0=9.80665,pi=3.14159265358979323846;
Table constant(double v){return{{0},{0},{v}};}
Table curve(std::vector<double> a,std::vector<double> v){return{{0},std::move(a),std::move(v)};}
Aerodynamics nescRocket(){
    const std::vector<double> a{-10,-8,-6,-4,-2,0,2,4,6,8,10},m{-20,0,20};
    Aerodynamics r;r.area=7;r.length=3;r.span=3;
    r.lift=curve(a,{-1.6,-1.,-.73,-.49,-.24,0,.24,.49,.73,1.,1.6});
    r.drag=curve(a,{.48,.38,.31,.25,.23,.21,.23,.25,.31,.38,.48});
    r.side=curve(a,{1.6,1.,.73,.49,.24,0,-.24,-.49,-.73,-1.,-1.6});
    r.roll=constant(0);r.pitch=curve(m,{.6,0,-.6});r.yaw=curve(m,{-.6,0,.6});
    return r;
}
struct Part {double mass,from,to,radius;bool solid;};
struct Props {double mass,cg;V3 inertia;};
Props props(const std::vector<Part>& parts){
    Props p{0,0,{}};for(const auto& q:parts){p.mass+=q.mass;p.cg+=q.mass*(q.from+q.to)/2;}p.cg/=p.mass;
    for(const auto& q:parts){const double l=q.to-q.from,d=(q.from+q.to)/2-p.cg,r2=q.radius*q.radius;
        const double axial=q.mass*r2*(q.solid?.5:1.),transverse=q.mass*(l*l/12+r2*(q.solid?.25:.5)+d*d);
        p.inertia+=V3{axial,transverse,transverse};}
    return p;
}
Aerodynamics slender(double area,double length=70,double diameter=3.66){
    // Axial coefficient at zero incidence (with base drag) versus Mach; CNα 2/rad;
    // crossflow drag 1.2 with the 0.6 finite-length factor over the planform (length × diameter).
    const std::vector<double> mach{0,.6,.9,1.05,1.2,1.5,2,3,4,6,10},axial{.30,.31,.38,.58,.60,.55,.47,.37,.31,.26,.23};
    std::vector<double> ang;for(int i=-20;i<=20;i+=2)ang.push_back(i);
    const double planform=length*diameter/area;
    Aerodynamics r;r.area=area;r.length=length;r.span=length;
    Table lift{mach,ang,{}},drag{mach,ang,{}},side{mach,ang,{}};
    for(std::size_t i=0;i<mach.size();++i)for(double d:ang){
        const double a=d*pi/180,s=std::sin(a),c=std::cos(a),cn=2*s*c+.6*1.2*planform*s*std::abs(s),ca=axial[i];
        lift.values.push_back(cn*c-ca*s);drag.values.push_back(ca*c+std::abs(cn*s));side.values.push_back(-cn);
    }
    r.lift=std::move(lift);r.drag=std::move(drag);r.side=std::move(side);r.roll=constant(0);r.pitch=constant(0);r.yaw=constant(0);
    return r;
}
}
Vehicle nescSphere(){
    Stage s;s.name="NESC sphere";s.massFull=s.massEmpty=slug;s.inertiaFull=s.inertiaEmpty={3.6*slugft2,3.6*slugft2,3.6*slugft2};
    s.aero.area=.1963495*ft*ft;s.aero.length=s.aero.span=.5*ft;s.aero.lift=s.aero.side=s.aero.roll=s.aero.pitch=s.aero.yaw=constant(0);s.aero.drag=constant(.1);
    s.length=.5*ft;s.cgFull=s.cgEmpty=s.reference=.25*ft;s.noseRadius=.25*ft;return{"NESC sphere",{s},0,1,true,.5*ft};
}
Vehicle nescTwoStage(){
    Stage a;a.name="NESC first stage";a.propellant=180000;a.massFull=314000;a.massEmpty=134000;a.cgFull=16.91879;a.cgEmpty=9.421642;a.reference=16.91879;
    a.inertiaFull={353250,33501637.473461,33501637.473461};a.inertiaEmpty={150750,10886636.572139,10886636.572139};
    a.thrust=17e6;a.exhaust=360*g0Nesc;a.nozzle=35;a.length=35;a.aero=nescRocket();
    Stage b;b.name="NESC second stage";b.propellant=80000;b.massFull=99000;b.massEmpty=19000;b.cgFull=4.79798;b.cgEmpty=3.947368;b.reference=4.79798;
    b.inertiaFull={111375,941063.762626,941063.762626};b.inertiaEmpty={21375,212384.868421,212384.868421};
    b.thrust=5e6;b.exhaust=390*g0Nesc;b.nozzle=10;b.length=10;b.aero=nescRocket();
    return{"NESC two-stage rocket",{a,b},0,.5,false,3}; // As the references (sims 4-5): half İω, no exhaust flux.
}
Vehicle genericLauncher(double payload){
    constexpr double r=1.83,area=pi*r*r;
    const std::vector<Part> upper{{payload,4,12,1.2,true},{1900,0,13,r,false},{3900,13,27,r,false}};
    auto with=[](std::vector<Part> p,std::initializer_list<Part> more){p.insert(p.end(),more);return p;};
    const auto s2full=props(with(upper,{{92670,15,26,r,true}})),s2empty=props(with(upper,{{0,25,26,r,true}}));
    const std::vector<Part> lower=with(upper,{{21370,27,70,r,false},{4230,67.5,70,r,true}});
    const auto s1full=props(with(lower,{{92670,15,26,r,true},{395700,31,66,r,true}})),s1empty=props(with(lower,{{92670,15,26,r,true}}));
    Stage a;a.name="first stage";a.propellant=395700;a.massFull=s1full.mass;a.massEmpty=s1empty.mass;a.cgFull=s1full.cg;a.cgEmpty=s1empty.cg;
    a.inertiaFull=s1full.inertia;a.inertiaEmpty=s1empty.inertia;a.reference=8;
    a.thrust=8.227e6;a.exhaust=311*g0;a.exitArea=6.12;a.nozzle=68;a.gimbal=5*pi/180;a.minThrottle=.6;a.length=70;a.aero=slender(area);a.noseRadius=r;
    Stage b;b.name="second stage";b.propellant=92670;b.massFull=s2full.mass;b.massEmpty=s2empty.mass;b.cgFull=s2full.cg;b.cgEmpty=s2empty.cg;
    b.inertiaFull=s2full.inertia;b.inertiaEmpty=s2empty.inertia;b.reference=8;
    b.thrust=981e3;b.exhaust=348*g0;b.exitArea=4.9;b.nozzle=26;b.gimbal=5*pi/180;b.minThrottle=.39;b.length=27;b.aero=slender(area);b.noseRadius=r;
    return{"Generic two-stage launcher (approximate)",{a,b},2e5,1,true,2*r};
}
Vehicle genericLander(){
    constexpr double r=2.1,full=15000,empty=6800,h=7; // Body radius, masses (kg), height (m).
    auto inertia=[&](double m){return V3{m*r*r/2,m*(3*r*r+h*h)/12,m*(3*r*r+h*h)/12};};
    Stage s;s.name="lander";s.propellant=full-empty;s.massFull=full;s.massEmpty=empty;s.cgFull=4.2;s.cgEmpty=3.8;s.reference=3.5;
    s.inertiaFull=inertia(full);s.inertiaEmpty=inertia(empty);
    s.thrust=45040;s.exhaust=311*g0;s.exitArea=1.77;s.nozzle=6.3;s.gimbal=6*pi/180;s.minThrottle=.1;s.length=h;s.aero=slender(pi*r*r);s.noseRadius=r;
    constexpr double pad=4.7/1.4142135623730951; // Footpads 4.7 m from the axis at the base.
    s.legs={{h,pad,pad},{h,-pad,pad},{h,pad,-pad},{h,-pad,-pad}};s.legStiffness=8.9e5;s.legDamping=5.7e4;s.friction=.6;
    return{"Generic lander (approximate)",{s},5e3,1,true,2*r};
}
Vehicle genericLunarAscent(){
    constexpr double r=1.7,h=2.8,full=4853,empty=2500; // Effective radius, height (m); masses with crew and samples (kg).
    auto inertia=[&](double m){return V3{m*r*r/2,m*(3*r*r+h*h)/12,m*(3*r*r+h*h)/12};};
    Stage s;s.name="ascent stage";s.propellant=full-empty;s.massFull=full;s.massEmpty=empty;s.cgFull=1.6;s.cgEmpty=1.5;s.reference=1.4;
    s.inertiaFull=inertia(full);s.inertiaEmpty=inertia(empty);
    s.thrust=15570;s.exhaust=311*g0;s.exitArea=.33;s.nozzle=h;s.gimbal=0;s.minThrottle=1;s.length=h;s.aero=slender(pi*r*r,h,2*r);s.noseRadius=r; // Fixed APS: no gimbal, no throttle.
    return{"Generic lunar ascent stage (approximate)",{s},1400,1,true,2*r}; // Attitude by RCS (~1.4 kN m).
}
Vehicle genericMarsAscent(){
    constexpr double r=.285,area=pi*r*r;
    const std::vector<Part> upper{{20,0,.4,.2,true},{50,0,1.2,r,false}}; // Payload (sample container), second-stage structure.
    auto with=[](std::vector<Part> p,std::initializer_list<Part> more){p.insert(p.end(),more);return p;};
    const auto s2full=props(with(upper,{{80,.4,1.1,r,true}})),s2empty=props(with(upper,{{0,1.05,1.1,r,true}}));
    const std::vector<Part> lower=with(upper,{{70,1.2,3,r,false}});
    const auto s1full=props(with(lower,{{80,.4,1.1,r,true},{230,1.3,2.9,r,true}})),s1empty=props(with(lower,{{80,.4,1.1,r,true}}));
    Stage a;a.name="first stage";a.propellant=230;a.massFull=s1full.mass;a.massEmpty=s1empty.mass;a.cgFull=s1full.cg;a.cgEmpty=s1empty.cg;
    a.inertiaFull=s1full.inertia;a.inertiaEmpty=s1empty.inertia;a.reference=.4;
    a.thrust=8000;a.exhaust=310*g0;a.exitArea=.03;a.nozzle=3;a.gimbal=5*pi/180;a.minThrottle=.5;a.length=3;a.aero=slender(area,3,2*r);a.noseRadius=r;
    Stage b;b.name="second stage";b.propellant=80;b.massFull=s2full.mass;b.massEmpty=s2empty.mass;b.cgFull=s2full.cg;b.cgEmpty=s2empty.cg;
    b.inertiaFull=s2full.inertia;b.inertiaEmpty=s2empty.inertia;b.reference=.4;
    b.thrust=2500;b.exhaust=320*g0;b.exitArea=.02;b.nozzle=1.2;b.gimbal=5*pi/180;b.minThrottle=.5;b.length=1.2;b.aero=slender(area,1.2,2*r);b.noseRadius=r;
    return{"Generic Mars ascent vehicle (approximate)",{a,b},100,1,true,2*r};
}
}
