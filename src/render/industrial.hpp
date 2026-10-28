#pragma once
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include <vector>
namespace albert {
// Drawn-only launch-site markers. Geometry stays local until Site applies the marker's ENU transform.
using IndustrialPieces=std::vector<std::shared_ptr<threepp::BufferGeometry>>;
bool industrialGeometry(IndustrialPieces& painted,IndustrialPieces& metal,IndustrialPieces& fine,const core::SceneryPart& part);
}
