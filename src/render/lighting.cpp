#include "lighting.hpp"
#include "threepp/renderers/shaders/ShaderLib.hpp"
#include <stdexcept>
using namespace threepp;
std::shared_ptr<ShaderMaterial> albert::assistedMaterial(Color color,float metalness,float roughness,bool flat,bool doubleSided){
    const auto& shader=shaders::ShaderLib::instance().standard;auto m=ShaderMaterial::create();m->lights=true;m->uniforms=shader.uniforms;    m->uniforms.at("diffuse").setValue(color);m->uniforms.at("metalness").setValue(metalness);m->uniforms.at("roughness").setValue(roughness);
    m->uniforms.emplace("auxiliaryStrength",Uniform(0.f));m->uniforms.emplace("auxiliarySolarDirection",Uniform(Vector3(1,0,0)));
    if(doubleSided)m->side=Side::Double;
    m->name=flat?"albert:assisted-flat":"albert:assisted"; // WGSL in webgpu_programs.cpp.
    m->uniforms.emplace("doubleSided",Uniform(doubleSided?1.f:0.f));
    return m;
}
