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
    std::string_view id;
    double afrhoMetres,referenceAu;      // Afρ at referenceAu
    double inboundSlope,outboundSlope;   // Afρ ∝ r^-slope before and after perihelion
    double onsetAu,fullAu;               // zero beyond onset, the power law inside full
    double grainSpeed;                   // m/s of beta = 0.1 grains at 1 AU; ∝ sqrt(beta/r)
};
const CometActivity* cometActivity(std::string_view id);
std::span<const CometActivity> cometActivities();
double afrho(const CometActivity&,double rAu,bool inbound);
// Halley-Marcus composite dust phase function (Schleicher 2010), 1.0 at 0°.
double dustPhase(double degrees);

inline constexpr double solarGM=1.32712440018e20; // m^3/s^2
struct StateVector {Vector position,velocity;};
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
}
