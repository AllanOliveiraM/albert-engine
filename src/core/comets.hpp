#pragma once
#include "local_system.hpp"
#include <functional>
#include <span>
#include <string_view>
#include <vector>
namespace albert::core {
// Measured dust activity of the comets drawn with a coma and a dust tail. Afρ is
// A(0°)fρ (A'Hearn et al. 1984): phase-corrected to 0° with the Halley-Marcus
// function, in a 10^4 km aperture, in metres.
struct CometActivity {
    std::string_view id,designation;     // Scene id and Horizons periodic designation
    double afrhoMetres,referenceAu;      // Afρ at referenceAu
    double inboundSlope,outboundSlope;   // Afρ ∝ r^-slope before and after perihelion
    double onsetAu,fullAu;               // zero beyond onset, the power law inside full
    double grainSpeed;                   // m/s of beta = 0.1 grains at 1 AU; ∝ sqrt(beta/r)
    double water,waterAu,carbonMonoxide; // Q(H2O) per second at waterAu; CO/H2O
};
const CometActivity* cometActivity(std::string_view id);
std::span<const CometActivity> cometActivities();
double afrho(const CometActivity&,double rAu,bool inbound);
// Sunward turnaround of beta = 0.1 grains under radiation pressure: X = v^2/(2 beta g), metres.
double radiationApex(const CometActivity&,double rAu);
// CO+ ions released per second: Q(H2O), scaled with the dust activity, times CO/H2O
// and the share of CO photo-destructions that ionize (6e-7 of 1.1e-6 per second at
// 1 AU: Huebner et al. 1992; Huebner and Mukherjee 2015).
double ionProduction(const CometActivity&,double rAu,bool inbound);
// Halley-Marcus composite dust phase function (Schleicher 2010), 1.0 at 0°.
double dustPhase(double degrees);

inline constexpr double solarGM=1.32712440018e20; // m^3/s^2
struct StateVector {Vector position,velocity;};
// Ion tail: the axis follows the solar wind (400 km/s) seen from the moving nucleus,
// u·r̂ - v (aberration). CO+ accelerates along it, 10 km/s at the nucleus towards
// 260 km/s with a 3e6 km scale (58 km/s at 4-9e5 km, Saito et al. 1987), ionizes over
// the neutral scale length and spreads from a 3e3 km Gaussian width at the nucleus to
// 3.3e4 km within ~1e5 km, then 0.02 x (an opening of about a degree).
inline constexpr double solarWind=4e5; // m/s
Vector ionTailAxis(const StateVector& heliocentric);
double ionSpeed(double metres);        // m/s at distance x along the axis
double ionWidth(double metres);        // Gaussian sigma, metres
// Column density on the axis (ions per m^2), seen perpendicular to the tail.
double ionColumn(const CometActivity&,double rAu,bool inbound,double metres);
// Equivalent I/F of the CO+ comet-tail bands (2,0)+(3,0), 3.06e-14 erg/s per ion at
// 1 AU (Rousselot et al. 2024), against sunlight over a 100 nm blue band (1.9 W/m^2/nm
// at 1 AU); both scale as r^-2, so I/F = 3.06e-14·N/(4·1.9e5) for N in cm^-2.
double ionReflectance(double columnPerSquareMetre);

// Two-body motion under gravitational parameter mu (universal variables): any
// conic, also mu reduced by radiation pressure.
StateVector propagate(const StateVector&,double mu,double seconds);

// Finson-Probstein dust: grains with radiation-pressure ratio beta, released ages
// ago from the nucleus with its velocity, under (1 - beta) solar gravity. Offsets
// are heliocentric ICRF metres from the nucleus now, row-major by beta. Rate is
// the albedo-weighted cross-section released per second and per unit ln(beta)
// (A·m²/s): 2·v·Afρ·w(beta), with w ∝ sqrt(beta) from an s^-3.5 size distribution.
// The nucleus' heliocentric state comes from `comet` (two-body elements, or tables).
struct DustGrid {std::vector<double> betas,ages;std::vector<Vector> offsets;std::vector<double> rate;};
using StateAt=std::function<StateVector(double jdTDB)>;
DustGrid dustGrid(const StateAt& comet,const CometActivity&,double jdTDB,std::span<const double> betas,std::span<const double> ages);
StateAt heliocentricState(const LocalOrbit&); // Two-body elements around the Sun.
// Osculating two-body motion through a heliocentric state at jdTDB.
StateAt osculating(const StateVector& heliocentric,double jdTDB);
// One revolution of the osculating ellipse through the state, evenly spaced in
// eccentric anomaly (dense near perihelion), as TDB dates and states; empty if open.
std::vector<std::pair<double,StateVector>> revolution(const StateVector& heliocentric,double jdTDB,unsigned samples);
}
