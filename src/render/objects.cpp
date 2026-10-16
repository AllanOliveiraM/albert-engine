#include "objects.hpp"
#include "core/model.hpp"
#include "animation.hpp"
#include <numbers>
using namespace threepp;
threepp::Vector3 albert::renderPosition(core::Vector p,core::Vector origin){
    p=p-origin;return {static_cast<float>(p.x/core::earthRadiusMeters),static_cast<float>(p.z/core::earthRadiusMeters),static_cast<float>(-p.y/core::earthRadiusMeters)};
}
albert::SceneObjects::~SceneObjects(){for(auto& [id,model]:models_){(void)id;for(auto& g:model.geometry)g->dispose();for(auto& m:model.materials)m->dispose();}}
void albert::SceneObjects::bind(const core::ObjectId& id,std::shared_ptr<Mesh> mesh){nodes_.emplace(id,std::move(mesh));}
std::optional<albert::core::ObjectId> albert::SceneObjects::pick(const core::Scene& registry,Raycaster& ray) const {
    std::optional<core::ObjectId> picked;
    float nearest=ray.farPlane;
    for(const auto& [id,node]:nodes_){
        const auto& object=registry.get(id);
        if(!node->visible||!object.visible)continue;
        float distance=std::numeric_limits<float>::infinity();
        if(object.appearance.shape==core::Shape::Builtin){
            // Built-in globes are ellipsoids. Test in local space to retain
            // oblateness without walking every terrain triangle on hover.
            node->updateWorldMatrix(true,false);
            if(!node->geometry()->boundingSphere)node->geometry()->computeBoundingSphere();
            Matrix4 inverse;inverse.copy(*node->matrixWorld).invert();
            auto localRay=ray.ray.clone().applyMatrix4(inverse);
            Vector3 hit;localRay.intersectSphere(*node->geometry()->boundingSphere,hit);
            if(!hit.isNan())distance=hit.applyMatrix4(*node->matrixWorld).distanceTo(ray.ray.origin);
        }else{
            const auto hits=ray.intersectObject(*node,object.appearance.shape==core::Shape::Model);
            if(!hits.empty())distance=hits.front().distance;
        }
        if(distance>=ray.nearPlane&&distance<nearest){nearest=distance;picked=id;}
    }
    return picked;
}
void albert::SceneObjects::sync(const core::Scene& registry,double now,core::Vector origin){
    for(auto it=nodes_.begin();it!=nodes_.end();) {
        if(!registry.objects().contains(it->first)||(!registry.get(it->first).builtin&&instances_[it->first]!=registry.get(it->first).instance)){
            auto mesh=it->second;scene_.remove(*mesh);mesh->traverseType<Mesh>([](Mesh& part){part.material()->dispose();});displayedFill_.erase(it->first);instances_.erase(it->first);it=nodes_.erase(it);
        }else ++it;
    }
    for(auto i=models_.begin();i!=models_.end();)if(!registry.models().contains(i->first)||registry.models().at(i->first)!=i->second.source){for(auto& g:i->second.geometry)g->dispose();for(auto& m:i->second.materials)m->dispose();i=models_.erase(i);}else ++i;
    for(const auto& [id,o]:registry.objects()){
        if(!nodes_.contains(id)){
            if(o.appearance.shape==core::Shape::Builtin)continue;
            instances_[id]=o.instance;
            if(o.appearance.shape==core::Shape::Model){
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
        mesh->position.copy(renderPosition(state.positionMeters,origin));
        if(!o.builtin){
            const auto q=state.attitude.value_or(core::Quaternion{});
            Quaternion basis;basis.setFromAxisAngle(Vector3(1,0,0),-std::numbers::pi_v<float>/2.f);
            basis.multiply(Quaternion(static_cast<float>(q.x),static_cast<float>(q.y),static_cast<float>(q.z),static_cast<float>(q.w)));
            mesh->quaternion.copy(basis);mesh->visible=o.visible;
        }
    }
}

void albert::SceneObjects::updateLighting(Camera& camera,const Vector3& sun,const AuxiliaryLights& settings,float seconds){
    camera.updateMatrixWorld();
    for(const auto& [id,instance]:instances_){(void)instance;const auto configured=settings.find(id);const bool enabled=configured!=settings.end()&&configured->second.enabled;
        if(!enabled&&!displayedFill_.contains(id))continue;
        auto& strength=displayedFill_[id];strength=animateValue(strength,enabled?std::clamp(configured->second.intensity,0.f,1.5f):0.f,seconds);
        const auto& mesh=nodes_.at(id);Vector3 direction=sun;direction.sub(mesh->position);if(direction.lengthSq()<1e-12f)direction.set(1,0,0);direction.transformDirection(camera.matrixWorldInverse);
        mesh->traverseType<Mesh>([&](Mesh& part){if(auto* m=part.material()->as<ShaderMaterial>();m&&m->uniforms.contains("auxiliaryStrength")){m->uniforms.at("auxiliaryStrength").setValue(strength);m->uniforms.at("auxiliarySolarDirection").setValue(direction);}});
        if(strength==0)displayedFill_.erase(id);
    }
}
