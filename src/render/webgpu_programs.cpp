#include "webgpu_programs.hpp"
#include "gpu/programs.hpp"
using albert::gpu::Program;using albert::gpu::Uniform;
namespace {
// Points: one quad per vertex; `corner` spans [-1, 1]² and replaces gl_PointCoord.
const char* const quadCorner=R"(
fn quad(corner: u32) -> vec2f { return array<vec2f, 6>(vec2f(-1, -1), vec2f(1, -1), vec2f(1, 1), vec2f(-1, -1), vec2f(1, 1), vec2f(-1, 1))[corner]; }
)";
void stars(){ // visuals.cpp makeStarField: proper motion, per-star size, soft disc, sRGB output (not tone mapped).
    Program p;p.points=true;p.attributes={{"motion",3},{"color",3},{"pointSize",1}};p.uniforms={{"years",Uniform::f32}};
    p.wgsl=std::string(quadCorner)+R"(
struct Material { years: f32 };
@group(2) @binding(0) var<uniform> material: Material;
struct Varyings { @builtin(position) clip: vec4f, @location(0) colour: vec3f, @location(1) corner: vec2f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) motion: vec3f, @location(2) colour: vec3f, @location(3) size: f32, @builtin(vertex_index) i: u32) -> Varyings {
    var o: Varyings; let c = quad(i);
    let clip = frame.projection * object.modelView * vec4f(normalize(position + motion * material.years) * 80000.0, 1.0);
    o.clip = vec4f(clip.xy + c * size / frame.viewport.xy * clip.w, clip.zw); o.colour = colour; o.corner = c;
    return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let alpha = 1.0 - smoothstep(0.2, 1.0, length(v.corner));
    if (alpha < 0.01) { discard; }
    return vec4f(linearToSrgb(v.colour), alpha);
}
)";
    albert::gpu::define("albert:stars",p);
}
}
void albert::defineWebGpuPrograms(){stars();}
