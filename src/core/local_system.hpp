#pragma once
#include "scene.hpp"
#include <span>
namespace albert::core {
enum class Category {Star,Planet,Moon,Dwarf,Asteroid,Comet,External};
struct LocalOrbit {
    std::string_view id,name,parent;Category category;
    double radius,epoch,a,e,inclination,node,periapsis,meanAnomaly,meanMotion,period;
};
std::span<const LocalOrbit> localOrbits();
const LocalOrbit* localOrbit(std::string_view id);
// Relative to the listed parent, in ICRF/SI. Two-body approximation only.
Sample localState(const LocalOrbit&,double jdTDB);
void includeLocalSystem(Scene&);
void updateLocalSystem(Scene&,double utcSeconds,double jdTDB);
Category category(std::string_view id);
const char* categoryName(Category);
std::string normalizedName(std::string_view);
int matchScore(std::string_view query,std::string_view name,std::string_view id);
double solarOrder(const Scene&,std::string_view id,double utcSeconds);
}
