#pragma once
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include <array>
namespace albert {
// A whole production campus: two silhouette batches and one optional fittings batch.
// LOD distances are evaluated in the site's metres, after its floating-origin transform.
class ProductionSite {
public:
    explicit ProductionSite(const std::vector<core::SceneryBuilding>& buildings);
    const std::shared_ptr<threepp::Object3D>& node()const{return root_;}
    void quality(int value);
    void view(threepp::Camera& camera);
private:
    std::shared_ptr<threepp::Object3D> root_;
    std::array<std::shared_ptr<threepp::Object3D>,2> levels_;
    threepp::Vector3 centre_;bool high_=true;int quality_=1;
    void select();
};
}
