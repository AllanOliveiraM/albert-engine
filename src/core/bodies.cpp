#include "bodies.hpp"
#include <cmath>
#include <algorithm>
#include <numbers>
#include <stdexcept>
namespace albert::core {
namespace {
// NASA/NAIF pck00011, IAU 2015 Mercury orientation and physical libration.
constexpr RotationTerm mercuryTerms[]{
    {174.7910857,4.092335,.01067257},{349.5821714,8.184670,-.00112309},
    {164.3732571,12.277005,-.00011040},{339.1643429,16.369340,-.00002539},
    {153.9554286,20.461675,-.00000571}};
#include "planet_frames.inc"
const NaturalBody mercury[]{
    {"mercury","Mercurio",199,2439400.,44.,6.,{281.0103,-.0328},{61.4155,-.0049},{329.5988,6.1385108},mercuryTerms}
};
}
std::span<const NaturalBody> naturalBodies(){static const auto bodies=[](){std::array<NaturalBody,7> value{};value[0]=mercury[0];std::copy(std::begin(additionalBodies),std::end(additionalBodies),value.begin()+1);return value;}();return bodies;}
const NaturalBody* naturalBody(std::string_view id){for(const auto& body:naturalBodies())if(body.id==id)return &body;return nullptr;}
double surfaceRadius(std::string_view id){if(id=="earth")return earthRadiusMeters;if(id=="moon")return 1737400.;if(const auto* body=naturalBody(id))return body->radiusMeters;throw std::invalid_argument("Unknown solid surface");}
BodyFrame bodyFrame(const NaturalBody& body,double jd){
    if(!std::isfinite(jd))throw std::invalid_argument("Invalid orientation epoch");
    constexpr double radians=std::numbers::pi/180.;const double days=jd-2451545.,centuries=days/36525.;
    double ra=body.poleRA[0]+centuries*(body.poleRA[1]+centuries*body.poleRA[2]),dec=body.poleDec[0]+centuries*(body.poleDec[1]+centuries*body.poleDec[2]);
    double w=body.meridian[0]+days*(body.meridian[1]+days*body.meridian[2]);
    for(const auto& term:body.terms){const double angle=std::remainder(term.phaseDegrees+days*(term.rateDegreesPerDay+days*term.quadraticPerDay),360.)*radians;w+=term.meridianAmplitude*std::sin(angle);ra+=term.raAmplitude*std::sin(angle);dec+=term.decAmplitude*std::cos(angle);}
    ra*=radians;dec*=radians;
    w=std::remainder(w,360.)*radians;
    const Vector node{-std::sin(ra),std::cos(ra),0},east{-std::sin(dec)*std::cos(ra),-std::sin(dec)*std::sin(ra),std::cos(dec)};
    return {node*std::cos(w)+east*std::sin(w),node*(-std::sin(w))+east*std::cos(w),{std::cos(dec)*std::cos(ra),std::cos(dec)*std::sin(ra),std::sin(dec)}};
}
}
