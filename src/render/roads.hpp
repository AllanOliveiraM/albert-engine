#pragma once
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <map>
#include <optional>
#include <array>
#include <string_view>
namespace albert {
std::string_view concreteWgsl(); // Shared by terrain roads and solid concrete; coordinates are metres.
// An exact ground decal. A spatial index selects short lists of analytic capsules and footprint masks;
// the terrain's existing triangles carry it, including their LOD morph, shadows and regional imagery.
class Roads {
public:
    void sync(const core::Scene& scene);
    // Regional imagery is blended on the same surface as the global image, including
    // triangles crossing its boundary. No second mesh or depth mask is required.
    std::shared_ptr<threepp::Material> material(const std::shared_ptr<threepp::MeshStandardMaterial>& base,int quality,
        const std::shared_ptr<threepp::MeshStandardMaterial>& regional=nullptr,
        std::optional<threepp::Vector4> bounds=std::nullopt);
    std::size_t segments()const{return segments_;}
    unsigned candidates()const{return slots_;}
private:
    void build(const core::Scenery& site);
    void cover(int quality);
    std::shared_ptr<const core::Scenery> source_;
    std::shared_ptr<threepp::DataTexture> index_,data_,empty_;
    std::array<std::shared_ptr<threepp::DataTexture>,2> cover_;
    std::map<unsigned,std::shared_ptr<threepp::ShaderMaterial>> materials_;
    // Vector4 defaults w to 1: that would enable an absent cover atlas with
    // zero extent. Keep all inactive coordinates finite on every adapter.
    threepp::Vector4 origin_{0,0,0,0},grid_{0,0,0,0},geo_{1,0,0,0};
    threepp::Vector4 coverGrid_{0,0,1,0};
    std::size_t segments_=0;unsigned slots_=0;
};
}
