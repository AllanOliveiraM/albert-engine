#pragma once
namespace albert::physics {
struct Air {double temperature=0,pressure=0,density=0,sound=0;}; // K, Pa, kg/m³, m/s.
// U.S. Standard Atmosphere 1976 below 86 km geometric (exact layer equations; the
// lowest layer extends to -5 km). Above 86 km: vacuum until NRLMSIS replaces it.
Air standardAtmosphere(double geometricHeight);
constexpr double standardTop=86000.;
}
