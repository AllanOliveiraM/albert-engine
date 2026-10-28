#pragma once
#include "threepp/textures/DataTexture.hpp"
#include <memory>
namespace albert {
// Shared mesh vertices in exact data textures: one near draw for all body styles, one for all poses.
// No per-car nodes, texture artwork, contact bodies or shadow passes.
struct CrowdDetail {
    std::shared_ptr<threepp::DataTexture> cars,people;
    unsigned carVertices=0,personVertices=0,carRows=0,personRows=0;
};
CrowdDetail makeCrowdDetail();
}
