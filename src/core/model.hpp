#pragma once
#include "scene.hpp"
#include <memory>
#include <span>
namespace albert::core {
struct ModelPart {
    std::vector<float> positions,normals;
    std::vector<unsigned> indices;
    std::array<float,3> color{1,1,1};
    float metalness=1,roughness=1;bool doubleSided=false;
};
struct Model {
    std::string id,source;std::size_t sourceBytes=0,vertices=0,triangles=0;
    Vector sizeUnits;double radiusUnits=0;
    std::vector<ModelPart> parts;
};
inline constexpr std::size_t maxModelBytes=768*1024,maxModelVertices=16384,maxModelTriangles=32768;
// A bounded, static, opaque GLB 2 subset. No filesystem or network access.
std::shared_ptr<const Model> decodeModel(std::string id,std::string source,std::span<const unsigned char> bytes);
}
