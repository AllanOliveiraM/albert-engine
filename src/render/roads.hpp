#pragma once
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <map>
#include <string_view>
namespace albert {
std::string_view concreteWgsl(); // Shared by terrain roads and solid concrete; coordinates are metres.
// An exact ground decal. A spatial index selects short lists of analytic capsules and footprint masks;
// the terrain's existing triangles carry it, including their LOD morph, shadows and regional imagery.
class Roads {
public:
    void sync(const core::Scene& scene);
    std::shared_ptr<threepp::Material> material(const std::shared_ptr<threepp::MeshStandardMaterial>& base,int quality);
    std::size_t segments()const{return segments_;}
    unsigned candidates()const{return slots_;}
private:
    void build(const core::Scenery& site);
    std::shared_ptr<const core::Scenery> source_;
    std::shared_ptr<threepp::DataTexture> index_,data_;
    std::map<unsigned,std::shared_ptr<threepp::ShaderMaterial>> materials_;
    threepp::Vector4 origin_,grid_,geo_;
    std::size_t segments_=0;unsigned slots_=0;
};
}
