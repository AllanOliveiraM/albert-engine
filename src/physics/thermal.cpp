#include "physics/thermal.hpp"
#include <algorithm>
#include <cmath>
namespace albert::physics {
namespace {constexpr double stefan=5.670374419e-8;}
double nodeHeating(const ThermalNode& n,double stagnation,V3 air){
    const double speed=norm(air),nn=norm(n.normal);if(!(speed>0)||!(nn>0)||!(stagnation>0))return 0;
    const double c=-dot(n.normal,air)/(nn*speed); // The flow arrives along air: windward faces have normals against it.
    return c>0?stagnation*n.factor*c:0.;
}
double nodeStep(const ThermalNode& n,double t,double q,double sky,double seconds){
    if(!(seconds>0))return t;
    const double c=n.capacity,a=n.area,e=n.emissivity*stefan*a,g=n.conductance,in=q*a*n.absorptivity+e*sky*sky*sky*sky+g*n.structure;
    // Backward Euler: c (T - T0)/dt = in - e T⁴ - g T, solved by Newton from T0 (a few iterations reach machine precision).
    const int steps=std::max(1,static_cast<int>(std::ceil(seconds/10.)));const double dt=seconds/steps;
    for(int k=0;k<steps;++k){
        const double t0=t;double x=t0;
        for(int i=0;i<8;++i){const double f=c*(x-t0)/dt-in+e*x*x*x*x+g*x,df=c/dt+4*e*x*x*x+g;const double nx=std::max(1.,x-f/df);if(std::abs(nx-x)<1e-9*x){x=nx;break;}x=nx;}
        t=x;
    }
    return t;
}
}
