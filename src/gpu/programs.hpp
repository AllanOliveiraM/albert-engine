#pragma once
#include <string>
#include <utility>
#include <vector>
namespace albert::gpu {
// WGSL counterparts of threepp ShaderMaterials, matched by Material::name. The prelude declares
// the frame (group 0) and object (group 1) uniforms and the colour helpers; group 2 holds the
// material struct `material` (binding 0, fields in `uniforms` order) and each texture of
// `textures` as a texture_2d<f32> at 1 + 2i with its sampler at 2 + 2i. Vertex inputs:
// position at location 0, then `attributes` in order. Points use per-instance attributes and
// instanceVertices procedural vertices (six by default, for a quad).
enum class Uniform {f32,vec2,vec3,vec4,mat3,mat4};
struct Program {
    std::string wgsl; // vs_main and fs_main.
    std::vector<std::pair<std::string,Uniform>> uniforms;
    std::vector<std::string> textures;
    std::vector<std::string> exactTextures; // textureLoad data: unfilterable f32, nearest sampler, no half-float fallback.
    std::vector<std::pair<std::string,int>> attributes; // Name and float components.
    bool points=false;
    unsigned instanceVertices=6; // Procedural instances use the point buffers with a fixed vertex count.
    // Drawn before everything else, without depth and showing back faces only: a shell (the sky)
    // around the camera, or seen from outside, covers its whole disc once and objects overwrite it.
    bool background=false;
};
Program surfaceProgram(); // The standard terrain lighting, reusable by ground decals.
void define(const std::string& name,Program program);
const Program* program(const std::string& name);
extern const char* const prelude;
// The frame's air (group 0, bindings 3-8; needs atmosphereWgsl): sky radiance, aerial perspective
// (airApply), sunlight and sky light at a point (airLight), transmittance (airFilter).
extern const char* const air;
// threepp's physical lighting (r166+ conventions, no legacy π): GGX with correlated Smith,
// Blinn-Phong, Lambert, three.js bump mapping; view space, light from `frame`. Every program's
// module is prelude + atmosphereWgsl + air + lighting + its own WGSL.
extern const char* const lighting;
// Lunar photometry (McEwen's lunar-Lambert) and earthshine (frame.earth); math only, before `lighting` in every module.
extern const char* const photometry;
}
