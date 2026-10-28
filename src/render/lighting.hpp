#pragma once
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/math/Color.hpp"
#include <map>
#include <string>
namespace albert {
struct AuxiliaryLight {bool enabled=false;float intensity=.25f;};
using AuxiliaryLights=std::map<std::string,AuxiliaryLight>;
std::shared_ptr<threepp::ShaderMaterial> assistedMaterial(threepp::Color color,float metalness,float roughness,bool flat=false,bool doubleSided=false);
// Dark adaptation at the Moon: the exposure factor (1 to 1000) once less light reaches the camera than from a quarter Moon,
// from Allen's lunar phase law (faded out over the last 30 degrees) and the night side's earthshine (geometric albedo 0.434,
// Mallama and Hilton's phase curve). Degrees: the sun-Moon-camera and Earth-Moon-camera angles and the sun-Earth-Moon angle;
// the Earth-Moon distance in Earth radii.
float lunarAdaptation(double phase,double earthward,double earthPhase,double earthDistance);
// Drawing-only marine boundary layer; fractions 0-1, lengths km, extinction /km.
struct CoastalLight {float extinction=0,scaleKm=.8f,cover=-1,baseKm=.9f,topKm=2.2f;};
float relativeHumidity(double specificHumidity,double temperature,double pressure); // -1 if unknown.
CoastalLight coastalLight(float strength,float humidity=-1,float cloudCover=-1);
float siteNight(float sunHeight); // Sine of the local solar elevation: lamps fade on from +3 to -4 degrees.
float earthNightExposure(float sunHeight); // Bounded photographic adaptation, shared with offscreen captures.
}
