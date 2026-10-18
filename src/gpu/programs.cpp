#include "gpu/programs.hpp"
#include <map>
#include <mutex>
namespace albert::gpu {
namespace {
std::mutex lock;
std::map<std::string,Program>& registry(){static std::map<std::string,Program> value;return value;}
}
void define(const std::string& name,Program program){std::lock_guard l(lock);registry()[name]=std::move(program);}
const Program* program(const std::string& name){std::lock_guard l(lock);const auto it=registry().find(name);return it==registry().end()?nullptr:&it->second;}
// Frame and object uniforms; ACES filmic (threepp's, with the 1/0.6 exposure scale) and sRGB.
const char* const prelude=R"(
struct Frame {
    view: mat4x4f, projection: mat4x4f,
    camera: vec4f,   // World position.
    viewport: vec4f, // Width, height (pixels), near plane, 0.
    sun: vec4f,      // Towards the directional light (world); w = 1 when present.
    sunColor: vec4f, ambient: vec4f,
    exposure: vec4f,
};
struct Object { model: mat4x4f, modelView: mat4x4f, normalWorld: mat3x3f, normalView: mat3x3f };
@group(0) @binding(0) var<uniform> frame: Frame;
@group(1) @binding(0) var<uniform> object: Object;
fn srgbToLinear(c: vec3f) -> vec3f { return select(pow((c + 0.055) / 1.055, vec3f(2.4)), c / 12.92, c <= vec3f(0.04045)); }
fn linearToSrgb(c: vec3f) -> vec3f { return select(pow(c, vec3f(0.41666)) * 1.055 - 0.055, c * 12.92, c <= vec3f(0.0031308)); }
fn aces(colour: vec3f) -> vec3f {
    let input = mat3x3f(vec3f(0.59719, 0.07600, 0.02840), vec3f(0.35458, 0.90834, 0.13383), vec3f(0.04823, 0.01566, 0.83777));
    let output = mat3x3f(vec3f(1.60475, -0.10208, -0.00327), vec3f(-0.53108, 1.10813, -0.07276), vec3f(-0.07367, -0.00605, 1.07602));
    let v = input * (colour * frame.exposure.x / 0.6);
    let a = v * (v + 0.0245786) - 0.000090537;
    let b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(output * (a / b), vec3f(0.0), vec3f(1.0));
}
fn finish(colour: vec3f) -> vec3f { return linearToSrgb(aces(colour)); } // tonemapping + encodings.
)";
}
