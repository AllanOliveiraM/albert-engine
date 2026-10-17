#pragma once
#include "scene.hpp"
#include <optional>
#include <string_view>
namespace albert::core {
// Geocentric ICRF positions in metres from the ERFA analytic models, for instants
// the tabulated ephemerides do not cover yet. Sun (epv00): ~5 km RMS over
// 1900-2100. Moon (moon98): 6 km RMS, 32 km worst over 1950-2100. Planets
// (plan94, maxima over 1800-2100, direction within 1.5 arcmin): about 2000 km
// for Mercury, 3700 km Venus, 30000 km Mars, 300000 km Jupiter, 650000 km Saturn,
// 1.3 million km Uranus and 350000 km Neptune. Other ids return nothing.
std::optional<Vector> approximateGeocentric(std::string_view id,double jdTDB);
}
