#pragma once
#include "physics/atmosphere.hpp"
#include "physics/earth.hpp"
#include <array>
#include <functional>
#include <vector>
namespace albert::physics::acoustics {
// One-third-octave bands at the exact centres 1000·10^(k/10) Hz, k = -29..12 (1.26 Hz to 15.8 kHz): a big rocket's
// spectrum peaks below 10 Hz, inaudible but in its overall level.
constexpr int bands=42;
double bandCentre(int band);
using Spectrum=std::array<double,bands>; // dB per band.
// ISO 9613-1:1993 pure-tone absorption of air (dB/m): temperature (K), pressure (Pa), molar concentration of water
// vapour (%); molarFromRelative takes the relative humidity (%), molarFromSpecific the specific humidity (kg/kg).
double absorption(double frequency,double temperature,double pressure,double molar);
double molarFromRelative(double relative,double temperature,double pressure);
double molarFromSpecific(double specific);
// Rocket noise by NASA SP-8072 (Eldred 1971): acoustic power η ½ F Ue (η 0.35%, the BYU measurements of Falcon 9, SLS and
// Starship), the normalised spectrum of its Fig. 5 in the Strouhal number f d_e/Ue (d_e: the diameter of one nozzle of the
// cluster's total exit area), and a directivity lobe +4.8 dB at 65° from the plume (−3 dB at ±20°, −14.5 dB elsewhere;
// Kellison and Gee 2023), normalised over the sphere.
struct RocketNoise {double thrust=0,exhaust=0,exitDiameter=1,efficiency=.0035;}; // N, m/s, m.
double soundPower(const RocketNoise& r); // W.
Spectrum powerLevels(const RocketNoise& r,double shift=1); // Band power levels (dB re 1 pW) of the spectrum at f/shift.
double directivity(double plumeAngleDegrees); // dB.
// Free-field band levels (dB re 20 µPa) at distance (m) and angle from the plume, before absorption and ground.
Spectrum freeField(const RocketNoise& r,double distance,double plumeAngleDegrees,double shift=1);
double overall(const Spectrum& levels); // Energy sum (dB).
// Ground reflection for a listener `height` (m) over hard ground on a path of grazing angle `grazing` (rad): +6 dB while the
// image path is shorter than a sixth of the wavelength, +3 dB (incoherent) above; 0 from 100 m up.
Spectrum ground(double height,double grazing,double soundSpeed);
// A rocket's sound at a listener, both in the central body's rotating frame (m, m/s), where the air rests but for its wind.
// history: emissions by time (s; positions linear between samples); plume: unit vector along the exhaust. The emission time
// solves now - t = |listener - source(t)| / c̄ by bisection, c̄ the path's mean sound speed (8 points on the straight path,
// which also sum the absorption); Doppler (c - v_l·n)/(c - v_s·n) with velocities relative to the air at each end, the
// spectrum heard at f taken at f/D. Silent if the path runs underground, the listener's air is below 10 Pa, the source was
// off at the emission time, or nothing emitted yet has arrived. air: the air at a point (humidity unknown: 50% RH).
struct Emission {double time=0;V3 position,velocity,plume;RocketNoise noise;bool on=false;};
struct Heard {Spectrum levels{};double delay=0,doppler=1,distance=0,emitted=0;bool audible=false;};
Heard hear(const std::vector<Emission>& history,double now,V3 listener,V3 listenerVelocity,const Planet& planet,const std::function<Air(const Geodetic&)>& air);
}
