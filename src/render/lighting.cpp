#include "lighting.hpp"
#include "threepp/renderers/shaders/ShaderLib.hpp"
#include <algorithm>
#include <cmath>
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
float albert::lunarAdaptation(double phase,double earthward,double earthPhase,double earthDistance){
    auto moon=[](double a){a=std::abs(a);return std::pow(10.,-.4*(.026*a+4e-9*a*a*a*a))*std::clamp((180-a)/30,0.,1.);}; // Relative to full.
    const double shine=.434*std::pow(10.,.4*earthPhase*(1.060e-3-2.054e-4*earthPhase))/(earthDistance*earthDistance);
    return static_cast<float>(std::clamp(moon(90)/(moon(phase)+shine*moon(earthward)),1.,1000.));
}
float albert::siteNight(float sunHeight){const float t=std::clamp((.052336f-sunHeight)/.122092f,0.f,1.f);return t*t*(3-2*t);}
float albert::earthNightExposure(float sunHeight){return 1+1.4f*siteNight(sunHeight);}
