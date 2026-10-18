#include "eclipses.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
using namespace threepp;
double albert::solarVisibility(core::Vector point,core::Vector sun,double sunRadius,core::Vector occluder,double radius){
    const auto s=sun-point,o=occluder-point;
    const double ds=std::hypot(s.x,s.y,s.z),dO=std::hypot(o.x,o.y,o.z);
    if(ds<=sunRadius||dO<=radius||dO>=ds)return 1.;
    const double a=std::asin(std::clamp(sunRadius/ds,0.,1.)),b=std::asin(std::clamp(radius/dO,0.,1.));
    const core::Vector cross{s.y*o.z-s.z*o.y,s.z*o.x-s.x*o.z,s.x*o.y-s.y*o.x};
    const double d=std::atan2(std::hypot(cross.x,cross.y,cross.z),s.x*o.x+s.y*o.y+s.z*o.z);
    if(d>=a+b)return 1.;
    if(d<=std::abs(a-b))return std::clamp(1.-std::min(a*a,b*b)/(a*a),0.,1.);
    const double u=std::clamp((d*d+a*a-b*b)/(2*d*a),-1.,1.),v=std::clamp((d*d+b*b-a*a)/(2*d*b),-1.,1.);
    const double area=a*a*std::acos(u)+b*b*std::acos(v)-.5*std::sqrt(std::max(0.,(-d+a+b)*(d+a-b)*(d-a+b)*(d+a+b)));
    return std::clamp(1.-area/(std::numbers::pi*a*a),0.,1.);
}
namespace {
albert::core::Vector vector(const Vector3& v){return {v.x,v.y,v.z};}
std::shared_ptr<ShaderMaterial> material(bool lunar){
    auto m=ShaderMaterial::create();m->name="albert:eclipse";m->transparent=true;m->depthWrite=false;m->blending=Blending::Multiply;m->toneMapped=false;
    m->polygonOffset=true;m->polygonOffsetFactor=-1.f;m->polygonOffsetUnits=-1.f;
    m->uniforms={{"sunPosition",Uniform(Vector3())},{"blockerPosition",Uniform(Vector3())},{"blockerRadius",Uniform(lunar?1.f:float(1737.4/6378.137))},
        {"lunar",Uniform(lunar?1.f:0.f)},{"alphaMap",Uniform(static_cast<Texture*>(nullptr))},{"surfaceMap",Uniform(static_cast<Texture*>(nullptr))},
        {"hasAlpha",Uniform(0.f)},{"hasSurface",Uniform(0.f)},{"cutoff",Uniform(.5f)}};
    return m;
}
}
albert::EclipseVisuals::EclipseVisuals(std::shared_ptr<Mesh> earth,std::shared_ptr<Mesh> moon,std::shared_ptr<Mesh> sun):earth_(std::move(earth)),moon_(std::move(moon)),sun_(std::move(sun)){}
albert::EclipseVisuals::~EclipseVisuals(){for(auto& [id,s]:surfaces_){(void)id;if(auto source=s.source.lock())source->remove(*s.overlay);s.overlay->material()->dispose();}}
void albert::EclipseVisuals::update(bool enabled){
    lunarActive_=false;
    for(const auto& root:{earth_,moon_}){
        const bool lunar=root==moon_;const auto& blocker=lunar?earth_:moon_;
        const float bodyRadius=lunar?float(1737.4/6378.137):1.f,blockerRadius=lunar?1.f:float(1737.4/6378.137);
        const bool active=enabled&&solarVisibility(vector(root->position),vector(sun_->position),695700./6378.137,vector(blocker->position),blockerRadius+bodyRadius)<.999999;
        if(lunar)lunarActive_=active;
        std::vector<std::shared_ptr<Mesh>> originals{root};
        for(auto* child:root->children)if(auto* mesh=child->as<Mesh>();mesh&&(mesh->material()->is<MeshPhongMaterial>()||mesh->material()->is<MeshStandardMaterial>()))originals.push_back(std::dynamic_pointer_cast<Mesh>(mesh->shared_from_this()));
        for(const auto& source:originals){
            source->castShadow=false; // finite solar-disk occlusion replaces hard mutual shadow maps
            auto found=surfaces_.find(source->id);
            if(found==surfaces_.end()){
                // Apply transmission before orbit annotations and cloud/city layers.
                auto overlay=Mesh::create(source->geometry(),material(lunar));overlay->renderOrder=0;source->add(overlay);
                found=surfaces_.emplace(source->id,Surface{source,overlay}).first;
            }
            auto& overlay=found->second.overlay;overlay->visible=active&&source->visible;
            if(!overlay->visible)continue;
            auto* m=overlay->material()->as<ShaderMaterial>();
            m->uniforms.at("sunPosition").setValue(sun_->position);m->uniforms.at("blockerPosition").setValue(blocker->position);
            const auto* alpha=dynamic_cast<MaterialWithAlphaMap*>(source->material().get());const auto* map=dynamic_cast<MaterialWithMap*>(source->material().get());
            m->uniforms.at("alphaMap").setValue(alpha?alpha->alphaMap.get():nullptr);m->uniforms.at("hasAlpha").setValue(alpha&&alpha->alphaMap?1.f:0.f);
            m->uniforms.at("surfaceMap").setValue(map?map->map.get():nullptr);m->uniforms.at("hasSurface").setValue(map&&map->map?1.f:0.f);
            m->uniforms.at("cutoff").setValue(source->material()->alphaTest);
        }
    }
    for(auto it=surfaces_.begin();it!=surfaces_.end();)if(it->second.source.expired()){it->second.overlay->material()->dispose();it=surfaces_.erase(it);}else ++it;
}
