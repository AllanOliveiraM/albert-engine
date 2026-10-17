#include "shapes.hpp"
#include "asset_pack.hpp"
#include "core/bodies.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
using namespace threepp;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    require(argc==2,"Provide the assembled asset library");albert::AssetPack pack(argv[1]);
    {   // Gaskell ICQ face centres may sit ~100 m off the axis (Phoebe: 47 and 133 m); such a pole has an
        // arbitrary longitude and must take its plate's, even when listed first in the plate.
        std::string raw("ALBMSH1\0",8);auto word=[&](std::uint32_t w){for(int k=0;k<4;++k)raw.push_back(char(w>>(8*k)&255));};
        std::vector<float> xyz{-100.f,1.7f,1e5f,0.f,-40.f,-1e5f};
        for(int i=0;i<8;++i){const double lon=i*std::numbers::pi/4;xyz.insert(xyz.end(),{float(1e5*std::cos(lon)),float(1e5*std::sin(lon)),0.f});}
        std::vector<std::uint32_t> plates;for(std::uint32_t i=0;i<8;++i)plates.insert(plates.end(),{0,2+i,2+(i+1)%8,1,2+(i+1)%8,2+i});
        word(10);word(16);for(float f:xyz)word(std::bit_cast<std::uint32_t>(f));for(auto plate:plates)word(plate);
        auto model=albert::decodeShape(raw);const auto* uv=model.geometry->getAttribute<float>("uv");const auto* indices=model.geometry->getIndex();
        for(int i=0;i<indices->count();i+=3){float lo=10,hi=-10;for(int j=0;j<3;++j){const float u=uv->getX(indices->getX(i+j));lo=std::min(lo,u);hi=std::max(hi,u);}require(hi-lo<=.25001f,"Off-axis pole split its plate across the longitude seam");}
    }
    {   // A plate pierced by the polar axis with no corner near it (67P crosses its axis away from vertices).
        std::string raw("ALBMSH1\0",8);auto word=[&](std::uint32_t w){for(int k=0;k<4;++k)raw.push_back(char(w>>(8*k)&255));};
        std::vector<float> xyz;for(int i=0;i<3;++i){const double lon=i*2*std::numbers::pi/3;xyz.insert(xyz.end(),{float(15*std::cos(lon)),float(15*std::sin(lon)),1500.f});}
        xyz.insert(xyz.end(),{0.f,0.f,-1500.f});const std::vector<std::uint32_t> plates{0,1,2,1,0,3,2,1,3,0,2,3};
        word(4);word(4);for(float f:xyz)word(std::bit_cast<std::uint32_t>(f));for(auto plate:plates)word(plate);
        auto model=albert::decodeShape(raw);const auto* uv=model.geometry->getAttribute<float>("uv");const auto* indices=model.geometry->getIndex();
        for(int i=0;i<indices->count();i+=3){float lo=10,hi=-10;for(int j=0;j<3;++j){const float u=uv->getX(indices->getX(i+j));lo=std::min(lo,u);hi=std::max(hi,u);}require(hi-lo<=.50001f,"Plate around the polar axis spans the longitude seam");}
    }
    struct Fixture {const char* id;albert::core::Vector origin,direction,hit;};
    const Fixture fixtures[]{
#include "measured_shape_fixture.inc"
    };
    for(const std::string id:{"phobos","deimos","mimas","phoebe","hyperion","proteus","67p"}){
        const auto raw=pack.payload("v1/"+id+"/shape.bin");auto model=albert::decodeShape(raw);const auto* definition=albert::core::surfaceBody(id);
        require(model.picker->triangleCount()==size_t(id=="phobos"?274874:id=="deimos"?5040:id=="mimas"?786432:id=="phoebe"?196608:id=="hyperion"?29268:id=="proteus"?5040:194642),"Measured plates were lost or fabricated");
        const auto* positions=model.geometry->getAttribute<float>("position"),*normals=model.geometry->getAttribute<float>("normal"),*uv=model.geometry->getAttribute<float>("uv");
        const auto* indices=model.geometry->getIndex();double maximum=0;
        for(int i=0;i<positions->count();++i){maximum=std::max(maximum,std::hypot(double(positions->getX(i)),double(positions->getY(i)),double(positions->getZ(i)))*albert::core::earthRadiusMeters);require(std::abs(std::hypot(normals->getX(i),normals->getY(i),normals->getZ(i))-1)<.001,"Measured mesh has invalid normals");}
        require(std::abs(maximum-definition->shapeRadiusMeters)<.01,"Measured model was rescaled or moved");
        for(int i=0;i<indices->count();i+=3){float lo=10,hi=-10;for(int j=0;j<3;++j){const float u=uv->getX(indices->getX(i+j));lo=std::min(lo,u);hi=std::max(hi,u);}require(hi-lo<=.50001f,"Shape UV crosses the longitude seam");}
        auto render=[](albert::core::Vector p,double scale){return Vector3(float(p.x*scale),float(p.z*scale),float(-p.y*scale));};
        for(const auto& f:fixtures)if(id==f.id){Ray ray(render(f.origin,1/albert::core::earthRadiusMeters),render(f.direction,1));const auto hit=model.picker->raycast(ray);require(bool(hit),"BVH missed an observed plate");require(hit->point.distanceTo(render(f.hit,1/albert::core::earthRadiusMeters))*albert::core::earthRadiusMeters<std::max(.05,2.5e-6*definition->shapeRadiusMeters),"Measured shape differs from independent reference intersections");}
        auto rejects=[&](std::string bytes){bool rejected=false;try{albert::decodeShape(bytes);}catch(const std::exception&){rejected=true;}require(rejected,"Invalid measured geometry accepted");};
        rejects(raw.substr(0,raw.size()-1));auto bad=raw;bad[0]='X';rejects(bad);bad=raw;for(int j=8;j<12;++j)bad[j]=char(255);rejects(bad);bad=raw;bad[16]=0;bad[17]=0;bad[18]=char(192);bad[19]=char(127);rejects(bad);
    }
    std::cout<<"Measured shapes: original physical bounds, normals, UV seam and 168 independent intersections (CSPICE DSKX02, float64 Gaskell/Thomas tables and Stooke grid) OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
