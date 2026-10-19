// Thermal-protection nodes (physics/thermal) against closed forms: radiative equilibrium (α q = ε σ T⁴), the exponential
// approach of a node conducting to its structure without radiation, one huge implicit step landing on the equilibrium, and
// the modified Newtonian distribution of the stagnation flux (cos θ windward, nothing leeward).
#include "physics/thermal.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
namespace {
int failures=0;
void near(double got,double want,double tolerance,const std::string& what){const bool ok=std::abs(got-want)<=tolerance;
    std::printf("  %-52s %12.6f  reference %12.6f%s\n",what.c_str(),got,want,ok?"":"  <-- outside");if(!ok)++failures;}
}
int main(){try{
    using namespace albert::physics;
    ThermalNode tile;tile.area=.04;tile.capacity=.04*.025*270*1000; // A 20 cm tile, 25 mm of 270 kg/m³ silica at ~1000 J/kg/K.
    const double q=1e5,stefan=5.670374419e-8,eq=std::pow(tile.absorptivity*q/(tile.emissivity*stefan),.25);
    double t=tile.start;for(int i=0;i<3600;++i)t=nodeStep(tile,t,q,0,1);
    std::printf("Thermal nodes:\n");near(t,eq,.01,"Radiative equilibrium at 100 kW/m² (K)");
    near(nodeStep(tile,tile.start,q,0,1e5),eq,.01,"One implicit step of a day lands there");
    ThermalNode slab=tile;slab.emissivity=0;slab.conductance=2;slab.structure=300; // T∞ = Ts + α q A / G, time constant C/G.
    const double inf=300+slab.absorptivity*1e3*slab.area/slab.conductance,tau=slab.capacity/slab.conductance;t=slab.start;for(int i=0;i<10000;++i)t=nodeStep(slab,t,1e3,0,.01);
    near(t,inf+(300-inf)*std::exp(-100/tau),.05,"Conduction to the structure, 100 s (K)");
    ThermalNode face;face.normal={1,0,0};face.factor=.8;
    near(nodeHeating(face,1e6,{-100,0,0}),8e5,1e-6,"Facing the flow: factor x stagnation");
    near(nodeHeating(face,1e6,{-50,86.60254037844386,0}),4e5,1e-3,"At 60 degrees: half");
    near(nodeHeating(face,1e6,{100,0,0}),0,0,"Lee side: none");
    if(failures)throw std::runtime_error(std::to_string(failures)+" values outside");
    std::printf("thermal_test: ok\n");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
