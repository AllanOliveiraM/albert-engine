#include "core/comets.hpp"
#include "core/approximate_ephemeris.hpp"
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
using namespace albert::core;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double norm(Vector v){return std::hypot(v.x,v.y,v.z);}
double dot(Vector a,Vector b){return a.x*b.x+a.y*b.y+a.z*b.z;}
int main(){try{
    // Measured activity (Knight et al. 2017; Schleicher et al. 1998).
    const auto& p67=*cometActivity("67p");const auto& halley=*cometActivity("halley");
    require(std::abs(afrho(p67,1.25,true)-15)<1e-12&&std::abs(afrho(p67,1.25,false)-15)<1e-12,"67P perihelion Afρ");
    require(std::abs(afrho(p67,2.99,true)-1.42)<.02,"67P inbound Afρ at 2.99 AU (Nov. 2014, ~140 cm)");
    require(afrho(p67,2.,false)<afrho(p67,2.,true)*.75&&afrho(p67,4.4,true)==0&&afrho(halley,6.1,true)==0&&std::abs(afrho(halley,.9,true)-800)<1e-9,"Activity slopes and onset");
    require(!cometActivity("vesta"),"Inactive body has a coma");
    // Halley-Marcus dust phase function (Schleicher 2010).
    require(dustPhase(0)==1&&std::abs(dustPhase(90)-.5375)<1e-12&&std::abs(dustPhase(180)-1231.1735)<1e-9&&std::abs(dustPhase(89.5)-std::sqrt(.5253*.5375))<1e-12,"Dust phase table");
    // Universal-variable two-body motion: circular quarter turn, a hyperbola against
    // a fine numerical integration, and a backwards round trip.
    const auto quarter=propagate({{1,0,0},{0,1,0}},1,std::numbers::pi/2);
    require(norm(quarter.position-Vector{0,1,0})<1e-12&&norm(quarter.velocity-Vector{-1,0,0})<1e-12,"Circular orbit");
    StateVector hyper{{1,0,0},{0,1.5,.2}};const auto analytic=propagate(hyper,1,3);
    {StateVector s=hyper;const int steps=300000;const double h=3./steps;
     auto accel=[](Vector p){const double r=norm(p);return p*(-1/(r*r*r));};
     for(int i=0;i<steps;++i){const auto k1v=accel(s.position),k1p=s.velocity;const auto k2v=accel(s.position+k1p*(h/2)),k2p=s.velocity+k1v*(h/2);
        const auto k3v=accel(s.position+k2p*(h/2)),k3p=s.velocity+k2v*(h/2);const auto k4v=accel(s.position+k3p*h),k4p=s.velocity+k3v*h;
        s.position=s.position+(k1p+k2p*2+k3p*2+k4p)*(h/6);s.velocity=s.velocity+(k1v+k2v*2+k3v*2+k4v)*(h/6);}
     require(norm(s.position-analytic.position)<1e-9&&norm(s.velocity-analytic.velocity)<1e-9,"Hyperbolic orbit");}
    const auto back=propagate(analytic,1,-3);require(norm(back.position-hyper.position)<1e-10,"Backwards propagation");
    // Consistent with the comets' own elements: a state propagated 2 years forward
    // lands on the elements' later state (metres over billions of kilometres).
    for(const char* id:{"67p","halley"}){const auto& orbit=*localOrbit(id);const double jd=2461329.5;
        const auto then=localState(orbit,jd-730),now=localState(orbit,jd);
        const auto moved=propagate({then.positionMeters,then.velocityMetersPerSecond},solarGM,730*86400.);
        require(norm(moved.position-now.positionMeters)<1e3,"Propagation disagrees with the comet elements");}
    // Finson-Probstein grid: no dust far from the Sun; near 67P's 2015 perihelion the
    // grains trail antisunward, small grains (high beta) farther.
    const std::vector<double> betas{.01,.1,.5},ages{86400.,10*86400.};
    const auto quiet=dustGrid(heliocentricState(*localOrbit("halley")),halley,2461329.5,betas,ages);
    for(double r:quiet.rate)require(r==0,"Dust released beyond the activity onset");
    const double perihelion=2457247.5; // 2015-08-13
    const auto grid=dustGrid(heliocentricState(*localOrbit("67p")),p67,perihelion,betas,ages);
    const auto sunward=localState(*localOrbit("67p"),perihelion).positionMeters;const Vector out=sunward*(1/norm(sunward));
    for(std::size_t i=0;i<betas.size();++i)for(std::size_t j=0;j<ages.size();++j){
        const auto& offset=grid.offsets[i*ages.size()+j];require(grid.rate[i*ages.size()+j]>0,"Active comet released no dust");
        require(dot(offset,out)>0,"Dust not antisunward");}
    require(norm(grid.offsets[2*ages.size()+1])>norm(grid.offsets[1])*10,"Radiation pressure does not separate grain sizes");
    // Ten-day grains of beta 0.5: displacement close to the constant-acceleration
    // estimate beta*g*t^2/2 (curvature and changing distance aside).
    const double g=solarGM/dot(sunward,sunward),expected=.5*.5*g*std::pow(10*86400.,2);
    require(std::abs(norm(grid.offsets[2*ages.size()+1])/expected-1)<.25,"Radiation pressure displacement");
    // Observed tail: 67P on 2015-09-23 showed its dust tail at position angle ~285°
    // (Knight et al. 2017, fig. 4). Nucleus and Earth from Horizons (JPL#K284/1 and
    // DE441, heliocentric ICRF at 00:00 TDB); grains weighted by released cross-section.
    {const StateVector comet{{-28230229.50562473e3,174034134.9583235e3,95420956.08915178e3},{-32.37739121930087e3,1.370490316402384e3,4.191725575438226e3}};
     const Vector earth{150133493.3289489e3,-1333612.298686994e3,-579354.1328524561e3};const double jd0=2457288.5;
     const StateAt at=[&](double t){return propagate(comet,solarGM,(t-jd0)*86400.);};
     std::vector<double> b,a;for(int i=0;i<24;++i)b.push_back(.003*std::pow(.9/.003,i/23.));for(int j=0;j<48;++j)a.push_back(86400.*.05*std::pow(1200.,j/47.));
     const auto tail=dustGrid(at,p67,jd0,b,a);const Vector sight=comet.position-earth;const double distance=norm(sight);
     const double ra=std::atan2(sight.y,sight.x),dec=std::asin(sight.z/distance);
     const Vector east{-std::sin(ra),std::cos(ra),0},north{-std::sin(dec)*std::cos(ra),-std::sin(dec)*std::sin(ra),std::cos(dec)};
     double e=0,n=0;for(std::size_t i=0;i<b.size();++i)for(std::size_t j=0;j<a.size();++j){
        const auto& o=tail.offsets[i*a.size()+j];const double x=dot(o,east),y=dot(o,north),rho=std::hypot(x,y);if(rho<1e6||rho>2e7)continue; // Ground images: 10^3-10^4 km.
        const double w=tail.rate[i*a.size()+j]*(j?a[j]-a[j-1]:a[0]);e+=w*x/rho;n+=w*y/rho;}
     const double angle=std::fmod(std::atan2(e,n)*180/std::numbers::pi+360,360);
     require(std::abs(angle-285)<10,"Dust tail position angle differs from the 2015 observation");}
    std::cout<<"Comets OK: measured activity, dust phase, two-body propagation, Finson-Probstein grains and the observed 67P tail direction\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
