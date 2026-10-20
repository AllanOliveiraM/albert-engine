#include "scenery.hpp"
#include "lighting.hpp"
#include "threepp/utils/BufferGeometryUtils.hpp"
#include <algorithm>
#include <cmath>
#include <map>
using namespace threepp;
namespace albert {
namespace {
constexpr float pi=3.14159265358979f;
using Pieces=std::vector<std::shared_ptr<BufferGeometry>>;
Vector3 vec(core::Vector v){return {static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)};}
void bar(Pieces& out,const Vector3& a,const Vector3& b,float t){ // A square bar t thick from a to b.
    Vector3 d=Vector3(b).sub(a);const float l=d.length();if(l<1e-4f)return;
    auto g=BoxGeometry::create(l,t,t);Quaternion q;q.setFromUnitVectors(Vector3(1,0,0),d.normalize());
    Matrix4 m;m.compose(Vector3(a).add(b).multiplyScalar(.5f),q,Vector3(1,1,1));g->applyMatrix4(m);out.push_back(g);
}
std::shared_ptr<BufferGeometry> inside(float r,float h){ // A tube's inner wall: an open cylinder facing its axis.
    auto g=CylinderGeometry::create(r,r,h,48,1,true);for(auto& n:g->getAttribute<float>("normal")->array())n=-n;
    auto& i=g->getIndex()->array();for(std::size_t k=0;k+2<i.size();k+=3)std::swap(i[k+1],i[k+2]);return g;
}
void truss(Pieces& out,float w,float d,float l,float t){ // Four chords along z, and at each bay struts and a zigzag of braces on every face.
    const float x=(w-t)/2,y=(d-t)/2;const Vector3 c[4]{{-x,-y,0},{x,-y,0},{x,y,0},{-x,y,0}};
    const int n=std::max(1,static_cast<int>(std::lround(l/std::max(w,d))));
    auto at=[&](int i,int k){return Vector3(c[i%4]).setZ(-l/2+l*static_cast<float>(k)/static_cast<float>(n));};
    for(int i=0;i<4;++i){bar(out,at(i,0),at(i,n),t);
        for(int k=0;k<=n;++k)bar(out,at(i,k),at(i+1,k),.8f*t);
        for(int k=0;k<n;++k)(k+i)%2?bar(out,at(i,k),at(i+1,k+1),.6f*t):bar(out,at(i+1,k),at(i,k+1),.6f*t);}
}
}
Site::Site(const core::Scenery& s){
    root_=Mesh::create(BufferGeometry::create());root_->frustumCulled=false;
    const std::size_t n=s.bars.size();hinges_.resize(n);
    for(std::size_t k=0;k<n;++k)if(s.bars[k].hinged){auto& h=hinges_[k];h.pivot=Object3D::create();h.pivot->position.copy(vec(s.bars[k].pivot));h.axis=vec(s.bars[k].axis).normalize();root_->add(h.pivot);}
    // By what carries them (0: the ground, k: the k-th structure's pivot) and colour, about that pivot.
    std::map<std::pair<std::size_t,std::uint32_t>,Pieces> by;constexpr std::uint32_t steel=0x1000000; // Past any colour: the bars' own material.
    auto carrier=[&](unsigned structure){return structure>0&&structure<=n&&hinges_[structure-1].pivot?structure:0u;};
    auto relative=[&](std::size_t k,std::shared_ptr<BufferGeometry> g){if(k)g->translate(-hinges_[k-1].pivot->position.x,-hinges_[k-1].pivot->position.y,-hinges_[k-1].pivot->position.z);return g;};
    for(const auto& p:s.parts){
        Pieces local;const float w=static_cast<float>(p.size.x),d=static_cast<float>(p.size.y),l=static_cast<float>(p.size.z);
        switch(p.shape){
        case core::SceneryPart::Shape::Box:local.push_back(BoxGeometry::create(w,d,l));break;
        case core::SceneryPart::Shape::Cylinder:{ // Built round (radius 1) along y, then laid along z and stretched to its diameters.
            const float k=static_cast<float>(p.inner)/w;
            if(k<=0)local.push_back(CylinderGeometry::create(1,1,l,48));
            else{local.push_back(CylinderGeometry::create(1,1,l,48,1,true));local.push_back(inside(k,l));
                for(const float side:{1.f,-1.f}){auto r=RingGeometry::create(k,1,48);r->rotateX(-side*pi/2);r->translate(0,side*l/2,0);local.push_back(r);}}
            for(auto& g:local){g->rotateX(pi/2);g->scale(w/2,d/2,1);}break;}
        case core::SceneryPart::Shape::Truss:truss(local,w,d,l,p.bar>0?static_cast<float>(p.bar):.08f*std::min(w,d));break;
        }
        // Its axes: y toward the heading (clockwise from north), z tilted from up toward y.
        const float h=static_cast<float>(p.heading),t=static_cast<float>(p.tilt);
        const Vector3 ahead(std::sin(h),std::cos(h),0),across(std::cos(h),-std::sin(h),0),up(0,0,1);
        const Vector3 z=Vector3(up).multiplyScalar(std::cos(t)).add(Vector3(ahead).multiplyScalar(std::sin(t))),y=Vector3(ahead).multiplyScalar(std::cos(t)).sub(Vector3(up).multiplyScalar(std::sin(t)));
        Matrix4 m;m.makeBasis(across,y,z);m.setPosition(vec(p.centre));const std::size_t k=carrier(p.structure);
        for(auto& g:local){g->applyMatrix4(m);by[{k,p.color}].push_back(relative(k,std::move(g)));}
    }
    for(std::size_t i=0;i<n;++i){const auto& b=s.bars[i];const Vector3 a=vec(b.from),e=vec(b.to);Vector3 dir=Vector3(e).sub(a);const float l=dir.length();if(l<1e-4f)continue; // Round steel bars.
        auto g=CylinderGeometry::create(static_cast<float>(b.radius),static_cast<float>(b.radius),l,24);Quaternion q;q.setFromUnitVectors(Vector3(0,1,0),dir.normalize());
        Matrix4 m;m.compose(Vector3(a).add(e).multiplyScalar(.5f),q,Vector3(1,1,1));g->applyMatrix4(m);const std::size_t k=carrier(static_cast<unsigned>(i+1));by[{k,steel}].push_back(relative(k,std::move(g)));}
    for(const auto& [key,pieces]:by){
        auto x=Mesh::create(mergeBufferGeometries(pieces),key.second==steel?assistedMaterial(Color(0x9c9c9a),.8f,.35f):assistedMaterial(Color(key.second),.25f,.7f));
        x->castShadow=x->receiveShadow=true;(key.first?hinges_[key.first-1].pivot:root_)->add(x);}
}
void Site::animate(const std::vector<float>* angles){
    for(std::size_t k=0;k<hinges_.size();++k)if(auto& h=hinges_[k];h.pivot)h.pivot->quaternion.setFromAxisAngle(h.axis,angles&&k<angles->size()?(*angles)[k]:0.f);
}
}
