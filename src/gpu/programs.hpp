#pragma once
#include <string>
#include <utility>
#include <vector>
namespace albert::gpu {
// WGSL counterparts of threepp ShaderMaterials, matched by Material::name. The prelude declares
// the frame (group 0) and object (group 1) uniforms and the colour helpers; group 2 holds the
// material struct `material` (binding 0, fields in `uniforms` order) and each texture of
// `textures` as a texture_2d<f32> at 1 + 2i with its sampler at 2 + 2i. Vertex inputs:
// position at location 0, then `attributes` in order. Points draw one instanced quad per vertex
// (the vertex shader receives @builtin(vertex_index) 0-5 for the corner).
enum class Uniform {f32,vec2,vec3,vec4,mat3,mat4};
struct Program {
    std::string wgsl; // vs_main and fs_main.
    std::vector<std::pair<std::string,Uniform>> uniforms;
    std::vector<std::string> textures;
    std::vector<std::pair<std::string,int>> attributes; // Name and float components.
    bool points=false;
};
void define(const std::string& name,Program program);
const Program* program(const std::string& name);
extern const char* const prelude;
// threepp's physical lighting (r166+ conventions, no legacy π): GGX with correlated Smith,
// Blinn-Phong, Lambert, three.js bump mapping; view space, light from `frame`.
extern const char* const lighting;
}
