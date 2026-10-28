#pragma once
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
namespace albert {
// Static spill is baked into the unused quadrant of the contact atlas. Only the
// four combined pad sources enter the existing, bounded local-light budget.
struct SitePool {threepp::Vector3 centre;float radius;threepp::Color colour;float strength;};
class SiteLighting {
public:
    explicit SiteLighting(const core::Scenery& scenery);
    const std::shared_ptr<threepp::Object3D>& node()const{return root_;}
    const std::vector<SitePool>& pools()const{return pools_;}
    void update(float night,double utc,int quality);
private:
    std::shared_ptr<threepp::Object3D> root_;
    std::shared_ptr<threepp::Mesh> glows_;
    std::shared_ptr<threepp::ShaderMaterial> fixtures_;
    std::vector<std::shared_ptr<threepp::PointLight>> keys_;
    std::vector<SitePool> pools_;
};
}
