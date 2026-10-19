#pragma once
#include "physics/vector.hpp"
#include <string>
#include <vector>
namespace albert::physics {
// A thermal-protection node (a tile, a panel): where it sits (x m aft of the nose, y right, z down) and its outward normal
// (body axes), its area (m²), heat capacity (J/K: mass × specific heat), emissivity and absorptivity, the conductance to
// the structure behind it (W/K) held at `structure` K, a heating factor on the stagnation-point flux (calibration for its
// shape; 1 at the stagnation point) and the temperature it fails above (K; 0: never).
struct ThermalNode {std::string name;V3 position,normal{1,0,0};double area=1,capacity=1e4,emissivity=.85,absorptivity=.85,conductance=0,structure=300,factor=1,start=300,limit=0;};
// The convective flux a node receives: the stagnation-point flux (Sutton-Graves) times its factor and the modified
// Newtonian distribution cos θ (θ between its outward normal and the oncoming flow; nothing on the lee side).
double nodeHeating(const ThermalNode& n,double stagnation,V3 airBody);
// One step of a node's energy balance, C dT/dt = q A α - ε σ A (T⁴ - T_sky⁴) - G (T - T_structure), implicit in T (a
// Newton solve per sub-step of at most 10 s): stable for any step. sky: the radiative sink (K).
double nodeStep(const ThermalNode& n,double temperature,double flux,double sky,double seconds);
}
