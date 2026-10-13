#pragma once
#include "objects.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <functional>
namespace albert {
class VectorLayers {
public:
    explicit VectorLayers(threepp::Scene& scene):scene_(scene){}
    ~VectorLayers();
    void update(const core::Scene&,const SceneObjects&,core::Vector origin,threepp::Vector2 viewport,float nearClip,float dt,
        const std::function<void(threepp::ShaderMaterial&)>& earthTerrain);
private:
    struct Visual {core::Layer layer;std::shared_ptr<threepp::Mesh> mesh;std::shared_ptr<threepp::ShaderMaterial> material;float opacity=0;bool geometryReady=false;core::Vector origin;std::uint64_t anchorInstance=0;};
    threepp::Scene& scene_;std::map<std::string,Visual> visuals_;
    void release(Visual&);
};
}
