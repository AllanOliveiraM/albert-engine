#include "lighting.hpp"
#include "threepp/renderers/shaders/ShaderLib.hpp"
#include <stdexcept>
using namespace threepp;
std::shared_ptr<ShaderMaterial> albert::assistedMaterial(Color color,float metalness,float roughness,bool flat,bool doubleSided){
    const auto& shader=shaders::ShaderLib::instance().standard;auto m=ShaderMaterial::create();m->lights=true;m->uniforms=shader.uniforms;m->vertexShader=shader.vertexShader;m->fragmentShader=shader.fragmentShader;
    m->uniforms.at("diffuse").setValue(color);m->uniforms.at("metalness").setValue(metalness);m->uniforms.at("roughness").setValue(roughness);
    m->uniforms.emplace("auxiliaryStrength",Uniform(0.f));m->uniforms.emplace("auxiliarySolarDirection",Uniform(Vector3(1,0,0)));
    m->fragmentShader="uniform float auxiliaryStrength; uniform vec3 auxiliarySolarDirection;\n"+m->fragmentShader;
    const std::string marker="#include <opaque_fragment>";const auto at=m->fragmentShader.find(marker);if(at==std::string::npos)throw std::runtime_error("Unsupported standard shader: no output stage");
    m->fragmentShader.insert(at,"outgoingLight += diffuseColor.rgb * auxiliaryStrength * (1.0-smoothstep(-0.10,0.0,dot(normal,normalize(auxiliarySolarDirection))));\n");
    if(flat)m->defines["FLAT_SHADED"]="";
    if(doubleSided)m->side=Side::Double;
    return m;
}
