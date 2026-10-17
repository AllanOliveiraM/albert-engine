#include "approximate_ephemeris.hpp"
#include <erfa.h>
#include <cmath>
#include <stdexcept>
#include <utility>
namespace albert::core {
std::optional<Vector> approximateGeocentric(std::string_view id,double tdb){
    if(!std::isfinite(tdb))throw std::invalid_argument("Invalid ephemeris epoch");
    constexpr double au=149597870700.;const double date1=2400000.5,date2=tdb-date1;
    auto metres=[&](const double p[3]){return Vector{p[0]*au,p[1]*au,p[2]*au};};
    double pv[2][3];
    if(id=="moon"){eraMoon98(date1,date2,pv);return metres(pv[0]);} // GCRS.
    // Heliocentric Earth, BCRS axes; outside 1900-2100 the status warns of lower accuracy.
    double heliocentric[2][3],barycentric[2][3];eraEpv00(date1,date2,heliocentric,barycentric);
    const auto earth=metres(heliocentric[0]);
    if(id=="sun")return earth*-1.;
    // Mean equator and equinox of J2000: within 0.03 arcsec of ICRF.
    constexpr std::pair<std::string_view,int> planets[]{{"mercury",1},{"venus",2},{"mars",4},{"jupiter",5},{"saturn",6},{"uranus",7},{"neptune",8}};
    for(const auto& [name,number]:planets)if(id==name){
        const int status=eraPlan94(date1,date2,number,pv); // +1 only warns: outside 1000-3000.
        if(status<0||status==2)throw std::runtime_error("Analytic planetary theory failed");
        return metres(pv[0])-earth;
    }
    return std::nullopt;
}
}
