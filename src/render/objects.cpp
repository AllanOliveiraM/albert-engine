#include "objects.hpp"
#include <numbers>
using namespace threepp;
threepp::Vector3 albert::renderPosition(core::Vector p,core::Vector origin){
    p=p-origin;return {static_cast<float>(p.x/core::earthRadiusMeters),static_cast<float>(p.z/core::earthRadiusMeters),static_cast<float>(-p.y/core::earthRadiusMeters)};
}
void albert::SceneObjects::bind(const core::ObjectId& id,std::shared_ptr<Mesh> mesh){nodes_.emplace(id,std::move(mesh));}
void albert::SceneObjects::sync(const core::Scene& registry,double now,core::Vector origin){
    for(auto it=nodes_.begin();it!=nodes_.end();) {
        if(!registry.objects().contains(it->first)){
            auto mesh=it->second;scene_.remove(*mesh);mesh->material()->dispose();it=nodes_.erase(it);
        }else ++it;
    }
    for(const auto& [id,o]:registry.objects()){
        if(!nodes_.contains(id)){
            if(o.builtin)continue;
            auto& geometry=geometries_[o.appearance.shape];
            if(!geometry) switch(o.appearance.shape){
                case core::Shape::Sphere:geometry=SphereGeometry::create(.5f,48,32);break;
                case core::Shape::Box:geometry=BoxGeometry::create(1,1,1);break;
                case core::Shape::Cylinder:geometry=CylinderGeometry::create(.5f,.5f,1.f,48);break;
                case core::Shape::Builtin:continue;
            }
            auto material=MeshStandardMaterial::create();material->color=Color(o.appearance.color);material->roughness=.65f;
            auto mesh=Mesh::create(geometry,material);mesh->castShadow=mesh->receiveShadow=true;
            mesh->scale.set(static_cast<float>(o.appearance.sizeMeters.x/core::earthRadiusMeters),static_cast<float>(o.appearance.sizeMeters.y/core::earthRadiusMeters),static_cast<float>(o.appearance.sizeMeters.z/core::earthRadiusMeters));
            nodes_[id]=mesh;scene_.add(mesh);
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
