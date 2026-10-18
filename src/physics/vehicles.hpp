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
}
