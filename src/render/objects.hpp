#pragma once
#include "core/scene.hpp"
#include "lighting.hpp"
#include "scenery.hpp"
#include "vehicle.hpp"
#include "threepp/threepp.hpp"
#include "threepp/utils/BVH.hpp"
#include <map>
namespace albert {
threepp::Vector3 renderPosition(core::Vector position,core::Vector origin={});
class SceneObjects {
public:
    ~SceneObjects();
    explicit SceneObjects(threepp::Scene& scene):scene_(scene){}
    void bind(const core::ObjectId& id,std::shared_ptr<threepp::Mesh> mesh,std::shared_ptr<const threepp::BVH> picker={});
    void sync(const core::Scene& registry,double utcSeconds,core::Vector origin);
    void updateLighting(threepp::Camera&,const threepp::Vector3& sun,const AuxiliaryLights&,float seconds);
    std::optional<core::ObjectId> pick(const core::Scene&,threepp::Raycaster&) const;
    const std::map<core::ObjectId,std::shared_ptr<threepp::Mesh>>& nodes() const{return nodes_;}
private:
    threepp::Scene& scene_;
    std::map<core::ObjectId,std::shared_ptr<threepp::Mesh>> nodes_;
    std::map<core::ObjectId,std::shared_ptr<const threepp::BVH>> pickers_;
    struct ModelMeshes {std::shared_ptr<const core::Model> source;std::vector<std::shared_ptr<threepp::BufferGeometry>> geometry;std::vector<std::shared_ptr<threepp::Material>> materials;};
    std::map<std::string,ModelMeshes> models_;
    std::map<core::ObjectId,std::uint64_t> instances_;
    std::map<core::ObjectId,float> displayedFill_;
    std::map<core::Shape,std::shared_ptr<threepp::BufferGeometry>> geometries_;
    std::map<core::ObjectId,std::unique_ptr<Vehicle>> vehicles_; // Objects with a look.
    std::map<core::ObjectId,std::unique_ptr<Site>> sites_;       // With scenery.
};
}
