#pragma once
#include "core/scene.hpp"
#include "site_lighting.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
namespace albert {
// A draw-free scene marker carrying a baked, site-local ambient atlas.
// RGBA stores visibility at 0, 2, 8 and 24 metres; high surfaces fade to clear.
std::shared_ptr<threepp::ShaderMaterial> contactField(const core::Scenery& scenery,std::shared_ptr<threepp::DataTexture>& texture,const std::vector<SitePool>& pools);
}
