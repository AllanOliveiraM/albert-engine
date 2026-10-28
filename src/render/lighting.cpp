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
float albert::earthNightExposure(float sunHeight){const float low=1.f-std::clamp((sunHeight-.015f)/.20f,0.f,1.f);return (1.f-.18f*low)+1.58f*siteNight(sunHeight);}

float albert::relativeHumidity(double q,double temperature,double pressure){
    if(!std::isfinite(q)||!std::isfinite(temperature)||!std::isfinite(pressure)||q<0||q>.1||temperature<180||temperature>330||pressure<=0)return -1;
    const double c=temperature-273.15,saturation=611.2*std::exp(17.67*c/(c+243.5));
    return static_cast<float>(std::clamp(q*pressure/(.622+.378*q)/saturation,0.,1.));
}
albert::CoastalLight albert::coastalLight(float strength,float humidity,float cover){
    const float rh=std::isfinite(humidity)&&humidity>=0?std::clamp(humidity,0.f,.98f):.78f;
    CoastalLight light;
    // Hygroscopic sea salt grows in humid air. Cap below saturation; this is haze, not a fog simulator.
    light.extinction=std::clamp(strength,0.f,1.f)*(.018f+.028f*(std::pow(1.f-std::min(rh,.96f),-.55f)-1.f));
    light.cover=std::clamp(strength,0.f,1.f)*(std::isfinite(cover)&&cover>=0?std::clamp(cover,0.f,1.f):.38f);
    light.baseKm=std::clamp(3.4f*(1-rh),.45f,1.5f); // Approximate lifting condensation level of warm marine air.
    light.topKm=light.baseKm+std::lerp(1.5f,.65f,std::clamp((light.cover-.55f)/.4f,0.f,1.f));
    return light;
}
