#pragma once
#include "scene.hpp"
#include <span>
namespace albert::core {
// Expensive geometry preparation belongs to the caller/worker, not the owner.
Layer prepareLayer(Layer layer,std::span<const Vector> positions,std::span<const Geographic> geographic={});
Vector earthSurfacePoint(Geographic point);
}
