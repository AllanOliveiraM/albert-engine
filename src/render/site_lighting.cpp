#include "site_lighting.hpp"
#include "lighting.hpp"
#include "threepp/utils/BufferGeometryUtils.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
SiteLighting::SiteLighting(const core::Scenery& s){
    root_=Object3D::create();root_->name="site night lighting";
    std::vector<std::shared_ptr<BufferGeometry>> fixtures;
    std::vector<float> positions,corners,colours,radii;
    auto glow=[&](Vector3 at,Color c,float radius){for(const Vector2 q:std::array<Vector2,6>{{{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}}}){
        positions.insert(positions.end(),{at.x,at.y,at.z});corners.insert(corners.end(),{q.x,q.y});colours.insert(colours.end(),{c.r,c.g,c.b});radii.push_back(radius);}};
    auto lamp=[&](Vector3 at,Vector3 size,Color c){auto g=BoxGeometry::create(size.x,size.y,size.z)->toNonIndexed();g->deleteAttribute("uv");g->clearGroups();g->translate(at.x,at.y,at.z);
        std::vector<float> ink;for(int i=0;i<g->getAttribute<float>("position")->count();++i)ink.insert(ink.end(),{c.r,c.g,c.b});g->setAttribute("color",FloatBufferAttribute::create(std::move(ink),3));fixtures.push_back(g);glow(at,c,.35f);};
    auto pool=[&](Vector3 at,float radius,Color c,float strength){pools_.push_back({at,radius,c,strength});};
    const Color warm(1.f,.76f,.45f),led(.72f,.85f,1.f),sodium(1.f,.48f,.13f);
    std::vector<Vector3> poles;
    unsigned car=0,farm=0;
    for(const auto& p:s.parts){const Vector3 at(float(p.centre.x),float(p.centre.y),float(p.centre.z));const float sn=std::sin(p.heading),cs=std::cos(p.heading);
        auto local=[&](float x,float y,float z){return Vector3(at).add({cs*x+sn*y,-sn*x+cs*y,z});};
        if(p.name=="pad floodlight pole")poles.push_back(at);
        if(p.name=="pad floodlight"||p.name=="tower floodlight lens")glow(at,warm,.5f);
        if(p.name=="tower aviation beacon")glow(at,Color(1.f,.008f,.001f),.18f);
        if(p.name=="farm light pole"){auto q=local(1.35f,0,float(p.size.z)-.25f);lamp(q,{.58f,.34f,.04f},sodium);pool(q,25,sodium,.35f);}
        if(p.name=="farm canopy")for(float x=-float(p.size.x)/2+6;x<float(p.size.x)/2;x+=18)for(float side:{-1.f,1.f}){
            auto q=local(x,side*float(p.size.y)*.46f,float(p.size.z)-.6f);lamp(q,{.9f,.35f,.12f},led);pool(q,19,led,.32f);}
        if(p.name=="farm manifold"||p.name=="farm vertical tank"||p.name=="farm horizontal tank"){
            if(farm++%3==0){auto q=local(float(p.size.x)/2+.45f,0,5.5f);lamp(q,{.4f,.3f,.18f},sodium);pool(q,17,sodium,.28f);}}
        // Existing shoulder cars are stationary scenery; only a few have their lights on.
        if(p.name=="detail car"&&car++%3==0){for(float x:{-.65f,.65f}){lamp(local(x,2.28f,.78f),{.32f,.08f,.18f},led);lamp(local(x,-2.28f,.78f),{.25f,.08f,.15f},Color(1.f,.015f,.002f));}
            pool(local(0,7,0),6,led,.16f);}
    }
    // Group each pad's four poles, preserving their surveyed/scenery positions.
    while(!poles.empty()){
        const auto first=poles.front();Vector3 centre;unsigned count=0;std::vector<Vector3> remaining;
        for(const auto& p:poles)if(p.distanceTo(first)<180){centre.add(p);++count;pool(p,48,warm,.18f);}else remaining.push_back(p);
        poles=std::move(remaining);centre.multiplyScalar(1.f/count);
        for(float side:{-1.f,1.f}){auto light=PointLight::create(warm,0,240,2);light->position.copy(centre).add({side*42,-18,29});light->name="combined pad floodlights";root_->add(light);keys_.push_back(light);}
    }
    for(const auto& b:s.buildings){using Style=core::SceneryBuilding::Style;if(b.style!=Style::Factory&&b.style!=Style::Bay)continue;
        // Real perimeter edges, including concave factory wings; no bounding-box lamps in courtyards.
        if(b.rings.empty())continue;
        const auto& r=b.rings.front();
        for(std::size_t i=0;i<r.size();++i){const auto a=r[i],e=r[(i+1)%r.size()];const float length=std::hypot(e.x-a.x,e.y-a.y);if(length<18)continue;
            const unsigned n=std::max(1,int(length/45));for(unsigned k=0;k<n;++k){const float t=(k+.5f)/n;
                Vector3 q(float(a.x+(e.x-a.x)*t),float(a.y+(e.y-a.y)*t),float(b.centre.z+8));
                // Offset just outside whichever winding the polygon uses.
                const Vector3 edge(float(e.x-a.x),float(e.y-a.y),0);Vector3 out(edge.y,-edge.x,0);out.normalize();
                const Vector3 toward(float(b.centre.x)-q.x,float(b.centre.y)-q.y,0);if(out.dot(toward)>0)out.negate();q.add(out.multiplyScalar(.3f));
                lamp(q,{.65f,.65f,.24f},led);pool(q,36,led,.24f);}}
    }
    if(!fixtures.empty()){fixtures_=assistedMaterial(Color(0xffffff),0,.8f);fixtures_->name="albert:site-emission";fixtures_->uniforms["night"].setValue(0.f);
        auto mesh=Mesh::create(mergeBufferGeometries(fixtures),fixtures_);mesh->name="night fixture lenses";root_->add(mesh);}
    if(!positions.empty()){auto g=BufferGeometry::create();g->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));g->setAttribute("corner",FloatBufferAttribute::create(std::move(corners),2));
        g->setAttribute("color",FloatBufferAttribute::create(std::move(colours),3));g->setAttribute("radius",FloatBufferAttribute::create(std::move(radii),1));
        auto m=ShaderMaterial::create();m->name="albert:site-glow";m->transparent=true;m->depthWrite=false;m->side=Side::Double;m->blending=Blending::Additive;
        m->uniforms={{"night",Uniform(0.f)},{"seconds",Uniform(0.f)},{"quality",Uniform(1.f)}};glows_=Mesh::create(g,m);glows_->name="site lamp halos";glows_->frustumCulled=false;root_->add(glows_);}
    update(0,0,1);
}
void SiteLighting::update(float night,double utc,int quality){
    for(std::size_t i=0;i<keys_.size();++i){keys_[i]->intensity=night*(quality>0?6500.f:10000.f);keys_[i]->visible=quality>0||i%2==0;}
    if(fixtures_)fixtures_->uniforms.at("night").setValue(night);
    if(glows_){glows_->visible=night>.001f;auto& u=glows_->material()->as<ShaderMaterial>()->uniforms;u.at("night").setValue(night);u.at("seconds").setValue(float(std::fmod(utc,4096.)));u.at("quality").setValue(float(quality));}
}
}
