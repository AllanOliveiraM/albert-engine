#pragma once
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/utils/BVH.hpp"
#include <memory>
#include <string_view>
namespace albert {
struct ShapeMesh {std::shared_ptr<threepp::BufferGeometry> geometry;std::shared_ptr<threepp::BVH> picker;};
// Compact DSK-derived mesh: float32 metres in the IAU body frame, uint32 plates.
ShapeMesh decodeShape(std::string_view bytes);
}
