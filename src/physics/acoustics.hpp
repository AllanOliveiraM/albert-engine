#pragma once
#include "physics/atmosphere.hpp"
#include "physics/earth.hpp"
#include <array>
#include <functional>
#include <vector>
namespace albert::physics::acoustics {
// Include infrasound so a cluster's low-frequency power survives propagation and playback.
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
// The flow noise of a body moving through the air (its turbulent boundary layer and wake; a dipole field, taken
// omnidirectional): acoustic power η P, P the drag power (W), η 4e-5 (M/0.24)³ up to Mach 0.5 and flat above (anchored on an
// airliner's airframe noise on approach, ~1.8 kW for ~40 MW; the shocks' sound is the boom's), its spectrum a hump
// 3x²/(1+x²)^2.5 in x = f/f_p, f_p at a Strouhal number of 0.2 on the body's diameter.
double flowPower(double dragPower,double mach); // W.
Spectrum flowLevels(double power,double speed,double diameter,double shift=1); // Band power levels (dB re 1 pW).
// Band-averaged direct/image interference over hard ground; finite bandwidth washes out distant notches.
// height (m) and grazing (rad) set the image-path excess; reflection fades out between 50 and 100 m up.
Spectrum ground(double height,double grazing,double soundSpeed);
// Publish at the fixed pad position; zero flows are silent. Scales are jet/splash sizes, not the trench width.
struct PadNoise {double ventFlow=0,ventSpeed=300,ventDiameter=.05,waterFlow=0,waterSpeed=20,waterScale=.02;}; // kg/s, m/s, m.
Spectrum padLevels(const PadNoise& pad,double shift=1); // Estimated acoustic efficiencies; band power (dB re 1 pW).
// A rocket's sound at a listener, both in the central body's rotating frame (m, m/s), where the air rests but for its wind.
// history: emissions by time (s; positions linear between samples); plume: unit vector along the exhaust. The emission time
// solves now - t = |listener - source(t)| / c̄ (its latest root) by bisection, c̄ the path's mean sound speed (8 points on the straight path,
// which also sum the absorption); Doppler (c - v_l·n)/(c - v_s·n) with velocities relative to the air at each end, the
// spectrum heard at f taken at f/D. The pressure at the listener for the intensity there by its air's impedance ρc (the
// levels as at sea level's 416.9 rayl: thinner up high). Silent if the path runs underground, the listener's air is below
// 10 Pa, the source was neither burning, venting, deluging nor moving through the air at emission, or nothing emitted yet has
// arrived. air: the air at a point (humidity unknown: 50% RH). Each emission also carries the body's length and diameter
// (m), its drag power (W: the flow noise) and its Mach number, sound speed (m/s) and pressure (Pa) where it is (the booms).
struct Emission {double time=0;V3 position,velocity,plume;RocketNoise noise;bool on=false;double length=0,diameter=0,dragPower=0,mach=0,sound=0,pressure=0,blast=0;PadNoise pad{};}; // blast: TNT kg it exploded with, then.
struct Heard {Spectrum levels{};double delay=0,doppler=1,distance=0,emitted=0,rocket=-1e9;bool audible=false;}; // rocket: the engines' overall level (dB).
Heard hear(const std::vector<Emission>& history,double now,V3 listener,V3 listenerVelocity,const Planet& planet,const std::function<Air(const Geodetic&)>& air);
// The sonic booms that have reached the listener by `now`, oldest first: where the arrival time t + |listener - source(t)| / c̄
// of the supersonic emissions has a local minimum, the Mach cone sweeps over it (c̄ the mean of the sound speeds at the source and
// the listener). Its peak overpressure (Pa) and duration (s) by Carlson's simplified method (NASA TP-1122, 1978):
// Δp = K_p K_r √(p_v p_g) (M²-1)^⅛ h_e^-¾ l^¾ K_s and Δt = K_t 3.42 a_v⁻¹ M (M²-1)^-⅜ h_e^¼ l^¾ K_s, with K_p = K_t = 1,
// K_s 0.1, K_r 2 within 100 m of the ground (1 above), h_e the height between them (at least the length), p_v and a_v the
// source's pressure and sound speed and p_g the listener's. Concorde (Mach 2, 16.5 km, 62 m) gives 109 Pa and 0.38 s against
// the ~93 Pa and ~0.3 s measured. A listener riding inside a cone keeps the count: a new boom is one more of them.
struct Boom {double time=0,overpressure=0,duration=0;};
std::vector<Boom> booms(const std::vector<Emission>& history,double now,V3 listener,const Planet& planet,const std::function<Air(const Geodetic&)>& air);
// A blast's peak overpressure and positive phase (Pa, s) at a distance (m) from `tnt` kg of TNT equivalent in air at that
// pressure: Kinney and Graham (1985), scaled distance Z = r / W^⅓.
Boom blast(double tnt,double distance,double pressure);
// Carlson's peak overpressure and duration for those values (Pa, s; heights in m).
Boom carlson(double mach,double sourcePressure,double sourceSound,double listenerPressure,double height,double length,bool ground);
}
