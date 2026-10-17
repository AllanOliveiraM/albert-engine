#include "shapes.hpp"
#include "core/scene.hpp"
#include "threepp/core/BufferAttribute.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>
using namespace threepp;
albert::ShapeMesh albert::decodeShape(std::string_view bytes){
    auto reject=[](){throw std::runtime_error("Invalid measured shape payload");};
    if(bytes.size()<16||bytes.substr(0,8)!=std::string_view("ALBMSH1\0",8))reject();
    auto integer=[&](size_t at){const auto* p=reinterpret_cast<const unsigned char*>(bytes.data()+at);return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);};
    const auto vertices=integer(8),faces=integer(12);
    if(vertices<4||vertices>500000||faces<4||faces>1000000||bytes.size()!=16+size_t(vertices)*12+size_t(faces)*12)reject();
    std::vector<float> positions;positions.reserve(size_t(vertices)*3+4096);std::vector<float> uv;uv.reserve(size_t(vertices)*2+4096);
    for(size_t i=0;i<vertices;++i){const float x=std::bit_cast<float>(integer(16+i*12)),y=std::bit_cast<float>(integer(20+i*12)),z=std::bit_cast<float>(integer(24+i*12));
        const double radius=std::hypot(double(x),double(y),double(z));if(!std::isfinite(radius)||radius<.1||radius>1e7)reject();
        positions.insert(positions.end(),{float(x/core::earthRadiusMeters),float(z/core::earthRadiusMeters),float(-y/core::earthRadiusMeters)});
        uv.insert(uv.end(),{float(std::atan2(double(y),double(x))/(2*std::numbers::pi)+.5),float(std::asin(std::clamp(z/radius,-1.,1.))/std::numbers::pi+.5)});
    }
    std::vector<unsigned> indices;indices.reserve(size_t(faces)*3);
    for(size_t i=0;i<size_t(faces)*3;++i){auto v=integer(16+size_t(vertices)*12+i*4);if(v>=vertices)reject();indices.push_back(v);}
    auto geometry=BufferGeometry::create();geometry->setAttribute("position",FloatBufferAttribute::create(positions,3));geometry->setIndex(indices);geometry->computeVertexNormals();
    std::vector<float> normals=geometry->getAttribute<float>("normal")->array();
    // Duplicate only UV seam/pole vertices, retaining source smooth normals and
    // exact plate positions. Interpolating across the seam would smear a face.
    std::map<unsigned,unsigned> shifted;
    for(size_t i=0;i<indices.size();i+=3){
        // Measured poles may sit metres to ~100 m off the axis (Gaskell ICQ face centres): a vertex
        // much closer to the axis than the rest of its plate has an arbitrary longitude.
        float axis[3],lo=1,hi=0;bool pole[3];
        for(int j=0;j<3;++j){const auto k=indices[i+j];axis[j]=std::hypot(positions[3*k],positions[3*k+2]);}
        for(int j=0;j<3;++j){pole[j]=axis[j]<.25f*std::min(axis[(j+1)%3],axis[(j+2)%3]);if(!pole[j]){lo=std::min(lo,uv[2*indices[i+j]]);hi=std::max(hi,uv[2*indices[i+j]]);}}
        if(!pole[0]&&!pole[1]&&!pole[2]){
            // A plate pierced by the polar axis away from its corners (non-convex bodies cross the
            // axis more than twice) spans every longitude; along the axis latitude is +-90 deg, so
            // the whole plate takes one longitude.
            float cross[3];for(int j=0;j<3;++j){const auto a=indices[i+j],b=indices[i+(j+1)%3];cross[j]=positions[3*a]*positions[3*b+2]-positions[3*a+2]*positions[3*b];}
            if((cross[0]>0&&cross[1]>0&&cross[2]>0)||(cross[0]<0&&cross[1]<0&&cross[2]<0)){
                const float u=uv[2*indices[i]];
                for(int j=0;j<3;++j){const unsigned original=indices[i+j];const auto next=static_cast<unsigned>(positions.size()/3);
                    for(int k=0;k<3;++k){const float p=positions[3*original+k],n=normals[3*original+k];positions.push_back(p);normals.push_back(n);}
                    const float v=uv[2*original+1];uv.push_back(u);uv.push_back(v);indices[i+j]=next;}
                continue;
            }
        }
        for(int j=0;j<3;++j){const unsigned original=indices[i+j];const bool shift=!pole[j]&&hi-lo>.5f&&uv[2*original]<.5f;
            if(!shift&&!pole[j])continue;
            if(shift&&shifted.contains(original)){indices[i+j]=shifted.at(original);continue;}
            const auto next=static_cast<unsigned>(positions.size()/3);for(int k=0;k<3;++k){const float p=positions[3*original+k],n=normals[3*original+k];positions.push_back(p);normals.push_back(n);}
            float u=uv[2*original]+(shift?1.f:0.f);
            if(pole[j]){const auto a=indices[i+(j+1)%3],b=indices[i+(j+2)%3];float ua=uv[2*a],ub=uv[2*b];if(std::abs(ua-ub)>.5f){if(ua<.5f)ua+=1;else ub+=1;}u=(ua+ub)*.5f;}
            const float v=uv[2*original+1];uv.push_back(u);uv.push_back(v);indices[i+j]=next;if(!pole[j])shifted[original]=next;
        }
    }
    geometry->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));geometry->setAttribute("normal",FloatBufferAttribute::create(std::move(normals),3));geometry->setAttribute("uv",FloatBufferAttribute::create(std::move(uv),2));geometry->setIndex(std::move(indices));geometry->computeBoundingBox();geometry->computeBoundingSphere();
    auto picker=std::make_shared<BVH>(8,18);picker->build(*geometry);return {std::move(geometry),std::move(picker)};
}
