#pragma once
#include "physics/flight.hpp"
namespace albert::physics {
// NASA NESC 6-DOF check cases (NASA/TM-2015-218675), DAVE-ML models converted to SI.
Vehicle nescSphere();   // 1 slug, 6 in cannonball, CD 0.1 (cases 1-10).
Vehicle nescTwoStage(); // Unguided two-stage rocket (case 17).
// Medium two-stage kerosene launcher sized from public Falcon 9 v1.2 figures:
// approximate masses, thrust, Isp and geometry; aerodynamics from slender-body theory
// with Allen-Perkins crossflow. Aerodynamically unstable: needs active control.
Vehicle genericLauncher(double payload=5000.);
// Single-stage lander sized from public Apollo LM descent figures (15 t, 45 kN throttleable to
// 10%, Isp 311 s) with four legs; approximate.
Vehicle genericLander();
// Lunar ascent stage sized from public Apollo LM ascent figures (4.85 t with 2,353 kg of propellant,
// fixed 15.57 kN APS at Isp 311 s, RCS attitude); approximate.
Vehicle genericLunarAscent();
// Two-stage Mars ascent vehicle on the mass budget of the Mars Sample Return MAV studies (~450 kg,
// 0.57 m across, 3 m long) with throttleable liquid engines (8 kN Isp 310 s; 2.5 kN Isp 320 s): ~4.6
// km/s, enough for low Mars orbit from the surface; approximate.
Vehicle genericMarsAscent();
}
