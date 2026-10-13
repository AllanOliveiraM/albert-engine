#pragma once
#include "scene.hpp"
#include <span>
#include <string_view>
namespace albert::core {
struct RotationTerm {double phaseDegrees,rateDegreesPerDay,meridianAmplitude,quadraticPerDay=0,raAmplitude=0,decAmplitude=0;};
struct NaturalBody {
    std::string_view id,name;
    int horizonsId;
    double radiusMeters,orbitHalfDays,orbitStepHours;
    std::array<double,3> poleRA,poleDec,meridian;
    std::span<const RotationTerm> terms;
    double polarRadiusMeters=0;bool cloudSurface=false;
};
// Additional solid bodies. Existing Earth/Moon/Sun keep their specialized models.
std::span<const NaturalBody> naturalBodies();
const NaturalBody* naturalBody(std::string_view id);
struct BodyFrame {Vector prime,east,pole;}; // Body-fixed basis -> geocentric ICRF.
BodyFrame bodyFrame(const NaturalBody& body,double jdTDB);
double surfaceRadius(std::string_view id);
}
