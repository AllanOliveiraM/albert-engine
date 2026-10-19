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
    planet: mat4x4f,         // World to the air's planet frame (km from its centre).
    air: vec4f,              // Camera in the planet frame; w: strength (0: no air this frame).
    airSun: vec4f,           // Towards the sun in the planet frame; w: the model's ground offset (km, <= 0).
    weather: vec4f,          // x: cloud layer height over the air's ground (km); y: its opacity (0: none); z: surface wind (m/s, 0: none); w: polar/equatorial axis ratio.
    earth: vec4f,            // The Earth's centre (world); w: its radius (world units), 0: no earthshine.
    ocean: vec4f,            // Wind waves: the pattern's anchor (world); w > 0 when on.
    oceanEast: vec4f,        // East at the anchor (world, unit); w: metres per world unit.
    oceanNorth: vec4f,       // North at the anchor (world, unit).
    oceanSlopes: vec4f,      // Mean square slope in each cascade's band (512, 128, 32 m).
};
struct Object { model: mat4x4f, modelView: mat4x4f, normalWorld: mat3x3f, normalView: mat3x3f, flags: vec4f }; // flags.x: receives shadows.
@group(0) @binding(0) var<uniform> frame: Frame;
@group(0) @binding(1) var shadowMap: texture_depth_2d;
@group(0) @binding(2) var shadowSampler: sampler_comparison;
@group(1) @binding(0) var<uniform> object: Object;
fn viewDirection(position: vec3f) -> vec3f { // Towards the eye from a view-space point: along +z under an orthographic projection.
    if (frame.projection[3][3] > 0.5) { return vec3f(0.0, 0.0, 1.0); }
    return normalize(-position);
}
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
// Bruneton's rendering functions on the frame's tables (GetSkyRadiance, GetSkyRadianceToPoint,
// GetSunAndSkyIrradiance), and their use on world positions: lengths in km in the planet frame.
const char* const air=R"(
@group(0) @binding(3) var<uniform> atmosphere: AtmosphereParameters;
@group(0) @binding(4) var airTransmittance: texture_2d<f32>;
@group(0) @binding(5) var airScattering: texture_3d<f32>;
@group(0) @binding(6) var airMie: texture_3d<f32>;
@group(0) @binding(7) var airIrradiance: texture_2d<f32>;
@group(0) @binding(8) var airSampler: sampler;
@group(0) @binding(9) var airClouds: texture_2d<f32>; // Density in green, u and v as the globe's.
@group(0) @binding(10) var airCloudSampler: sampler;
struct Sky { radiance: vec3f, transmittance: vec3f }; // Radiance relative to the sun's colour.
fn airScatter(r: f32, mu: f32, muS: f32, nu: f32, ground: bool) -> ScatterPair {
    let uvwz = scatteringUvwz(r, mu, muS, nu, ground);
    let x = uvwz.x * f32(SNU - 1u); let tx = floor(x); let l = x - tx;
    let uvw0 = vec3f((tx + uvwz.y) / f32(SNU), uvwz.z, uvwz.w); let uvw1 = vec3f((tx + 1.0 + uvwz.y) / f32(SNU), uvwz.z, uvwz.w);
    let s = textureSampleLevel(airScattering, airSampler, uvw0, 0.0) * (1.0 - l) + textureSampleLevel(airScattering, airSampler, uvw1, 0.0) * l;
    let m = textureSampleLevel(airMie, airSampler, uvw0, 0.0) * (1.0 - l) + textureSampleLevel(airMie, airSampler, uvw1, 0.0) * l;
    return ScatterPair(s.rgb, m.rgb);
}
struct AirRay { camera: vec3f, r: f32, rmu: f32, hit: bool };
fn airEntry(camera: vec3f, ray: vec3f) -> AirRay { // From space: moved to the top of the air, or a miss.
    let r = length(camera); let rmu = dot(camera, ray); let top = topRadius();
    if (r < bottomRadius()) { let c = camera * (bottomRadius() / r); return AirRay(c, bottomRadius(), dot(c, ray), true); }
    if (r <= top) { return AirRay(camera, r, rmu, true); }
    // Far away (the Moon: 384 000 km) the perpendicular distance |c × ray| is better conditioned
    // than rμ² - (r - R)(r + R), two terms of ~1.5e11 km² in f32.
    let h = cross(camera, ray); let h2 = dot(h, h);
    if (rmu >= 0.0 || h2 >= top * top) { return AirRay(camera, r, rmu, false); }
    let half = sqrt(top * top - h2); return AirRay(camera + ray * (-rmu - half), top, -half, true);
}
fn skyRadiance(camera: vec3f, ray: vec3f, sun: vec3f) -> Sky { // Along the whole ray (to the ground or space).
    let e = airEntry(camera, ray); if (!e.hit) { return Sky(vec3f(0.0), vec3f(1.0)); }
    let mu = e.rmu / e.r; let muS = dot(e.camera, sun) / e.r; let nu = dot(ray, sun); let ground = intersectsGround(e.r, mu);
    let s = airScatter(e.r, mu, muS, nu, ground);
    return Sky(s.rayleigh * rayleighPhase(nu) + s.mie * miePhase(nu), select(transmittanceToTop(airTransmittance, airSampler, e.r, mu), vec3f(0.0), ground));
}
// A point inside the air; onGround: the point is on the ground, so a downward ray reaches it there
// (a point on the sphere makes the discriminant ~0, and its sign noise flips the lookups per pixel).
fn skyRadianceToPoint(camera: vec3f, point: vec3f, sun: vec3f, onGround: bool) -> Sky {
    let ray = normalize(point - camera); let e = airEntry(camera, ray); if (!e.hit) { return Sky(vec3f(0.0), vec3f(1.0)); }
    let r = e.r; let mu = e.rmu / r; let muS = dot(e.camera, sun) / r; let nu = dot(ray, sun); let ground0 = bottomRadius();
    let disc = r * mu * r * mu - (r - ground0) * (r + ground0); let ground = mu < 0.0 && (onGround || disc >= 0.0);
    var d = length(point - e.camera); if (ground) { d = min(d, -r * mu - safeSqrt(disc)); } // Not past the model's ground.
    let t = transmittanceAlong(airTransmittance, airSampler, r, mu, d, ground);
    let a = airScatter(r, mu, muS, nu, ground);
    let rp = clampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r)); let b = airScatter(rp, clampCosine((r * mu + d) / rp), clampCosine((r * muS + d * nu) / rp), nu, ground);
    let rayleigh = max(a.rayleigh - t * b.rayleigh, vec3f(0.0)); let mie = max(a.mie - t * b.mie, vec3f(0.0)) * smoothstep(0.0, 0.01, muS); // Bruneton's twilight fix.
    return Sky(rayleigh * rayleighPhase(nu) + mie * miePhase(nu), t);
}
fn airPoint(world: vec3f) -> vec3f { let p = (frame.planet * vec4f(world, 1.0)).xyz; return p * (1.0 - frame.airSun.w / max(length(p), 1.0)); } // Radii raised by the lowered ground.
fn horizonPoint(c: vec3f, toward: vec3f) -> vec3f { // The model sphere's horizon seen from c, in the azimuth of `toward`.
    let b0 = bottomRadius(); let rc = max(length(c), b0 + 0.001); let rho = sqrt((rc - b0) * (rc + b0)); let up = c / length(c);
    let h = toward - dot(toward, up) * up; let hl = max(length(h), 1e-9);
    return up * rc + (h / hl * (b0 / rc) - up * (rho / rc)) * rho;
}
// The cloud map's coordinates over a point (planet frame): its v is geodetic (as its shell's map), tan φ = tan β · a / b
// from the frame's parametric β.
fn cloudUv(p: vec3f) -> vec2f { return vec2f(atan2(p.z, -p.x) / (2.0 * AIR_PI), atan2(p.y, length(p.xz) * frame.weather.w) / AIR_PI + 0.5); }
// Sunlight through the cloud layer over a point (planet frame, radius r): the layer's opacity where the
// ray toward the sun crosses it (1 above it, or without clouds).
fn cloudShadow(p: vec3f, r: f32) -> f32 {
    let top = bottomRadius() + frame.weather.x; let sun = frame.airSun.xyz;
    if (frame.weather.y <= 0.0 || r >= top) { return 1.0; }
    let rmu = dot(p, sun); let crossing = p + sun * (-rmu + sqrt(max(rmu * rmu - (r - top) * (r + top), 0.0)));
    return 1.0 - frame.weather.y * textureSampleLevel(airClouds, airCloudSampler, cloudUv(crossing), 0.0).g;
}
struct AirLight { sun: vec3f, sky: vec3f }; // Sunlight reaching a point (fraction of the sun's colour); sky irradiance (relative to it).
fn airLight(world: vec3f, worldNormal: vec3f) -> AirLight {
    if (frame.air.w <= 0.0) { return AirLight(vec3f(1.0), vec3f(0.0)); }
    let p = airPoint(world); let r = length(p); if (r >= topRadius()) { return AirLight(vec3f(1.0), vec3f(0.0)); }
    let up = p / r; let muS = dot(up, frame.airSun.xyz); let rc = max(r, bottomRadius());
    let n = normalize((frame.planet * vec4f(worldNormal, 0.0)).xyz);
    let sky = lookupIrradiance(airIrradiance, airSampler, rc, muS) * (1.0 + dot(n, up)) * 0.5;
    return AirLight(mix(vec3f(1.0), transmittanceToSun(airTransmittance, airSampler, rc, muS), frame.air.w) * cloudShadow(p, r), sky * frame.air.w);
}
fn skyHitsGround(c: vec3f, ray: vec3f) -> bool { let e = airEntry(c, ray); return e.hit && intersectsGround(e.r, e.rmu / e.r); }
// The air between c and c + segment (planet frame, km). The segment comes from camera-relative world
// coordinates: far from the planet (Mars: ~1e8 km, f32 steps of ~8 km) point - c would collapse.
fn airBetween(c: vec3f, segment: vec3f, sun: vec3f) -> Sky {
    var p = c + segment; let r = length(p); let d = length(segment);
    if (r > topRadius()) { // Beyond the air: the whole ray, if the air lies between.
        if (!(d > 0.0)) { return Sky(vec3f(0.0), vec3f(1.0)); }
        let ray = segment / d; let e = airEntry(c, ray);
        if (!e.hit || (length(c) > topRadius() && length(e.camera - c) >= d)) { return Sky(vec3f(0.0), vec3f(1.0)); }
        // Below the model's horizon from inside the air (the globe mesh's is lower): the horizon's air.
        if (length(c) <= topRadius() && skyHitsGround(c, ray)) { return skyRadianceToPoint(c, horizonPoint(c, ray), sun, true); }
        return skyRadiance(c, ray, sun);
    }
    // Ground below the model's sphere (chords of the globe mesh sag up to ~1 km) breaks the lookups,
    // which assume the ground on it: lift such points onto it. The sagging mesh also shows ground past
    // the sphere's horizon, where the lifted point would get less air than the horizon: use the
    // horizon point in that azimuth.
    let onGround = r < bottomRadius() + 0.05;
    if (onGround) {
        let b0 = bottomRadius(); p = p * (b0 / min(r, b0)); let rc = max(length(c), b0 + 0.001);
        if (length(p - c) > sqrt((rc - b0) * (rc + b0))) { p = horizonPoint(c, p - c); }
    }
    if (!(length(p - c) > 0.0)) { return Sky(vec3f(0.0), vec3f(1.0)); } // Within an f32 step of the camera (~0.5 m at the ground).
    return skyRadianceToPoint(c, p, sun, onGround);
}
fn airSky(world: vec3f) -> Sky { return airBetween(frame.air.xyz, (frame.planet * vec4f(world - frame.camera.xyz, 0.0)).xyz, frame.airSun.xyz); } // Camera to a world point.
fn airApply(world: vec3f, colour: vec3f) -> vec3f { // Aerial perspective: an opaque surface seen through the air.
    if (frame.air.w <= 0.0) { return colour; }
    let sky = airSky(world); return mix(colour, colour * sky.transmittance + sky.radiance * frame.sunColor.rgb, frame.air.w);
}
// Sky light mirrored by a smooth surface (the ocean): the sky along the reflected ray from the point,
// by Schlick's Fresnel with roughness; it fades out on rough ones (land, 0.5-0.8), whose sky light is
// the diffuse irradiance. View-space normal and position; f0: reflectance at normal incidence.
fn airReflection(world: vec3f, viewNormal: vec3f, viewPosition: vec3f, roughness: f32, f0: vec3f) -> vec3f {
    if (frame.air.w <= 0.0 || roughness >= 0.8) { return vec3f(0.0); }
    let v = viewDirection(viewPosition); let nv = clamp(dot(viewNormal, v), 0.0, 1.0);
    let toWorld = transpose(mat3x3f(frame.view[0].xyz, frame.view[1].xyz, frame.view[2].xyz)); // The view's rotation inverted.
    var ray = normalize((frame.planet * vec4f(toWorld * reflect(-v, viewNormal), 0.0)).xyz);
    let up = normalize(airPoint(world)); ray = normalize(ray + up * max(0.02 - dot(ray, up), 0.0)); // A wave's back reflects other waves: the sky just over the horizon.
    let f = f0 + (max(vec3f(1.0 - roughness), f0) - f0) * pow(1.0 - nv, 5.0);
    return f * skyRadiance(airPoint(world), ray, frame.airSun.xyz).radiance * frame.sunColor.rgb * (1.0 - smoothstep(0.5, 0.8, roughness)) * frame.air.w;
}
fn airFilter(world: vec3f) -> vec3f { // Transmittance only: light added over what is behind it.
    if (frame.air.w <= 0.0) { return vec3f(1.0); }
    return mix(vec3f(1.0), airSky(world).transmittance, frame.air.w);
}
)";
const char* const photometry=R"(
// Wind waves (gpu::Ocean, bindings 11-13): the cascades' slopes at a world point, each faded out as a pixel comes to cover one
// to two of its texels (`footprint`: metres per pixel; no mipmaps, so its slopes then return to the roughness, as LEAN
// mapping would); the normal (world), the slope variance they resolve there, and the foam where the Jacobian folds the surface.
@group(0) @binding(11) var oceanDerivatives: texture_2d_array<f32>;
@group(0) @binding(12) var oceanDisplacement: texture_2d_array<f32>;
@group(0) @binding(13) var oceanSampler: sampler;
struct Sea { normal: vec3f, resolved: f32, foam: f32 };
fn seaAt(world: vec3f, footprint: f32) -> Sea {
    var s = Sea(vec3f(0.0), 0.0, 0.0); if (frame.ocean.w <= 0.0) { return s; }
    let east = frame.oceanEast.xyz; let north = frame.oceanNorth.xyz; let o = (world - frame.ocean.xyz) * frame.oceanEast.w;
    let p = vec2f(dot(o, east), dot(o, north)); let patches = array<f32, 3>(512.0, 128.0, 32.0); let variance = frame.oceanSlopes;
    var slope = vec2f(0.0); var fold = vec2f(0.0);
    for (var c = 0; c < 3; c++) {
        let weight = 1.0 - smoothstep(1.0, 2.0, footprint * 256.0 / patches[c]); if (weight <= 0.0) { continue; }
        let d = textureSampleLevel(oceanDerivatives, oceanSampler, p / patches[c], c, 0.0);
        slope += weight * d.xy; fold += weight * d.zw; s.resolved += weight * variance[c];
    }
    s.normal = normalize(cross(east, north) - slope.x * east - slope.y * north);
    let jacobian = (1.0 + fold.x) * (1.0 + fold.y); s.foam = 1.0 - smoothstep(0.15, 0.65, jacobian);
    return s;
}
// McEwen's lunar-Lambert photometry at 0.56 µm (A precise lunar photometric function, LPSC 27, 841, 1996; the disk
// term's factor 2 as in USGS ISIS): I/F = R30 X(i, e, α) / X(30°, 0°, 30°) F(α) / F(30°), R30 the reflectance at
// i = 30°, e = 0, α = 30°. mu0, mu: cosines of incidence and emission; alpha: phase angle (degrees).
fn lunarDisk(mu0: f32, mu: f32, alpha: f32) -> f32 {
    let l = 1.0 + alpha * (-0.019 + alpha * (2.42e-4 - 1.46e-6 * alpha)); // L(α): no limb darkening at full phase.
    return max(2.0 * l * mu0 / max(mu0 + mu, 1e-4) + (1.0 - l) * mu0, 0.0);
}
fn lunarLobes(alpha: f32, r30: f32) -> f32 { // Shadow hiding (B0 1.78, h 0.048) times two Henyey-Greenstein lobes.
    let a = radians(alpha); let c = cos(a); let g1 = 0.14 * r30 - 0.262; let g2 = 0.44;
    let p = 0.45 * (1.0 - g1 * g1) / pow(1.0 + g1 * g1 + 2.0 * g1 * c, 1.5) + 0.55 * (1.0 - g2 * g2) / pow(1.0 + g2 * g2 + 2.0 * g2 * c, 1.5);
    return (1.0 + 1.78 / (1.0 + tan(a * 0.5) / 0.048)) * p;
}
fn lunarPhase(alpha: f32, r30: f32) -> f32 { // F(α), 1 at α = 0: Buratti's linear opposition slope below 3°.
    let slope = -0.07716;
    if (alpha < 3.0) { return 1.0 + slope * alpha; }
    return lunarLobes(alpha, r30) * (1.0 + 3.0 * slope) / lunarLobes(3.0, r30);
}
fn lunarFactor(mu0: f32, mu: f32, alpha: f32, r30: f32) -> f32 { // I/F per unit R30.
    return lunarDisk(mu0, mu, alpha) / 0.90383 * lunarPhase(alpha, r30) / lunarPhase(30.0, r30);
}
// Earthshine: sunlight the Earth reflects, a point source at its centre of geometric albedo 0.434 with Mallama and Hilton's
// phase curve (PASP 130, 2018); beyond 3 Earth radii, fading in from 1.5 (nearer, the air's light stands for it).
// xyz: towards the Earth (view space); w: the irradiance as a fraction of the sun's (0: none).
fn earthLight(world: vec3f) -> vec4f {
    if (frame.earth.w <= 0.0) { return vec4f(0.0); }
    let d = frame.earth.xyz - world; let distance = length(d); let r = distance / frame.earth.w;
    let beta = degrees(acos(clamp(dot(-d / distance, frame.sun.xyz), -1.0, 1.0))); // Sun-Earth-point.
    let k = 0.434 * pow(10.0, 0.4 * beta * (1.060e-3 - 2.054e-4 * beta)) / (r * r) * smoothstep(1.5, 3.0, r) * frame.sun.w;
    return vec4f(normalize((frame.view * vec4f(d, 0.0)).xyz), k);
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
// `sun`: the sun's light reaching the point, as a factor of frame.sunColor (shadow, air).
fn standardLight(albedo: vec3f, roughnessIn: f32, metalness: f32, n: vec3f, geometryNormal: vec3f, position: vec3f, extra: vec3f, sun: vec3f) -> vec3f {
    let dxy = max(abs(dpdx(geometryNormal)), abs(dpdy(geometryNormal)));
    let roughness = min(max(roughnessIn, 0.0525) + max(max(dxy.x, dxy.y), dxy.z), 1.0);
    let diffuse = albedo * (1.0 - metalness); let f0 = mix(vec3f(0.04), albedo, metalness);
    let l = sunView(); let v = viewDirection(position); let nl = clamp(dot(n, l), 0.0, 1.0) * frame.sun.w;
    return nl * frame.sunColor.rgb * sun * (diffuse / PI_L + specularGGX(n, l, v, f0, roughness)) + (frame.ambient.rgb + extra) * diffuse / PI_L;
}
fn phongLight(albedo: vec3f, specular: vec3f, shininess: f32, n: vec3f, position: vec3f, extra: vec3f, sun: vec3f) -> vec3f {
    let l = sunView(); let v = viewDirection(position); let nl = clamp(dot(n, l), 0.0, 1.0) * frame.sun.w;
    return nl * frame.sunColor.rgb * sun * (albedo / PI_L + blinnPhong(n, l, v, specular, shininess)) + (frame.ambient.rgb + extra) * albedo / PI_L;
}
// three.js perturbNormalArb with dHdxy_fwd (invariant under WebGPU's flipped framebuffer y).
fn bumpNormal(position: vec3f, normal: vec3f, dBx: f32, dBy: f32, face: f32) -> vec3f {
    let sigmaX = dpdx(position); let sigmaY = dpdy(position);
    let r1 = cross(sigmaY, normal); let r2 = cross(normal, sigmaX); let det = dot(sigmaX, r1) * face;
    return normalize(abs(det) * normal - sign(det) * (dBx * r1 + dBy * r2));
}
// Bilinear height in f32: hardware filtering quantises the sub-texel position (~1/256), which
// becomes stair steps under derivative bump mapping when a relief texel spans many pixels.
fn heightAt(image: texture_2d<f32>, uv: vec2f, wraps: bool) -> f32 { // wraps: longitude repeats (global maps), else clamps (patches).
    let size = vec2i(textureDimensions(image)); let p = uv * vec2f(size) - 0.5; let base = floor(p); let f = p - base;
    var x0 = (i32(base.x) % size.x + size.x) % size.x; var x1 = (x0 + 1) % size.x;
    if (!wraps) { x0 = clamp(i32(base.x), 0, size.x - 1); x1 = clamp(i32(base.x) + 1, 0, size.x - 1); }
    let y0 = clamp(i32(base.y), 0, size.y - 1); let y1 = clamp(i32(base.y) + 1, 0, size.y - 1);
    let a = textureLoad(image, vec2i(x0, y0), 0).r; let b = textureLoad(image, vec2i(x1, y0), 0).r;
    let c = textureLoad(image, vec2i(x0, y1), 0).r; let d = textureLoad(image, vec2i(x1, y1), 0).r;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// The Moon's surface (materials named "albert:moon"): McEwen's photometry for the sunlight and the earthshine, with
// R30 = 0.866 × the albedo (the Lambert brightness at i = 30°, e = 0); ambient and light maps stay diffuse.
fn lunarLight(albedo: vec3f, n: vec3f, position: vec3f, extra: vec3f, sun: vec3f, earth: vec4f) -> vec3f {
    let v = viewDirection(position); let mu = max(dot(n, v), 1e-3); let r30 = albedo * 0.8660254; let r = dot(r30, vec3f(0.2126, 0.7152, 0.0722));
    let l = sunView(); let direct = lunarFactor(max(dot(n, l), 0.0), mu, degrees(acos(clamp(dot(l, v), -1.0, 1.0))), r) * frame.sun.w;
    let bounce = select(0.0, lunarFactor(max(dot(n, earth.xyz), 0.0), mu, degrees(acos(clamp(dot(earth.xyz, v), -1.0, 1.0))), r) * earth.w, earth.w > 0.0);
    return r30 / PI_L * frame.sunColor.rgb * (direct * sun + bounce) + (frame.ambient.rgb + extra) * albedo / PI_L;
}
fn lambertLight(albedo: vec3f, n: vec3f, extra: vec3f, sun: vec3f) -> vec3f {
    let nl = clamp(dot(n, sunView()), 0.0, 1.0) * frame.sun.w;
    return (nl * frame.sunColor.rgb * sun + frame.ambient.rgb + extra) * albedo / PI_L;
}
fn flatNormal(position: vec3f) -> vec3f { return normalize(cross(dpdy(position), dpdx(position))); } // GL's cross(dFdx, dFdy) with y flipped.
)";
}
