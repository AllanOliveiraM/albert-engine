#pragma once
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/Color.hpp"
#include <map>
#include <string>
namespace albert {
struct AuxiliaryLight {bool enabled=false;float intensity=.25f;};
using AuxiliaryLights=std::map<std::string,AuxiliaryLight>;
std::shared_ptr<threepp::ShaderMaterial> assistedMaterial(threepp::Color color,float metalness,float roughness,bool flat=false,bool doubleSided=false);
}
