#pragma once
#include "threepp/materials/ShaderMaterial.hpp"
#include <memory>

namespace albert {
std::shared_ptr<threepp::ShaderMaterial> orbitMaterial(float horizon);
std::shared_ptr<threepp::ShaderMaterial> borderMaterial();
}
