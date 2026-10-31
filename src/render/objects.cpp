#include "objects.hpp"
#include "core/model.hpp"
#include "core/bodies.hpp"
#include "animation.hpp"
#include "rocks.hpp"
#include "scenery.hpp"
#include "vehicle.hpp"
#include "terrain.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <tuple>
using namespace threepp;
namespace {
struct BuildTiming {
    const char* kind;const std::string& id;bool enabled=std::getenv("ALBERT_LOAD_STATS")!=nullptr;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    BuildTiming(const char* kind,const std::string& id):kind(kind),id(id){if(enabled)std::fprintf(stderr,"BUILD begin %s %s\n",kind,id.c_str());}
    ~BuildTiming(){if(enabled)std::fprintf(stderr,"BUILD done %s %s %.2fms\n",kind,id.c_str(),std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());}
};
}
threepp::Vector3 albert::renderPosition(core::Vector p,core::Vector origin){
    p=p-origin;return {static_cast<float>(p.x/core::earthRadiusMeters),static_cast<float>(p.z/core::earthRadiusMeters),static_cast<float>(-p.y/core::earthRadiusMeters)};
}
std::array<double,3> albert::renderPositionExact(core::Vector p,core::Vector origin){p=p-origin;return {p.x/core::earthRadiusMeters,p.z/core::earthRadiusMeters,-p.y/core::earthRadiusMeters};}
albert::SceneObjects::~SceneObjects(){for(auto& [id,model]:models_){(void)id;for(auto& g:model.geometry)g->dispose();for(auto& m:model.materials)m->dispose();}}
void albert::SceneObjects::bind(const core::ObjectId& id,std::shared_ptr<Mesh> mesh,std::shared_ptr<const BVH> picker){nodes_.emplace(id,std::move(mesh));if(picker)pickers_[id]=std::move(picker);}
std::optional<albert::core::ObjectId> albert::SceneObjects::pick(const core::Scene& registry,Raycaster& ray,const std::function<bool(const core::ObjectId&)>& only) const {
    std::optional<core::ObjectId> picked;
    float nearest=ray.farPlane;
    for(const auto& [id,node]:nodes_){
        const auto& object=registry.get(id);
        if(!node->visible||!object.visible||(only&&!only(id)))continue;
        float distance=std::numeric_limits<float>::infinity();
        if(const auto picker=pickers_.find(id);picker!=pickers_.end()){
            node->updateWorldMatrix(true,false);Matrix4 inverse;inverse.copy(*node->matrixWorld).invert();const auto localRay=ray.ray.clone().applyMatrix4(inverse);
            if(const auto hit=picker->second->raycast(localRay))distance=hit->point.clone().applyMatrix4(*node->matrixWorld).distanceTo(ray.ray.origin);
        }else if(object.appearance.shape==core::Shape::Builtin||core::surfaceBody(id)){
            // Built-in globes are ellipsoids. Test in local space to retain
            // oblateness without walking every terrain triangle on hover. Height maps
            // displace the surface in the shader (Vesta: up to 293 km over 262.7 km),
            // so the measured largest radius bounds the test when it is larger.
            node->updateWorldMatrix(true,false);
            if(!node->geometry()->boundingSphere)node->geometry()->computeBoundingSphere();
            Matrix4 inverse;inverse.copy(*node->matrixWorld).invert();
            auto localRay=ray.ray.clone().applyMatrix4(inverse);
            auto bounds=*node->geometry()->boundingSphere;
            if(const auto* surface=core::surfaceBody(id);surface&&surface->shapeRadiusMeters>0)bounds.radius=std::max(bounds.radius,static_cast<float>(surface->shapeRadiusMeters/core::earthRadiusMeters));
            Vector3 hit;localRay.intersectSphere(bounds,hit);
            if(!hit.isNan())distance=hit.applyMatrix4(*node->matrixWorld).distanceTo(ray.ray.origin);
        }else{
            const auto hits=ray.intersectObject(*node,object.appearance.shape==core::Shape::Model||object.look||object.scenery);
            if(!hits.empty())distance=hits.front().distance;
        }
        if(distance>=ray.nearPlane&&distance<nearest){nearest=distance;picked=id;}
    }
    return picked;
}
void albert::SceneObjects::sync(const core::Scene& registry,double now,core::Vector origin){
    for(auto it=nodes_.begin();it!=nodes_.end();) {
        if(!registry.objects().contains(it->first)||(!registry.get(it->first).builtin&&instances_[it->first]!=registry.get(it->first).instance)){
            auto mesh=it->second;if(auto water=water_.find(it->first);water!=water_.end()){scene_.remove(*water->second->node());water_.erase(water);}scene_.remove(*mesh);mesh->traverseType<Mesh>([](Mesh& part){part.material()->dispose();});displayedFill_.erase(it->first);instances_.erase(it->first);pickers_.erase(it->first);vehicles_.erase(it->first);sites_.erase(it->first);it=nodes_.erase(it);
        }else ++it;
    }
    for(auto i=models_.begin();i!=models_.end();)if(!registry.models().contains(i->first)||registry.models().at(i->first)!=i->second.source){for(auto& g:i->second.geometry)g->dispose();for(auto& m:i->second.materials)m->dispose();i=models_.erase(i);}else ++i;
    for(const auto& [id,o]:registry.objects()){
        if(!nodes_.contains(id)){
            // A small body whose measured shape is absent from the library (no solid bound)
            // falls back to its artistic rock instead of disappearing.
            if(o.appearance.shape==core::Shape::Builtin&&!(core::localOrbit(id)&&artisticRock(id)))continue;
            instances_[id]=o.instance;
            if(o.scenery){ // A site's: its parts and structures, in metres.
                BuildTiming timing("site",id);
                auto v=std::make_unique<Site>(*o.scenery);v->quality(quality_);auto mesh=v->node();mesh->scale.setScalar(static_cast<float>(1/core::earthRadiusMeters));nodes_[id]=mesh;scene_.add(mesh);sites_[id]=std::move(v);
            }else if(o.look){ // A vehicle: its body, parts, bells, plumes and jets, in metres.
                BuildTiming timing("vehicle",id);
                auto v=std::make_unique<Vehicle>(*o.look);v->quality(quality_);v->gasQuality(quality_<=0?0:splashQuality_);auto mesh=v->node();mesh->scale.setScalar(static_cast<float>(1/core::earthRadiusMeters));nodes_[id]=mesh;scene_.add(mesh);vehicles_[id]=std::move(v);
            }else if(artisticRock(id)){
                auto mesh=makeArtisticRock(*core::localOrbit(id));
                mesh->scale.setScalar(float(o.radiusMeters*2/core::earthRadiusMeters));nodes_[id]=mesh;scene_.add(mesh);
            }else if(o.appearance.shape==core::Shape::Model){
                auto& asset=models_[o.appearance.modelId];
                if(!asset.source){asset.source=registry.models().at(o.appearance.modelId);
                    for(const auto& part:asset.source->parts){auto geometry=BufferGeometry::create();geometry->setAttribute("position",FloatBufferAttribute::create(part.positions,3));geometry->setIndex(part.indices);
                        if(part.normals.empty())geometry->computeVertexNormals();else geometry->setAttribute("normal",FloatBufferAttribute::create(part.normals,3));
                        auto material=assistedMaterial(Color(part.color[0],part.color[1],part.color[2]),part.metalness,part.roughness,part.normals.empty(),part.doubleSided);
                        asset.geometry.push_back(std::move(geometry));asset.materials.push_back(std::move(material));}}
                auto mesh=Mesh::create(BufferGeometry::create());mesh->frustumCulled=false;
                for(std::size_t i=0;i<asset.geometry.size();++i){auto part=Mesh::create(asset.geometry[i],asset.materials[i]->clone<ShaderMaterial>());part->castShadow=part->receiveShadow=true;mesh->add(part);}
                const float scale=static_cast<float>(o.appearance.metersPerUnit/core::earthRadiusMeters);mesh->scale.setScalar(scale);nodes_[id]=mesh;scene_.add(mesh);
            }else{
            auto& geometry=geometries_[o.appearance.shape];
            if(!geometry) switch(o.appearance.shape){
                case core::Shape::Sphere:geometry=SphereGeometry::create(.5f,48,32);break;
                case core::Shape::Box:geometry=BoxGeometry::create(1,1,1);break;
                case core::Shape::Cylinder:geometry=CylinderGeometry::create(.5f,.5f,1.f,48);break;
                case core::Shape::Builtin:case core::Shape::Model:continue;
            }
            auto material=assistedMaterial(Color(o.appearance.color),0.f,.65f);
            auto mesh=Mesh::create(geometry,material);mesh->castShadow=mesh->receiveShadow=true;
            mesh->scale.set(static_cast<float>(o.appearance.sizeMeters.x/core::earthRadiusMeters),static_cast<float>(o.appearance.sizeMeters.y/core::earthRadiusMeters),static_cast<float>(o.appearance.sizeMeters.z/core::earthRadiusMeters));
            nodes_[id]=mesh;scene_.add(mesh);
            }
        }
        const auto& mesh=nodes_.at(id);const auto state=registry.evaluate(id,now).state;
        mesh->position.copy(renderPosition(state.positionMeters,origin));exact_[id]=renderPositionExact(state.positionMeters,origin);
        if(!o.builtin){
            const auto q=state.attitude.value_or(core::Quaternion{});
            Quaternion basis;basis.setFromAxisAngle(Vector3(1,0,0),-std::numbers::pi_v<float>/2.f);
            basis.multiply(Quaternion(static_cast<float>(q.x),static_cast<float>(q.y),static_cast<float>(q.z),static_cast<float>(q.w)));
            mesh->quaternion.copy(basis);mesh->visible=o.visible;
            if(const auto v=vehicles_.find(id);v!=vehicles_.end()){
                const auto p=state.positionMeters;const double r=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z),w=std::sqrt(weatherCentre_.x*weatherCentre_.x+weatherCentre_.y*weatherCentre_.y+weatherCentre_.z*weatherCentre_.z);
                const auto d=r>0&&w>0?p*(w/r)-weatherCentre_:core::Vector{1e9,0,0};
                v->second->humidity(d.x*d.x+d.y*d.y+d.z*d.z<50e3*50e3?humidity_:-1.f);}
            if(const auto v=sites_.find(id);v!=sites_.end())v->second->animate(state.parts.get());
        }
    }
    // A stage lit over the one it left (hot staging): its plumes end on that one's top and splash there, the brightest close,
    // gone by ~80 m apart.
    std::vector<std::tuple<Vehicle*,Vehicle*,float>> staging;
    for(auto& [id,v]:vehicles_){v->hotstage(0,0,now);float metres=0,glow=0;const auto& a=*nodes_.at(id);
        if(a.visible)for(const auto& [other,w]:vehicles_){const auto& b=*nodes_.at(other);if(other==id||!b.visible)continue;
            Vector3 aft(0,-1,0);aft.applyQuaternion(a.quaternion);const auto& p=exact_.at(id);const auto& q=exact_.at(other);const double m=core::earthRadiusMeters;
            const double dx=(q[0]-p[0])*m,dy=(q[1]-p[1])*m,dz=(q[2]-p[2])*m,along=dx*aft.x+dy*aft.y+dz*aft.z,across=std::sqrt(std::max(0.,dx*dx+dy*dy+dz*dz-along*along));
            const double gap=along-v->length()/2-w->length()/2;
            Vector3 nose(0,1,0);nose.applyQuaternion(b.quaternion);
            if(gap>-2&&gap<80&&across<5+.2*gap&&nose.dot(aft)<-.85f){metres=static_cast<float>(std::max(gap,.3));glow=static_cast<float>(std::clamp(1-(gap-10)/70,0.,1.));staging.emplace_back(w.get(),v.get(),metres);break;}}
        v->impinge(metres,glow);}
    // Resolve both poses before animating: first-frame staging and rewind must not inherit yesterday's obstruction.
    for(auto& [id,v]:vehicles_){const auto state=registry.evaluate(id,now).state;v->animate(state.parts.get(),now,state.positionMeters,state.velocityMetersPerSecond);}
    for(const auto& [receiver,ship,gap]:staging)receiver->hotstage(ship->thrust()*float(ship->engineCount()),gap,now);
    // A pad's trench steams while its flight's engines burn over it ("site" with "flight", "site.<id>" with "flight.<id>").
    const bool rewind=cloudTimed_&&now<cloudUtc_;const float seconds=cloudTimed_&&!rewind?static_cast<float>(now-cloudUtc_):0.f;cloudUtc_=now;cloudTimed_=true;
    // Telemetry explicitly marks water; never infer it from altitude or the terrain colour.
    for(auto& [id,v]:vehicles_){const auto state=registry.evaluate(id,now).state;
        if(!state.water){v->water(Vector4());if(auto w=water_.find(id);w!=water_.end())w->second->reset();continue;}
        const auto& sea=*state.water;const auto& look=*registry.get(id).look;
        auto direction=[](core::Vector a){return Vector3(float(a.x),float(a.z),float(-a.y)).normalize();};
        const Vector3 east=direction(sea.east),north=direction(sea.north),up=Vector3(east).cross(north).normalize();
        Matrix4 basis;basis.makeBasis(east,north,up);Quaternion frame;frame.setFromRotationMatrix(basis);const auto inverse=frame.clone().invert();
        auto& water=water_[id];if(!water){water=std::make_unique<Splashdown>(float(look.length),float(look.diameter*.5));water->quality(quality_<=0?0:splashQuality_);scene_.add(water->node());}
        const auto relative=state.positionMeters-sea.origin;
        Vector3 centre(float(relative.x),float(relative.z),float(-relative.y));centre.applyQuaternion(inverse);
        Vector3 nose(0,1,0);nose.applyQuaternion(nodes_.at(id)->quaternion).applyQuaternion(inverse);
        if(rewind)water->reset();water->update(centre,nose,v->thrust(),sea.contact,now,{float(sea.wind.x),float(sea.wind.y),0},delugeEffects_);
        water->node()->position.copy(renderPosition(sea.origin,origin));water->node()->quaternion.copy(frame);water->node()->scale.setScalar(float(1/core::earthRadiusMeters));
        water->node()->visible=water->node()->visible&&nodes_.at(id)->visible;
        const auto hullUp=Vector3(up).applyQuaternion(nodes_.at(id)->quaternion.clone().invert());
        v->water(Vector4(hullUp.x,hullUp.y,hullUp.z,centre.z));
    }
    std::map<core::ObjectId,float> vents; // Fuelled on its pad: venting boil-off and shedding cryogenic mist, fading as it climbs away (gone by ~170 m).
    for(auto& [id,site]:sites_){const auto v=vehicles_.find("flight"+id.substr(4));float thrust=0,above=1e9f;
        if(v!=vehicles_.end()&&nodes_.at(v->first)->visible){const auto& p=exact_.at(v->first);const auto& o=exact_.at(id);Vector3 up(0,0,1),aft(0,-1,0);up.applyQuaternion(nodes_.at(id)->quaternion);aft.applyQuaternion(nodes_.at(v->first)->quaternion);
            const double m=core::earthRadiusMeters,dx=p[0]-o[0],dy=p[1]-o[1],dz=p[2]-o[2],h=(dx*up.x+dy*up.y+dz*up.z)*m+v->second->length()/2*aft.dot(up);
            if(std::sqrt(dx*dx+dy*dy+dz*dz)*m<2e3){thrust=v->second->thrust();above=static_cast<float>(h);vents[v->first]=std::clamp(1-(above-20)/150,0.f,1.f);}} // Over its pad only (in orbit, beyond the Earth's limb, the height along the pad's up went negative: it vented and steamed there).
        const auto count=count_.find("flight"+id.substr(4));
        site->launchDay(count!=count_.end()&&count->second.launch&&v!=vehicles_.end()?count->second.seconds:1e9,count!=count_.end()&&count->second.scrubbed);
        if(rewind)site->resetExhaust();const auto water=deluge_.find(id);const auto wind=winds_.find(id);
        site->exhaust(thrust,above,seconds,water!=deluge_.end()?water->second:0.f,wind!=winds_.end()?wind->second:Vector3(2,-.8f,0),delugeEffects_);site->venting(now);}
    // The stack's vents shut for tank pressurisation from T-40 s to liftoff (a hold keeps its count time), open again after a scrub.
    for(auto& [id,v]:vehicles_){const auto it=vents.find(id);const auto c=count_.find(id);const bool closed=c!=count_.end()&&c->second.seconds>=-40&&!c->second.scrubbed;
        v->venting(it!=vents.end()?it->second:0.f,now,closed);}
}

void albert::SceneObjects::updateLighting(Camera& camera,const Vector3& sun,const AuxiliaryLights& settings,float seconds){
    camera.updateMatrixWorld();
    for(auto& [id,site]:sites_){
        LaunchCrowd::Ground ground;
        if(terrain_&&earth_){
            site->node()->updateWorldMatrix(true,false);Matrix4 inverse;inverse.copy(*earth_->matrixWorld).invert();Matrix4 local;local.multiplyMatrices(inverse,*site->node()->matrixWorld);
            ground=[this,local](const Vector3& p)->std::optional<float>{
                // Double arithmetic preserves metre offsets before subtracting two Earth-sized radii.
                const auto& e=local.elements;const double x=e[0]*double(p.x)+e[4]*double(p.y)+e[8]*double(p.z)+e[12],
                    y=e[1]*double(p.x)+e[5]*double(p.y)+e[9]*double(p.z)+e[13],z=e[2]*double(p.x)+e[6]*double(p.y)+e[10]*double(p.z)+e[14];
                const auto r=terrain_->ground({float(x),float(y),float(z)});if(!r)return std::nullopt;
                const double length=std::sqrt(x*x+y*y+z*z),dot=x*e[8]+y*e[9]+z*e[10];
                if(!(length>0&&dot>0))return std::nullopt;
                return float(p.z+(*r-length)*length/dot);
            };
        }
        site->view(camera,ground);site->lighting(sun);
    }
    for(const auto& [id,instance]:instances_){(void)instance;const auto configured=settings.find(id);const bool enabled=configured!=settings.end()&&configured->second.enabled;
        if(!enabled&&!displayedFill_.contains(id))continue;
        auto& strength=displayedFill_[id];strength=animateValue(strength,enabled?std::clamp(configured->second.intensity,0.f,1.5f):0.f,seconds);
        const auto& mesh=nodes_.at(id);Vector3 direction=sun;direction.sub(mesh->position);if(direction.lengthSq()<1e-12f)direction.set(1,0,0);direction.transformDirection(camera.matrixWorldInverse);
        mesh->traverseType<Mesh>([&](Mesh& part){if(auto* m=part.material()->as<ShaderMaterial>();m&&m->uniforms.contains("auxiliaryStrength")){m->uniforms.at("auxiliaryStrength").setValue(strength);m->uniforms.at("auxiliarySolarDirection").setValue(direction);}});
        if(strength==0)displayedFill_.erase(id);
    }
}
