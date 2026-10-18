#include "physics/flight.hpp"
#include "physics/vehicles.hpp"
#include "providers/gravity_models.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace albert;using namespace albert::physics;
namespace {
constexpr double pi=3.14159265358979323846,deg=pi/180;
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
void near(double a,double b,double tol,const char* m){if(!(std::abs(a-b)<=tol)){std::fprintf(stderr,"%s: %.12g vs %.12g (tolerance %.3g)\n",m,a,b,tol);throw std::runtime_error(m);}}
std::string read(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);require(bool(in),"Fixture missing");return{std::istreambuf_iterator<char>(in),{}};}
struct Frame {const char* body;double jd;double m[3][3];};
constexpr Frame planetFrames[]{
#include "planet_frames_fixture.inc"
};
}
// Moon and Mars: GRAIL/MRO fields through the provider (degree-8 PDS excerpts as the
// transport), IAU rotation, third bodies, the Mars climatology and orbits about both.
int main(int argc,char** argv){try{
    require(argc==3,"Usage: central_bodies_test <fixtures> <cache>");
    const std::filesystem::path fixtures=argv[1],cache=argv[2];std::filesystem::remove_all(cache);
    const std::string moonTable=read(fixtures/"moon_gravity_degree8.tab"),marsTable=read(fixtures/"mars_gravity_degree8.tab");
    // Provider: one ranged read up to the degree, then the cache; a corrupt cache refetches.
    int calls=0;std::string range;
    auto serve=[&](const std::string& url,const std::string& r,DownloadProgress*){++calls;range=r;return url.find("gggrx")!=std::string::npos?moonTable:marsTable;};
    std::shared_ptr<const Gravity> moonField,marsField;
    {
        providers::GravityModels models(cache,serve);moonField=models.moon(8);marsField=models.mars(8);
        require(calls==2&&range=="0-5611","Ranged gravity read");
        near(moonField->field().gm,4.9028001224453e12,1,"GRGM1200A GM");near(moonField->field().radius,1738e3,1e-6,"GRGM1200A radius");
        near(moonField->field().c[3],-9.0884339347424299e-5,1e-20,"GRGM1200A C20");near(marsField->field().c[3],-8.7502113235452894e-4,1e-19,"GMM-3 C20");
        providers::GravityModels cached(cache,[](const std::string&,const std::string&,DownloadProgress*)->std::string{throw std::runtime_error("Unexpected download");});
        require(cached.moon(8)->field().c==moonField->field().c,"Cached field");
        std::ofstream(cache/"GMM-3-8.tab")<<"garbage";providers::GravityModels again(cache,serve);again.mars(8);require(calls==3,"Corrupt cache refetches");
        bool threw=false;try{providers::GravityModels(cache,serve).mars(121);}catch(const std::invalid_argument&){threw=true;}require(threw,"Degree above the model");
        std::string bad=moonTable;bad.replace(bad.find("-9.0884"),7,"    nan");threw=false;try{providers::parseShadr(bad,8,"x");}catch(const std::runtime_error&){threw=true;}require(threw,"Non-finite coefficient");
        threw=false;try{providers::parseShadr(moonTable.substr(0,moonTable.size()-122*5),8,"x");}catch(const std::runtime_error&){threw=true;}require(threw,"Truncated table");
    }
    // Surface attraction: GM/R² with the J2 term at the equator; the field tends to the point mass.
    {
        const Planet moon=planet(Center::moon,moonField.get(),8),mars=planet(Center::mars,marsField.get(),8);
        near(norm(gravitation(moon,{1737.4e3,0,0})),1.6243,.004,"Lunar surface gravity");
        near(norm(gravitation(mars,{3396.19e3,0,0})),3.724,.008,"Martian equatorial gravity");
        const V3 far{2e8,1e8,-5e7};near(norm(gravitation(moon,far))/(moon.gm/dot(far,far)),1,1e-7,"Lunar field far away");
        bool threw=false;try{planet(Center::mars,marsField.get(),9);}catch(const std::invalid_argument&){threw=true;}require(threw,"Planet degree above the field");
    }
    // Rotation: Mars against CSPICE pxform(IAU_MARS,J2000) with pck00011; the Moon 103.9" (the
    // NAIF DE440 principal-axis offset) from the IAU mean-Earth frame; ω against the rotation's derivative.
    {
        const Rotation mars=Rotation::body(Center::mars),moon=Rotation::body(Center::moon);
        int checked=0;
        for(const auto& f:planetFrames){if(std::string(f.body)!="mars")continue;++checked;
            const M3 m=transpose(mars.at(static_cast<Tt>(std::llround((f.jd-2451545.)*86400e9))).fixed);
            for(int r=0;r<3;++r)for(int c=0;c<3;++c)near(m.m[r][c],f.m[r][c],2e-9,"Mars orientation differs from CSPICE");}
        require(checked==3,"Mars CSPICE fixtures");
        for(const Rotation* body:{&mars,&moon})for(double day:{0.,9786.}){
            const Tt t=static_cast<Tt>(day*86400e9),h=4*second;const M3 a=body->at(t-h).fixed,b=body->at(t+h).fixed,m=body->at(t).fixed;
            M3 w{{{0,0,0},{0,0,0},{0,0,0}}};for(int r=0;r<3;++r)for(int c=0;c<3;++c)for(int k=0;k<3;++k)w.m[r][c]+=(b.m[k][r]-a.m[k][r])/8.*m.m[k][c]; // [ω×] = Ṁᵀ M.
            const V3 o=body->at(t).omega;near(norm(o-V3{w.m[2][1],w.m[0][2],w.m[1][0]})/norm(o),0,1e-7,"Angular velocity"); // Double-precision days limit the difference; libration rates are ~1e-4 of ω.
        }
        auto R1=[](double a){return M3{{{1,0,0},{0,std::cos(a),std::sin(a)},{0,-std::sin(a),std::cos(a)}}};};
        auto R3=[](double a){return M3{{{std::cos(a),std::sin(a),0},{-std::sin(a),std::cos(a),0},{0,0,1}}};};
        auto S=[](double x){return std::sin(x*deg);};auto C=[](double x){return std::cos(x*deg);};
        const double ra=269.9949-3.8787*S(125.045)-.1204*S(250.089)+.07*S(260.008)-.0172*S(176.625)+.0072*S(311.589)-.0052*S(15.134)+.0043*S(25.053),
            dec=66.5392+1.5419*C(125.045)+.0239*C(250.089)-.0278*C(260.008)+.0068*C(176.625)-.0029*C(311.589)+.0009*C(134.963)+.0008*C(15.134)-.0009*C(25.053),
            w=38.3213+3.5610*S(125.045)+.1208*S(250.089)-.0642*S(260.008)+.0158*S(176.625)+.0252*S(357.529)-.0066*S(311.589)-.0047*S(134.963)-.0046*S(276.617)+.0028*S(34.226)+.0052*S(15.134)+.0040*S(119.743)+.0019*S(239.961)-.0044*S(25.053);
        const M3 me=R3(w*deg)*R1((90-dec)*deg)*R3((90+ra)*deg),offset=moon.at(0).fixed*transpose(me);
        near(std::acos((offset.m[0][0]+offset.m[1][1]+offset.m[2][2]-1)/2)/deg*3600,103.913,.01,"Lunar principal-axis offset");
        const double y=78.6944/3600*deg,z=67.8526/3600*deg;near(offset.m[2][0],std::sin(y),1e-12,"Offset about y");near(offset.m[1][0],-std::sin(z)*std::cos(y),1e-12,"Offset about z");
        // The Moon keeps one face to the Earth: the sub-Earth point stays within the optical librations over a month.
        double maxLon=0,maxLat=0;
        for(int day=0;day<=30;++day){
            const Tt t=ttFromUtc(1792238400.+day*86400.);ThirdBody b[2];require(thirdBodies(Center::moon,t,b)==2,"Lunar third bodies");
            near(norm(b[0].position),3.85e8,2.2e7,"Earth distance from the Moon");
            const V3 e=moon.at(t).fixed*b[0].position;maxLon=std::max(maxLon,std::abs(std::atan2(e.y,e.x)));maxLat=std::max(maxLat,std::abs(std::asin(e.z/norm(e))));
        }
        std::printf("bodies: sub-Earth point within %.2f deg longitude, %.2f deg latitude over a month\n",maxLon/deg,maxLat/deg);
        require(maxLon<8.5*deg&&maxLat<7.5*deg&&maxLon>3*deg&&maxLat>3*deg,"Lunar librations");
        constexpr double au=149597870700.;
        for(int day=0;day<=780;day+=60){ThirdBody b[2];require(thirdBodies(Center::mars,ttFromUtc(1792238400.+day*86400.),b)==1,"Martian third bodies");const double d=norm(b[0].position)/au;require(d>1.38&&d<1.67,"Sun distance from Mars");}
    }
    // Mars climatology: Glenn surface values, continuous at the isothermal top, falling to vacuum.
    {
        const Air s=marsAtmosphere(0);near(s.pressure,699,1e-9,"Mars surface pressure");near(s.temperature,242.1,1e-9,"Mars surface temperature");near(s.density,.015030,1e-5,"Mars surface density");
        const double top=(133.1-23.4)/.00222;near(marsAtmosphere(top+1e-3).pressure/marsAtmosphere(top-1e-3).pressure,1,5e-7,"Isothermal layer joins");
        double last=s.density;for(double h=1000;h<marsTop;h+=1000){const double r=marsAtmosphere(h).density;require(r>0&&r<last,"Martian density falls");last=r;}
        const double d100=marsAtmosphere(1e5).density;require(d100>1e-8&&d100<1e-6,"Martian density at 100 km");require(marsAtmosphere(marsTop).density==0,"Vacuum above 150 km");
    }
    // Orbits: 100 km lunar polar and 300 km Martian circular orbits, one revolution with 1 s
    // coast steps against 0.1 s; the Moon has no air.
    for(Center c:{Center::moon,Center::mars}){
        const Environment e=environment(c,c==Center::moon?moonField:marsField,8);const double r=e.planet.radius+(c==Center::moon?1e5:3e5),v=std::sqrt(e.planet.gm/r);
        State s;s.time=ttFromUtc(1792238400.);s.position={r,0,0};s.velocity={0,v*std::cos(80*deg),v*std::sin(80*deg)};s.propellant=0;
        const Vehicle l=genericLauncher();Flight fine(l,e,s),coarse(l,e,s);fine.steps(second/10);coarse.steps(second/1000,second);
        const Tt span=static_cast<Tt>(2*pi*r/v*1e9);double low=1e30,high=0;
        for(Tt t=0;t<span;){t+=coarse.advance(std::min<Tt>(10*second,span-t));const double h=geodetic(e.planet,e.rotation.at(coarse.state().time).fixed*coarse.state().position).height;low=std::min(low,h);high=std::max(high,h);}
        for(Tt t=0;t<span;)t+=fine.advance(span-t);
        const double gap=norm(fine.state().position-coarse.state().position);
        std::printf("bodies: %s orbit %.1f-%.1f km, 1 s steps within %.2e m of 0.1 s\n",c==Center::moon?"lunar":"Martian",low/1e3,high/1e3,gap);
        near(gap,0,.05,"Orbit step convergence");require(low>(c==Center::moon?7e4:2.6e5)&&high<(c==Center::moon?1.3e5:3.4e5),"Orbit stays near circular");
        if(c==Center::moon)require(fine.sample().air.density==0,"No lunar air");else require(Flight(l,e,Flight::free(e,l,s.time,{0,0,1000},{},{})).sample().air.density>.01,"Martian air near the surface");
    }
    std::cout<<"Moon and Mars bodies checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
