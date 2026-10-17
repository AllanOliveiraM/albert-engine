#pragma once
#include "core/local_system.hpp"
#include "threepp/objects/Mesh.hpp"
namespace albert {
// Deterministic artistic surfaces, never measured shape models or imagery.
bool artisticRock(std::string_view id);
std::shared_ptr<threepp::Mesh> makeArtisticRock(const core::LocalOrbit& body);
}
