#include "shapes.hpp"
#include "asset_pack.hpp"
#include "core/bodies.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace threepp;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    require(argc==2,"Provide the assembled asset library");albert::AssetPack pack(argv[1]);
    struct Fixture {const char* id;albert::core::Vector origin,direction,hit;};
    const Fixture fixtures[]{
#include "measured_shape_fixture.inc"
    };
    for(const std::string id:{"phobos","deimos"}){
        const auto raw=pack.payload("v1/"+id+"/shape.bin");auto model=albert::decodeShape(raw);const auto* definition=albert::core::surfaceBody(id);
        require(model.picker->triangleCount()==size_t(id=="phobos"?274874:5040),"Measured plates were lost or fabricated");
        const auto* positions=model.geometry->getAttribute<float>("position"),*normals=model.geometry->getAttribute<float>("normal"),*uv=model.geometry->getAttribute<float>("uv");
        const auto* indices=model.geometry->getIndex();double maximum=0;
        for(int i=0;i<positions->count();++i){maximum=std::max(maximum,std::hypot(double(positions->getX(i)),double(positions->getY(i)),double(positions->getZ(i)))*albert::core::earthRadiusMeters);require(std::abs(std::hypot(normals->getX(i),normals->getY(i),normals->getZ(i))-1)<.001,"Measured mesh has invalid normals");}
        require(std::abs(maximum-definition->shapeRadiusMeters)<.01,"Measured model was rescaled or moved");
        for(int i=0;i<indices->count();i+=3){float lo=10,hi=-10;for(int j=0;j<3;++j){const float u=uv->getX(indices->getX(i+j));lo=std::min(lo,u);hi=std::max(hi,u);}require(hi-lo<=.50001f,"Shape UV crosses the longitude seam");}
        auto render=[](albert::core::Vector p,double scale){return Vector3(float(p.x*scale),float(p.z*scale),float(-p.y*scale));};
        for(const auto& f:fixtures)if(id==f.id){Ray ray(render(f.origin,1/albert::core::earthRadiusMeters),render(f.direction,1));const auto hit=model.picker->raycast(ray);require(bool(hit),"BVH missed an observed plate");require(hit->point.distanceTo(render(f.hit,1/albert::core::earthRadiusMeters))*albert::core::earthRadiusMeters<.05,"Measured shape differs from independent CSPICE DSKX02");}
        auto rejects=[&](std::string bytes){bool rejected=false;try{albert::decodeShape(bytes);}catch(const std::exception&){rejected=true;}require(rejected,"Invalid measured geometry accepted");};
        rejects(raw.substr(0,raw.size()-1));auto bad=raw;bad[0]='X';rejects(bad);bad=raw;for(int j=8;j<12;++j)bad[j]=char(255);rejects(bad);bad=raw;bad[16]=0;bad[17]=0;bad[18]=char(192);bad[19]=char(127);rejects(bad);
    }
    std::cout<<"Measured shapes: original physical bounds, normals, UV seam and 48 independent CSPICE intersections OK\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
