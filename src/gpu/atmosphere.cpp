#include "gpu/atmosphere.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace albert::gpu {
// Ported from Bruneton's functions.glsl (Copyright (c) 2017 Eric Bruneton, BSD-3-Clause; see
// THIRD_PARTY.md), names shortened, sizes as in the reference (constants.h).
const char* const atmosphereWgsl=R"(
struct Profile { lower: vec4f, upper: vec4f, constants: vec4f }; // Layers: width, exp term, exp scale, linear term; constant terms.
struct AtmosphereParameters {
    solar: vec4f,         // Solar irradiance; w: sun angular radius.
    rayleigh: vec4f,      // Rayleigh scattering.
    mieScattering: vec4f, // w: cosine of the largest sun zenith angle precomputed.
    mieExtinction: vec4f, // w: bottom radius.
    absorption: vec4f,    // w: top radius.
    groundAlbedo: vec4f,
    mieG: vec4f,          // Mie phase asymmetry per channel.
    rayleighDensity: Profile, mieDensity: Profile, absorptionDensity: Profile,
};
const TW = 256u; const TH = 64u; const SR = 32u; const SMU = 128u; const SMUS = 32u; const SNU = 8u; const IW = 64u; const IH = 16u;
const AIR_PI = 3.14159265358979;
const SCATTER_SAMPLES = 200; // Bruneton uses 50: +14% Mie for upward rays (1.6 km steps against a 1.2 km scale height).
struct ScatterPair { rayleigh: vec3f, mie: vec3f };
struct LutPoint { r: f32, mu: f32, muS: f32, nu: f32, ground: bool };
fn bottomRadius() -> f32 { return atmosphere.mieExtinction.w; }
fn topRadius() -> f32 { return atmosphere.absorption.w; }
fn clampCosine(mu: f32) -> f32 { return clamp(mu, -1.0, 1.0); }
fn clampRadius(r: f32) -> f32 { return clamp(r, bottomRadius(), topRadius()); }
fn safeSqrt(a: f32) -> f32 { return sqrt(max(a, 0.0)); }
// Discriminants as (rμ)² ± (R - r)(R + r): r² - R² in f32 at r ≈ 6378 km loses metres, enough to
// flip ground hits between neighbouring pixels for a camera near the ground.
fn distanceToTop(r: f32, mu: f32) -> f32 { let t = topRadius(); return max(-r * mu + safeSqrt(r * mu * r * mu + (t - r) * (t + r)), 0.0); }
fn distanceToBottom(r: f32, mu: f32) -> f32 { let b = bottomRadius(); return max(-r * mu - safeSqrt(r * mu * r * mu - (r - b) * (r + b)), 0.0); }
fn intersectsGround(r: f32, mu: f32) -> bool { let b = bottomRadius(); return mu < 0.0 && r * mu * r * mu >= (r - b) * (r + b); }
fn distanceToNearest(r: f32, mu: f32, ground: bool) -> f32 { if (ground) { return distanceToBottom(r, mu); } return distanceToTop(r, mu); }
fn layerDensity(layer: vec4f, constant: f32, altitude: f32) -> f32 { return clamp(layer.y * exp(layer.z * altitude) + layer.w * altitude + constant, 0.0, 1.0); }
fn profileDensity(p: Profile, altitude: f32) -> f32 {
    if (altitude < p.lower.x) { return layerDensity(p.lower, p.constants.x, altitude); }
    return layerDensity(p.upper, p.constants.y, altitude);
}
fn texCoord(x: f32, size: u32) -> f32 { return 0.5 / f32(size) + x * (1.0 - 1.0 / f32(size)); }
fn unitRange(u: f32, size: u32) -> f32 { return (u - 0.5 / f32(size)) / (1.0 - 1.0 / f32(size)); }
fn airHorizon() -> f32 { let t = topRadius(); let b = bottomRadius(); return sqrt(t * t - b * b); } // Ground to top, horizontally.
fn transmittanceUv(r: f32, mu: f32) -> vec2f {
    let H = airHorizon(); let b = bottomRadius(); let rho = safeSqrt((r - b) * (r + b));
    let d = distanceToTop(r, mu); let dMin = topRadius() - r; let dMax = rho + H;
    return vec2f(texCoord((d - dMin) / (dMax - dMin), TW), texCoord(rho / H, TH));
}
fn transmittanceToTop(t: texture_2d<f32>, s: sampler, r: f32, mu: f32) -> vec3f { return textureSampleLevel(t, s, transmittanceUv(r, mu), 0.0).rgb; }
fn transmittanceAlong(t: texture_2d<f32>, s: sampler, r: f32, mu: f32, d: f32, ground: bool) -> vec3f {
    let rd = clampRadius(sqrt(d * d + 2.0 * r * mu * d + r * r)); let muD = clampCosine((r * mu + d) / rd);
    // Denominators kept above 0: half-float tables (no float32 filtering) underflow near the horizon.
    if (ground) { return min(transmittanceToTop(t, s, rd, -muD) / max(transmittanceToTop(t, s, r, -mu), vec3f(1e-30)), vec3f(1.0)); }
    return min(transmittanceToTop(t, s, r, mu) / max(transmittanceToTop(t, s, rd, muD), vec3f(1e-30)), vec3f(1.0));
}
fn transmittanceToSun(t: texture_2d<f32>, s: sampler, r: f32, muS: f32) -> vec3f {
    let sinH = bottomRadius() / r; let cosH = -sqrt(max(1.0 - sinH * sinH, 0.0)); let a = atmosphere.solar.w;
    return transmittanceToTop(t, s, r, muS) * smoothstep(-sinH * a, sinH * a, muS - cosH);
}
fn rayleighPhase(nu: f32) -> f32 { return 3.0 / (16.0 * AIR_PI) * (1.0 + nu * nu); }
fn miePhase(nu: f32) -> vec3f { // Cornette-Shanks, per channel.
    let g = atmosphere.mieG.xyz; let k = 3.0 / (8.0 * AIR_PI) * (1.0 - g * g) / (2.0 + g * g);
    return k * (1.0 + nu * nu) / pow(1.0 + g * g - 2.0 * g * nu, vec3f(1.5));
}
fn sunZenithRange() -> f32 { // A of the mu_s mapping.
    let dMin = topRadius() - bottomRadius(); return (distanceToTop(bottomRadius(), atmosphere.mieScattering.w) - dMin) / (airHorizon() - dMin);
}
fn scatteringUvwz(r: f32, mu: f32, muS: f32, nu: f32, ground: bool) -> vec4f {
    let H = airHorizon(); let b = bottomRadius(); let rho = safeSqrt((r - b) * (r + b));
    let rMu = r * mu; let disc = rMu * rMu - (r - b) * (r + b);
    var uMu: f32;
    if (ground) {
        let d = -rMu - safeSqrt(disc); let dMin = r - b; let dMax = rho;
        uMu = 0.5 - 0.5 * texCoord(select((d - dMin) / (dMax - dMin), 0.0, dMax == dMin), SMU / 2u);
    } else {
        let d = -rMu + safeSqrt(disc + H * H); let dMin = topRadius() - r; let dMax = rho + H;
        uMu = 0.5 + 0.5 * texCoord((d - dMin) / (dMax - dMin), SMU / 2u);
    }
    let dMin = topRadius() - b; let a = (distanceToTop(b, muS) - dMin) / (H - dMin); let A = sunZenithRange();
    return vec4f((nu + 1.0) / 2.0, texCoord(max(1.0 - a / A, 0.0) / (1.0 + a), SMUS), uMu, texCoord(rho / H, SR));
}
fn lookupScattering(tex: texture_3d<f32>, s: sampler, r: f32, mu: f32, muS: f32, nu: f32, ground: bool) -> vec3f {
    let uvwz = scatteringUvwz(r, mu, muS, nu, ground);
    let x = uvwz.x * f32(SNU - 1u); let tx = floor(x); let l = x - tx;
    let uvw0 = vec3f((tx + uvwz.y) / f32(SNU), uvwz.z, uvwz.w); let uvw1 = vec3f((tx + 1.0 + uvwz.y) / f32(SNU), uvwz.z, uvwz.w);
    return (textureSampleLevel(tex, s, uvw0, 0.0) * (1.0 - l) + textureSampleLevel(tex, s, uvw1, 0.0) * l).rgb;
}
fn irradianceUv(r: f32, muS: f32) -> vec2f {
    let b = bottomRadius(); return vec2f(texCoord(muS * 0.5 + 0.5, IW), texCoord((r - b) / (topRadius() - b), IH));
}
fn lookupIrradiance(t: texture_2d<f32>, s: sampler, r: f32, muS: f32) -> vec3f { return textureSampleLevel(t, s, irradianceUv(r, muS), 0.0).rgb; }
)";
namespace {
// Precomputation passes (functions.glsl's Compute* and model.cc's shaders). Texels are evaluated at
// their centres; the 3D passes run `job.y`-offset slices of layers; `job.x` is the scattering order.
const char* const precomputeWgsl=R"(
@group(0) @binding(0) var<uniform> atmosphere: AtmosphereParameters;
@group(0) @binding(1) var lut: sampler;
@group(0) @binding(2) var transmittanceIn: texture_2d<f32>;
@group(0) @binding(3) var rayleighIn: texture_3d<f32>;
@group(0) @binding(4) var mieIn: texture_3d<f32>;
@group(0) @binding(5) var multipleIn: texture_3d<f32>;
@group(0) @binding(6) var irradianceIn: texture_2d<f32>;
@group(0) @binding(7) var densityIn: texture_3d<f32>;
@group(0) @binding(8) var scatteringIn: texture_3d<f32>;
@group(0) @binding(9) var irradianceSumIn: texture_2d<f32>;
@group(0) @binding(10) var transmittanceOut: texture_storage_2d<FLAT, write>;
@group(0) @binding(11) var irradianceOut: texture_storage_2d<FLAT, write>;
@group(0) @binding(12) var irradianceSumOut: texture_storage_2d<FLAT, write>;
@group(0) @binding(13) var rayleighOut: texture_storage_3d<rgba16float, write>;
@group(0) @binding(14) var mieOut: texture_storage_3d<rgba16float, write>;
@group(0) @binding(15) var scatteringOut: texture_storage_3d<rgba16float, write>;
@group(0) @binding(16) var densityOut: texture_storage_3d<rgba16float, write>;
@group(0) @binding(17) var<uniform> job: vec4i;
fn opticalLength(p: Profile, r: f32, mu: f32) -> f32 {
    let n = 500; let dx = distanceToTop(r, mu) / f32(n); var sum = 0.0;
    for (var i = 0; i <= n; i++) {
        let d = f32(i) * dx; let ri = sqrt(d * d + 2.0 * r * mu * d + r * r);
        sum += profileDensity(p, ri - bottomRadius()) * select(1.0, 0.5, i == 0 || i == n);
    }
    return sum * dx;
}
fn orderScattering(r: f32, mu: f32, muS: f32, nu: f32, ground: bool, order: i32) -> vec3f {
    if (order == 1) { return lookupScattering(rayleighIn, lut, r, mu, muS, nu, ground) * rayleighPhase(nu) + lookupScattering(mieIn, lut, r, mu, muS, nu, ground) * miePhase(nu); }
    return lookupScattering(multipleIn, lut, r, mu, muS, nu, ground);
}
fn texelPoint(coord: vec3f) -> LutPoint { // GetRMuMuSNuFromScatteringTextureFragCoord.
    let fNu = floor(coord.x / f32(SMUS)); let fMuS = coord.x - fNu * f32(SMUS);
    let uvwz = vec4f(fNu, fMuS, coord.y, coord.z) / vec4f(f32(SNU - 1u), f32(SMUS), f32(SMU), f32(SR));
    let H = airHorizon(); let b = bottomRadius(); let rho = H * unitRange(uvwz.w, SR); let r = sqrt(rho * rho + b * b);
    var mu: f32; var ground: bool;
    if (uvwz.z < 0.5) {
        let dMin = r - b; let dMax = rho; let d = dMin + (dMax - dMin) * unitRange(1.0 - 2.0 * uvwz.z, SMU / 2u);
        mu = select(clampCosine(-(rho * rho + d * d) / (2.0 * r * d)), -1.0, d == 0.0); ground = true;
    } else {
        let dMin = topRadius() - r; let dMax = rho + H; let d = dMin + (dMax - dMin) * unitRange(2.0 * uvwz.z - 1.0, SMU / 2u);
        mu = select(clampCosine((H * H - rho * rho - d * d) / (2.0 * r * d)), 1.0, d == 0.0); ground = false;
    }
    let xMuS = unitRange(uvwz.y, SMUS); let dMin = topRadius() - b; let A = sunZenithRange();
    let a = (A - xMuS * A) / (1.0 + xMuS * A); let d = dMin + min(a, A) * (H - dMin);
    let muS = select(clampCosine((H * H - d * d) / (2.0 * b * d)), 1.0, d == 0.0);
    let k = sqrt((1.0 - mu * mu) * (1.0 - muS * muS));
    return LutPoint(r, mu, muS, clamp(clampCosine(uvwz.x * 2.0 - 1.0), mu * muS - k, mu * muS + k), ground);
}
fn irradiancePoint(id: vec2u) -> vec2f { // (r, mu_s)
    let uv = (vec2f(id) + 0.5) / vec2f(f32(IW), f32(IH)); let b = bottomRadius();
    return vec2f(b + unitRange(uv.y, IH) * (topRadius() - b), clampCosine(2.0 * unitRange(uv.x, IW) - 1.0));
}
fn layerTexel(id: vec3u) -> vec3u { return vec3u(id.xy, id.z + u32(job.y)); }
@compute @workgroup_size(8, 8) fn transmittancePass(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= TW || id.y >= TH) { return; }
    let uv = (vec2f(id.xy) + 0.5) / vec2f(f32(TW), f32(TH));
    let H = airHorizon(); let b = bottomRadius(); let rho = H * unitRange(uv.y, TH); let r = sqrt(rho * rho + b * b);
    let dMin = topRadius() - r; let dMax = rho + H; let d = dMin + unitRange(uv.x, TW) * (dMax - dMin);
    let mu = clampCosine(select((H * H - rho * rho - d * d) / (2.0 * r * d), 1.0, d == 0.0));
    let depth = atmosphere.rayleigh.rgb * opticalLength(atmosphere.rayleighDensity, r, mu) + atmosphere.mieExtinction.rgb * opticalLength(atmosphere.mieDensity, r, mu)
        + atmosphere.absorption.rgb * opticalLength(atmosphere.absorptionDensity, r, mu);
    textureStore(transmittanceOut, id.xy, vec4f(exp(-depth), 1.0));
}
@compute @workgroup_size(8, 8) fn directIrradiancePass(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= IW || id.y >= IH) { return; }
    let p = irradiancePoint(id.xy); let a = atmosphere.solar.w; let muS = p.y;
    let cosine = select(select((muS + a) * (muS + a) / (4.0 * a), muS, muS > a), 0.0, muS < -a); // Over the visible solar disc.
    textureStore(irradianceOut, id.xy, vec4f(atmosphere.solar.rgb * transmittanceToTop(transmittanceIn, lut, p.x, muS) * cosine, 1.0));
    textureStore(irradianceSumOut, id.xy, vec4f(0.0)); // Sky irradiance only: the direct term is not stored.
}
@compute @workgroup_size(8, 8) fn singleScatteringPass(@builtin(global_invocation_id) id: vec3u) {
    let texel = layerTexel(id); if (texel.x >= SNU * SMUS || texel.y >= SMU) { return; }
    let p = texelPoint(vec3f(texel) + 0.5);
    let n = SCATTER_SAMPLES; let dx = distanceToNearest(p.r, p.mu, p.ground) / f32(n); var rayleigh = vec3f(0.0); var mie = vec3f(0.0);
    for (var i = 0; i <= n; i++) {
        let d = f32(i) * dx; let rd = clampRadius(sqrt(d * d + 2.0 * p.r * p.mu * d + p.r * p.r)); let muSd = clampCosine((p.r * p.muS + d * p.nu) / rd);
        let t = transmittanceAlong(transmittanceIn, lut, p.r, p.mu, d, p.ground) * transmittanceToSun(transmittanceIn, lut, rd, muSd) * select(1.0, 0.5, i == 0 || i == n);
        rayleigh += t * profileDensity(atmosphere.rayleighDensity, rd - bottomRadius()); mie += t * profileDensity(atmosphere.mieDensity, rd - bottomRadius());
    }
    rayleigh *= dx * atmosphere.solar.rgb * atmosphere.rayleigh.rgb; mie *= dx * atmosphere.solar.rgb * atmosphere.mieScattering.rgb;
    textureStore(rayleighOut, texel, vec4f(rayleigh, 1.0)); textureStore(mieOut, texel, vec4f(mie, 1.0)); textureStore(scatteringOut, texel, vec4f(rayleigh, mie.r));
}
@compute @workgroup_size(8, 8) fn densityPass(@builtin(global_invocation_id) id: vec3u) {
    let texel = layerTexel(id); if (texel.x >= SNU * SMUS || texel.y >= SMU) { return; }
    let p = texelPoint(vec3f(texel) + 0.5); let order = job.x;
    let omega = vec3f(sqrt(1.0 - p.mu * p.mu), 0.0, p.mu);
    let sunX = select((p.nu - p.mu * p.muS) / omega.x, 0.0, omega.x == 0.0);
    let omegaS = vec3f(sunX, sqrt(max(1.0 - sunX * sunX - p.muS * p.muS, 0.0)), p.muS);
    let n = 16; let dphi = AIR_PI / f32(n); let dtheta = AIR_PI / f32(n);
    let rayleighDensity = profileDensity(atmosphere.rayleighDensity, p.r - bottomRadius()); let mieDensity = profileDensity(atmosphere.mieDensity, p.r - bottomRadius());
    var sum = vec3f(0.0);
    for (var l = 0; l < n; l++) {
        let theta = (f32(l) + 0.5) * dtheta; let cosT = cos(theta); let sinT = sin(theta); let ground = intersectsGround(p.r, cosT);
        var toGround = 0.0; var groundLight = vec3f(0.0);
        if (ground) { toGround = distanceToBottom(p.r, cosT); groundLight = transmittanceAlong(transmittanceIn, lut, p.r, cosT, toGround, true) * atmosphere.groundAlbedo.rgb / AIR_PI; }
        for (var m = 0; m < 2 * n; m++) {
            let phi = (f32(m) + 0.5) * dphi; let omegaI = vec3f(cos(phi) * sinT, sin(phi) * sinT, cosT);
            var incident = orderScattering(p.r, omegaI.z, p.muS, dot(omegaS, omegaI), ground, order - 1);
            let groundNormal = normalize(vec3f(0.0, 0.0, p.r) + omegaI * toGround);
            incident += groundLight * lookupIrradiance(irradianceIn, lut, bottomRadius(), dot(groundNormal, omegaS));
            let nu2 = dot(omega, omegaI);
            sum += incident * (atmosphere.rayleigh.rgb * rayleighDensity * rayleighPhase(nu2) + atmosphere.mieScattering.rgb * mieDensity * miePhase(nu2)) * (dtheta * dphi * sinT);
        }
    }
    textureStore(densityOut, texel, vec4f(sum, 1.0));
}
@compute @workgroup_size(8, 8) fn indirectIrradiancePass(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= IW || id.y >= IH) { return; }
    let p = irradiancePoint(id.xy); let order = job.x - 1;
    let n = 32; let dphi = AIR_PI / f32(n); let dtheta = AIR_PI / f32(n); let omegaS = vec3f(sqrt(1.0 - p.y * p.y), 0.0, p.y);
    var sum = vec3f(0.0);
    for (var j = 0; j < n / 2; j++) {
        let theta = (f32(j) + 0.5) * dtheta;
        for (var i = 0; i < 2 * n; i++) {
            let phi = (f32(i) + 0.5) * dphi; let omega = vec3f(cos(phi) * sin(theta), sin(phi) * sin(theta), cos(theta));
            sum += orderScattering(p.x, omega.z, p.y, dot(omega, omegaS), false, order) * omega.z * (dtheta * dphi * sin(theta));
        }
    }
    textureStore(irradianceOut, id.xy, vec4f(sum, 1.0));
    textureStore(irradianceSumOut, id.xy, textureLoad(irradianceSumIn, id.xy, 0) + vec4f(sum, 0.0));
}
@compute @workgroup_size(8, 8) fn multipleScatteringPass(@builtin(global_invocation_id) id: vec3u) {
    let texel = layerTexel(id); if (texel.x >= SNU * SMUS || texel.y >= SMU) { return; }
    let p = texelPoint(vec3f(texel) + 0.5);
    let n = SCATTER_SAMPLES; let dx = distanceToNearest(p.r, p.mu, p.ground) / f32(n); var sum = vec3f(0.0);
    for (var i = 0; i <= n; i++) {
        let d = f32(i) * dx; let ri = clampRadius(sqrt(d * d + 2.0 * p.r * p.mu * d + p.r * p.r));
        let mui = clampCosine((p.r * p.mu + d) / ri); let muSi = clampCosine((p.r * p.muS + d * p.nu) / ri);
        sum += lookupScattering(densityIn, lut, ri, mui, muSi, p.nu, p.ground) * transmittanceAlong(transmittanceIn, lut, p.r, p.mu, d, p.ground) * select(1.0, 0.5, i == 0 || i == n);
    }
    sum *= dx;
    textureStore(rayleighOut, texel, vec4f(sum, 1.0)); // The next order's input (delta multiple scattering).
    textureStore(scatteringOut, texel, textureLoad(scatteringIn, texel, 0) + vec4f(sum / rayleighPhase(p.nu), 0.0));
}
)";
constexpr unsigned layersPerSlice=1,slices=32/layersPerSlice,jobStride=256;
WGPUStringView sv(const std::string& s){return {s.data(),s.size()};}
void pack(float* p,const DensityProfile& d){
    const auto& a=d.lower;const auto& b=d.upper;const float v[12]{a.width,a.expTerm,a.expScale,a.linearTerm,b.width,b.expTerm,b.expScale,b.linearTerm,a.constantTerm,b.constantTerm,0,0};std::memcpy(p,v,sizeof v);
}
}
AtmosphereModel earthAtmosphere(){ // demo.cc's values at kLambdaR/G/B (aerosols apart), lengths in km.
    AtmosphereModel m;m.bottom=6378.137f;m.top=m.bottom+80.f;
    auto rayleigh=[](double micrometres){return static_cast<float>(1.24062e-3*std::pow(micrometres,-4));};
    m.rayleigh={rayleigh(.68),rayleigh(.55),rayleigh(.44)};
    // Aerosols: a clear day's optical depth of 0.1 at 550 nm (Ångström exponent 1, single scattering
    // albedo 0.95) instead of Bruneton's 0.0053, which leaves the sky too dark and the horizon blue.
    auto aerosol=[](double micrometres){return 0.1/1.2*std::pow(micrometres/.55,-1.);};
    for(int c=0;c<3;++c){const double l=c==0?.68:c==1?.55:.44;m.mieExtinction[static_cast<std::size_t>(c)]=static_cast<float>(aerosol(l));m.mieScattering[static_cast<std::size_t>(c)]=static_cast<float>(.95*aerosol(l));}
    constexpr double ozone=300.*2.687e20/15000.*1e3; // Peak number density (per m³) times km.
    m.absorption={static_cast<float>(ozone*1.209e-25),static_cast<float>(ozone*3.5e-25),static_cast<float>(ozone*1.582e-26)};
    m.groundAlbedo={.1f,.1f,.1f};
    m.rayleighDensity.upper={0,1,-1.f/8.f,0,0};m.mieDensity.upper={0,1,-1.f/1.2f,0,0};
    m.absorptionDensity.lower={25,0,0,1.f/15.f,-2.f/3.f};m.absorptionDensity.upper={0,0,0,-1.f/15.f,8.f/3.f};
    m.mieG={.8f,.8f,.8f};m.sunAngularRadius=.004675f;m.maxSunZenith=static_cast<float>(102./180.*3.14159265358979);
    return m;
}
AtmosphereModel marsAtmosphere(){ // Lengths in km; coefficients at the model's ground (8.2 km under the datum).
    constexpr double datum=3396.19,depth=8.2,height=11.1,denser=2.0928; // exp(depth / height).
    AtmosphereModel m;m.bottom=static_cast<float>(datum-depth);m.top=m.bottom+110.f;
    // CO2: ~2.1e23 molecules/m³ at 610 Pa and 210 K, Rayleigh cross-section ~1.2e-30 m² at 532 nm
    // (Sneep & Ubachs 2005), as λ^-4: 2.5e-4/km at 550 nm on the datum.
    auto rayleigh=[](double micrometres){return static_cast<float>(2.5e-4*denser*std::pow(micrometres/.55,-4));};
    m.rayleigh={rayleigh(.68),rayleigh(.55),rayleigh(.44)};
    // Dust (Wolff et al. 2009): grey extinction, optical depth 0.5 above the datum; single scattering
    // albedo 0.97, 0.94 and 0.87 at 680, 550 and 440 nm (it absorbs blue).
    const double extinction=.5/height*denser;constexpr std::array<double,3> albedo{.97,.94,.87};
    for(std::size_t c=0;c<3;++c){m.mieExtinction[c]=static_cast<float>(extinction);m.mieScattering[c]=static_cast<float>(extinction*albedo[c]);}
    m.absorption={0,0,0};m.groundAlbedo={.3f,.2f,.12f};
    m.rayleighDensity.upper={0,1,static_cast<float>(-1/height),0,0};m.mieDensity.upper={0,1,static_cast<float>(-1/height),0,0};
    // Asymmetry rising toward blue (Tomasko et al. 1999, Pathfinder; Wolff et al. 2009): the dust
    // throws blue farther forward, a blue glow around the setting sun under a butterscotch sky.
    m.mieG={.63f,.67f,.73f};m.sunAngularRadius=static_cast<float>(.004675/1.524);m.maxSunZenith=static_cast<float>(110./180.*3.14159265358979); // Dusty twilights last.
    return m;
}
// The precomputation as a list of dispatches (each 3D pass in one-layer slices), recorded a few
// per frame by advance(); the temporaries go once it is all recorded (wgpu keeps them alive for the
// submitted work).
struct Atmosphere::Plan {
    struct Step {WGPUComputePipeline pipeline;WGPUBindGroup group;unsigned x,y,z;};
    std::vector<Step> steps;std::size_t next=0;
    std::vector<WGPUTexture> textures;std::vector<WGPUTextureView> views;std::vector<WGPUBindGroup> groups;std::vector<WGPUComputePipeline> pipelines;
    WGPUShaderModule module=nullptr;WGPUSampler sampler=nullptr;WGPUBuffer jobs=nullptr;
    ~Plan(){for(auto g:groups)wgpuBindGroupRelease(g);for(auto p:pipelines)wgpuComputePipelineRelease(p);for(auto v:views)wgpuTextureViewRelease(v);for(auto t:textures)wgpuTextureRelease(t);
        if(module)wgpuShaderModuleRelease(module);if(sampler)wgpuSamplerRelease(sampler);if(jobs)wgpuBufferRelease(jobs);}
};
Atmosphere::Atmosphere(Context& context,const AtmosphereModel& model):model_(model),plan_(std::make_unique<Plan>()){
    WGPUDevice device=context.device();auto& plan=*plan_;
    const bool wide=wgpuDeviceHasFeature(device,WGPUFeatureName_Float32Filterable); // 2D tables in f32 when filterable.
    const WGPUTextureFormat flat=wide?WGPUTextureFormat_RGBA32Float:WGPUTextureFormat_RGBA16Float;
    float p[parameterBytes/4]{};
    auto put=[&](int at,const std::array<float,3>& v,float w){p[at]=v[0];p[at+1]=v[1];p[at+2]=v[2];p[at+3]=w;};
    put(0,{1,1,1},model.sunAngularRadius);put(4,model.rayleigh,0);put(8,model.mieScattering,std::cos(model.maxSunZenith));
    put(12,model.mieExtinction,model.bottom);put(16,model.absorption,model.top);put(20,model.groundAlbedo,0);put(24,model.mieG,0);
    pack(p+28,model.rayleighDensity);pack(p+40,model.mieDensity);pack(p+52,model.absorptionDensity);
    WGPUBufferDescriptor bd{};bd.size=parameterBytes;bd.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;parameters_=wgpuDeviceCreateBuffer(device,&bd);
    wgpuQueueWriteBuffer(context.queue(),parameters_,0,p,parameterBytes);
    const unsigned orders=static_cast<unsigned>(std::max(model.orders,1));
    bd.size=(orders+1)*slices*jobStride;plan.jobs=wgpuDeviceCreateBuffer(device,&bd);
    {std::vector<std::byte> j(bd.size);for(unsigned o=0;o<=orders;++o)for(unsigned s=0;s<slices;++s){const int v[4]{static_cast<int>(o),static_cast<int>(s*layersPerSlice),0,0};std::memcpy(j.data()+(o*slices+s)*jobStride,v,16);}
     wgpuQueueWriteBuffer(context.queue(),plan.jobs,0,j.data(),j.size());}
    // Final T, S, M, E and the temporaries: deltas and ping-pong sums.
    auto texture=[&](unsigned w,unsigned h,unsigned d,WGPUTextureFormat f){WGPUTextureDescriptor t{};t.size={w,h,d};t.format=f;t.usage=WGPUTextureUsage_StorageBinding|WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopySrc;
        t.mipLevelCount=1;t.sampleCount=1;t.dimension=d>1?WGPUTextureDimension_3D:WGPUTextureDimension_2D;plan.textures.push_back(wgpuDeviceCreateTexture(device,&t));return plan.textures.back();};
    const unsigned sw=256,sh=128,sd=32;constexpr auto half=WGPUTextureFormat_RGBA16Float;
    WGPUTexture transmittance=texture(256,64,1,flat),deltaE=texture(64,16,1,flat),sumE[2]{texture(64,16,1,flat),texture(64,16,1,flat)};
    WGPUTexture deltaR=texture(sw,sh,sd,half),deltaM=texture(sw,sh,sd,half),density=texture(sw,sh,sd,half),sumS[2]{texture(sw,sh,sd,half),texture(sw,sh,sd,half)};
    auto view=[&](WGPUTexture t){plan.views.push_back(wgpuTextureCreateView(t,nullptr));return plan.views.back();};
    const WGPUTextureView vT=view(transmittance),vDE=view(deltaE),vE[2]{view(sumE[0]),view(sumE[1])},vR=view(deltaR),vM=view(deltaM),vD=view(density),vS[2]{view(sumS[0]),view(sumS[1])};
    WGPUSamplerDescriptor s{};s.addressModeU=s.addressModeV=s.addressModeW=WGPUAddressMode_ClampToEdge;s.magFilter=s.minFilter=WGPUFilterMode_Linear;s.mipmapFilter=WGPUMipmapFilterMode_Nearest;s.lodMaxClamp=1;s.maxAnisotropy=1;
    plan.sampler=wgpuDeviceCreateSampler(device,&s);
    std::string code=std::string(atmosphereWgsl)+precomputeWgsl;
    for(std::size_t at;(at=code.find("FLAT"))!=std::string::npos;)code.replace(at,4,wide?"rgba32float":"rgba16float");
    WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code=sv(code);WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;
    plan.module=wgpuDeviceCreateShaderModule(device,&md);
    auto pipeline=[&](const char* entry){WGPUComputePipelineDescriptor d{};d.compute.module=plan.module;d.compute.entryPoint={entry,std::strlen(entry)};plan.pipelines.push_back(wgpuDeviceCreateComputePipeline(device,&d));return plan.pipelines.back();};
    const WGPUComputePipeline pT=pipeline("transmittancePass"),pDE=pipeline("directIrradiancePass"),pSS=pipeline("singleScatteringPass"),pD=pipeline("densityPass"),pIE=pipeline("indirectIrradiancePass"),pMS=pipeline("multipleScatteringPass");
    struct Bind {std::uint32_t binding;WGPUTextureView view;};
    // One dispatch: the parameters, `job` (order, slice) when used, the sampler when anything is sampled, and these textures.
    auto dispatch=[&](WGPUComputePipeline pl,std::initializer_list<Bind> binds,unsigned x,unsigned y,unsigned z,unsigned order,unsigned slice,bool usesJob,bool samples){
        std::vector<WGPUBindGroupEntry> e;WGPUBindGroupEntry a{};a.binding=0;a.buffer=parameters_;a.size=parameterBytes;e.push_back(a);
        if(samples){WGPUBindGroupEntry b{};b.binding=1;b.sampler=plan.sampler;e.push_back(b);}
        if(usesJob){WGPUBindGroupEntry b{};b.binding=17;b.buffer=plan.jobs;b.offset=(order*slices+slice)*jobStride;b.size=16;e.push_back(b);}
        for(const auto& b:binds){WGPUBindGroupEntry t{};t.binding=b.binding;t.textureView=b.view;e.push_back(t);}
        WGPUBindGroupLayout layout=wgpuComputePipelineGetBindGroupLayout(pl,0);
        WGPUBindGroupDescriptor gd{};gd.layout=layout;gd.entryCount=e.size();gd.entries=e.data();plan.groups.push_back(wgpuDeviceCreateBindGroup(device,&gd));wgpuBindGroupLayoutRelease(layout);
        plan.steps.push_back({pl,plan.groups.back(),x,y,z});
    };
    auto layers=[&](WGPUComputePipeline pl,std::initializer_list<Bind> binds,unsigned order){for(unsigned i=0;i<slices;++i)dispatch(pl,binds,sw/8,sh/8,layersPerSlice,order,i,true,true);};
    dispatch(pT,{{10,vT}},256/8,64/8,1,0,0,false,false);
    dispatch(pDE,{{2,vT},{11,vDE},{12,vE[0]}},64/8,16/8,1,0,0,false,true);
    layers(pSS,{{2,vT},{13,vR},{14,vM},{15,vS[0]}},1);
    int e=0,sc=0; // Current sums: vE[e], vS[sc].
    for(unsigned order=2;order<=orders;++order){
        layers(pD,{{2,vT},{3,vR},{4,vM},{5,vR},{6,vDE},{16,vD}},order);
        dispatch(pIE,{{3,vR},{4,vM},{5,vR},{9,vE[e]},{11,vDE},{12,vE[1-e]}},64/8,16/8,1,order,0,true,true);e=1-e;
        layers(pMS,{{2,vT},{7,vD},{8,vS[sc]},{13,vR},{15,vS[1-sc]}},order);sc=1-sc;
    }
    textures_={transmittance,sumS[sc],deltaM,sumE[e]};
    for(std::size_t i=0;i<4;++i){wgpuTextureAddRef(textures_[i]);views_[i]=wgpuTextureCreateView(textures_[i],nullptr);}
}
Atmosphere::~Atmosphere(){
    for(auto v:views_)if(v)wgpuTextureViewRelease(v);for(auto t:textures_)if(t)wgpuTextureRelease(t);if(parameters_)wgpuBufferRelease(parameters_);
}
bool Atmosphere::ready()const{return !plan_;}
bool Atmosphere::advance(WGPUCommandEncoder encoder,unsigned steps){
    if(!plan_)return true;
    WGPUComputePassEncoder pass=wgpuCommandEncoderBeginComputePass(encoder,nullptr);
    for(auto& plan=*plan_;steps--&&plan.next<plan.steps.size();++plan.next){const auto& s=plan.steps[plan.next];
        wgpuComputePassEncoderSetPipeline(pass,s.pipeline);wgpuComputePassEncoderSetBindGroup(pass,0,s.group,0,nullptr);wgpuComputePassEncoderDispatchWorkgroups(pass,s.x,s.y,s.z);}
    wgpuComputePassEncoderEnd(pass);wgpuComputePassEncoderRelease(pass);
    if(plan_->next==plan_->steps.size())plan_.reset();
    return !plan_;
}
void Atmosphere::precompute(Context& context){
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(context.device(),nullptr);advance(encoder,~0u);
    WGPUCommandBuffer commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(context.queue(),1,&commands);wgpuCommandBufferRelease(commands);wgpuCommandEncoderRelease(encoder);
}
}
