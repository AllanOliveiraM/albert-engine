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
    shadowViewProj: mat4x4f, // World to the sun's shadow clip space (depth 0-1).
    shadow: vec4f,           // Enabled, bias, normal bias, texel size.
};
struct Object { model: mat4x4f, modelView: mat4x4f, normalWorld: mat3x3f, normalView: mat3x3f, flags: vec4f }; // flags.x: receives shadows.
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var shadowMap: texture_depth_2d;
@group(0) @binding(2) var shadowSampler: sampler_comparison;
@group(1) @binding(0) var<uniform> object: Object;
// threepp's PCF directional shadow: normal bias in world space, depth bias, 3x3 comparisons.
fn shadowFactor(world: vec3f, normal: vec3f) -> f32 {
    if (frame.shadow.x < 0.5 || object.flags.x < 0.5) { return 1.0; }
    let p = frame.shadowViewProj * vec4f(world + normal * frame.shadow.z, 1.0);
    let uv = vec2f(p.x * 0.5 + 0.5, 0.5 - p.y * 0.5); let depth = p.z + frame.shadow.y;
    if (any(uv < vec2f(0.0)) || any(uv > vec2f(1.0)) || depth > 1.0) { return 1.0; }
    var sum = 0.0;
    for (var y = -1; y <= 1; y++) { for (var x = -1; x <= 1; x++) { sum += textureSampleCompareLevel(shadowMap, shadowSampler, uv + vec2f(f32(x), f32(y)) * frame.shadow.w, depth); } }
    return sum / 9.0;
}
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
fn finish(colour: vec3f) -> vec3f { // tonemapping (ACES, or none) + encodings.
    if (frame.exposure.y > 0.5) { return linearToSrgb(aces(colour)); }
    return linearToSrgb(max(colour, vec3f(0.0)));
}
)";
const char* const lighting=R"(
const PI_L = 3.141592653589793;
fn sunView() -> vec3f { return normalize((frame.view * vec4f(frame.sun.xyz, 0.0)).xyz); }
fn specularGGX(n: vec3f, l: vec3f, v: vec3f, f0: vec3f, roughness: f32) -> vec3f {
    let alpha = roughness * roughness; let h = normalize(l + v);
    let nl = clamp(dot(n, l), 0.0, 1.0); let nv = clamp(dot(n, v), 0.0, 1.0); let nh = clamp(dot(n, h), 0.0, 1.0); let vh = clamp(dot(l, h), 0.0, 1.0);
    let fresnel = pow(1.0 - vh, 5.0); let f = f0 * (1.0 - fresnel) + fresnel;
    let a2 = alpha * alpha;
    let gv = nl * sqrt(a2 + (1.0 - a2) * nv * nv); let gl = nv * sqrt(a2 + (1.0 - a2) * nl * nl);
    let denom = nh * nh * (a2 - 1.0) + 1.0;
    return f * (0.5 / max(gv + gl, 1e-6)) * (a2 / (PI_L * denom * denom));
}
fn blinnPhong(n: vec3f, l: vec3f, v: vec3f, specular: vec3f, shininess: f32) -> vec3f {
    let h = normalize(l + v); let vh = clamp(dot(v, h), 0.0, 1.0); let fresnel = pow(1.0 - vh, 5.0);
    return (specular * (1.0 - fresnel) + fresnel) * 0.25 * (shininess * 0.5 + 1.0) / PI_L * pow(clamp(dot(n, h), 0.0, 1.0), shininess);
}
// MeshStandardMaterial: direct sun, ambient and extra irradiance (light maps) on a dielectric/metal.
fn standardLight(albedo: vec3f, roughnessIn: f32, metalness: f32, n: vec3f, geometryNormal: vec3f, position: vec3f, extra: vec3f, shadow: f32) -> vec3f {
    let dxy = max(abs(dpdx(geometryNormal)), abs(dpdy(geometryNormal)));
    let roughness = min(max(roughnessIn, 0.0525) + max(max(dxy.x, dxy.y), dxy.z), 1.0);
    let diffuse = albedo * (1.0 - metalness); let f0 = mix(vec3f(0.04), albedo, metalness);
    let l = sunView(); let v = normalize(-position); let nl = clamp(dot(n, l), 0.0, 1.0) * frame.sun.w * shadow;
    return nl * frame.sunColor.rgb * (diffuse / PI_L + specularGGX(n, l, v, f0, roughness)) + (frame.ambient.rgb + extra) * diffuse / PI_L;
}
fn phongLight(albedo: vec3f, specular: vec3f, shininess: f32, n: vec3f, position: vec3f, extra: vec3f, shadow: f32) -> vec3f {
    let l = sunView(); let v = normalize(-position); let nl = clamp(dot(n, l), 0.0, 1.0) * frame.sun.w * shadow;
    return nl * frame.sunColor.rgb * (albedo / PI_L + blinnPhong(n, l, v, specular, shininess)) + (frame.ambient.rgb + extra) * albedo / PI_L;
}
// three.js perturbNormalArb with dHdxy_fwd (invariant under WebGPU's flipped framebuffer y).
fn bumpNormal(position: vec3f, normal: vec3f, dBx: f32, dBy: f32, face: f32) -> vec3f {
    let sigmaX = dpdx(position); let sigmaY = dpdy(position);
    let r1 = cross(sigmaY, normal); let r2 = cross(normal, sigmaX); let det = dot(sigmaX, r1) * face;
    return normalize(abs(det) * normal - sign(det) * (dBx * r1 + dBy * r2));
}
// Bilinear height in f32: hardware filtering quantises the sub-texel position (~1/256), which
// becomes stair steps under derivative bump mapping when a relief texel spans many pixels.
fn heightAt(image: texture_2d<f32>, uv: vec2f) -> f32 {
    let size = vec2i(textureDimensions(image)); let p = uv * vec2f(size) - 0.5; let base = floor(p); let f = p - base;
    let x0 = (i32(base.x) % size.x + size.x) % size.x; let x1 = (x0 + 1) % size.x; // Longitude wraps.
    let y0 = clamp(i32(base.y), 0, size.y - 1); let y1 = clamp(i32(base.y) + 1, 0, size.y - 1);
    let a = textureLoad(image, vec2i(x0, y0), 0).r; let b = textureLoad(image, vec2i(x1, y0), 0).r;
    let c = textureLoad(image, vec2i(x0, y1), 0).r; let d = textureLoad(image, vec2i(x1, y1), 0).r;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
fn flatNormal(position: vec3f) -> vec3f { return normalize(cross(dpdy(position), dpdx(position))); } // GL's cross(dFdx, dFdy) with y flipped.
)";
}
