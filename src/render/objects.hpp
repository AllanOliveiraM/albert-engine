#pragma once
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include <map>
namespace albert {
threepp::Vector3 renderPosition(core::Vector position,core::Vector origin={});
class SceneObjects {
public:
    explicit SceneObjects(threepp::Scene& scene):scene_(scene){}
    void bind(const core::ObjectId& id,std::shared_ptr<threepp::Mesh> mesh);
    void sync(const core::Scene& registry,double utcSeconds,core::Vector origin);
    const std::map<core::ObjectId,std::shared_ptr<threepp::Mesh>>& nodes() const{return nodes_;}
private:
    threepp::Scene& scene_;
    std::map<core::ObjectId,std::shared_ptr<threepp::Mesh>> nodes_;
    std::map<core::Shape,std::shared_ptr<threepp::BufferGeometry>> geometries_;
};
}
