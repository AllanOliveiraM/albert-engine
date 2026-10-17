#pragma once
#include "threepp/materials/ShaderMaterial.hpp"
#include <array>
#include <memory>
#include <span>

namespace albert {
std::shared_ptr<threepp::ShaderMaterial> orbitMaterial(float horizon);
struct PathPoint {double day,x,y,z;};
// Days for a sampled path to turn once about `normal` from its first point (angle
// linear between points); 0 when the samples cover less than one turn.
double revolutionDays(std::span<const PathPoint> points,const std::array<double,3>& normal);
// Path time of the present, in days from the path epoch: unchanged within the
// sampled half-width; up to three revolutions beyond it, folded by whole turns,
// so the highlight keeps the orbital phase while new samples load. Farther away
// (precession, period error) the highlight is hidden: 1e7 days.
double pathPresent(double days,double halfWidth,double period);
std::shared_ptr<threepp::ShaderMaterial> borderMaterial();
}
