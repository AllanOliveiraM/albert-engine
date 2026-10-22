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
struct Varyings { @builtin(position) clip: vec4f, @location(0) colour: vec3f, @location(1) corner: vec2f, @location(2) fade: f32 };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) motion: vec3f, @location(2) colour: vec3f, @location(3) size: f32, @builtin(vertex_index) i: u32) -> Varyings {
    var o: Varyings; let c = quad(i);
    let clip = frame.projection * object.modelView * vec4f(normalize(position + motion * material.years) * 80000.0, 1.0);
    o.clip = vec4f(clip.xy + c * size / frame.viewport.xy * clip.w, clip.zw); o.colour = colour; o.corner = c; o.fade = 1.0;
    if (frame.air.w > 0.0) { // Through the air, and hidden once the sky outshines them (display brightness is artistic).
        let d = normalize((frame.planet * vec4f(mat3x3f(object.model[0].xyz, object.model[1].xyz, object.model[2].xyz) * position, 0.0)).xyz);
        let sky = skyRadiance(frame.air.xyz, d, frame.airSun.xyz); let glow = dot(sky.radiance * frame.sunColor.rgb, vec3f(0.2126, 0.7152, 0.0722)) * frame.exposure.x;
        o.colour = colour * mix(vec3f(1.0), sky.transmittance, frame.air.w); o.fade = mix(1.0, exp(-40.0 * glow), frame.air.w);
    }
    return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let alpha = (1.0 - smoothstep(0.2, 1.0, length(v.corner))) * v.fade;
    if (alpha < 0.01) { discard; }
    return vec4f(linearToSrgb(v.colour), alpha);
}
)";
    albert::gpu::define("albert:stars",p);
}
// visuals.cpp surfaceVertex: uv, local and world position and normal (normal by mat3(model), as there).
const char* const surface=R"(
struct Surface { @builtin(position) clip: vec4f, @location(0) uv: vec2f, @location(1) local: vec3f, @location(2) localNormal: vec3f, @location(3) worldNormal: vec3f, @location(4) world: vec3f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f) -> Surface {
    var o: Surface; let world = object.model * vec4f(position, 1.0);
    o.uv = uv; o.local = position; o.localNormal = normal; o.world = world.xyz;
    o.worldNormal = normalize(mat3x3f(object.model[0].xyz, object.model[1].xyz, object.model[2].xyz) * normal);
    o.clip = frame.projection * frame.view * world;
    return o;
}
)";
const std::vector<std::pair<std::string,int>> surfaceAttributes{{"normal",3},{"uv",2}};
// eclipses.cpp eclipseShader(): visible fraction of the solar disc past a spherical blocker.
const char* const visibleSun=R"(
fn visibleSun(point: vec3f, sun: vec3f, sunRadius: f32, blocker: vec3f, radius: f32) -> f32 {
    let s = sun - point; let o = blocker - point; let ds = length(s); let dO = length(o);
    if (ds <= sunRadius || dO <= radius || dO >= ds) { return 1.0; }
    let a = asin(clamp(sunRadius / ds, 0.0, 1.0)); let b = asin(clamp(radius / dO, 0.0, 1.0));
    let d = atan2(length(cross(s / ds, o / dO)), dot(s / ds, o / dO));
    if (d >= a + b) { return 1.0; }
    if (d <= abs(a - b)) { return clamp(1.0 - min(a * a, b * b) / (a * a), 0.0, 1.0); }
    let u = clamp((d * d + a * a - b * b) / (2.0 * d * a), -1.0, 1.0); let v = clamp((d * d + b * b - a * a) / (2.0 * d * b), -1.0, 1.0);
    let area = a * a * acos(u) + b * b * acos(v) - 0.5 * sqrt(max(0.0, (-d + a + b) * (d + a - b) * (d - a + b) * (d + a + b)));
    return clamp(1.0 - area / (3.14159265359 * a * a), 0.0, 1.0);
}
)";
// visuals.cpp cloudSampling: a regional crop replaces the global clouds. Gradients come from the
// caller (uniform control flow) so the crops can sample under conditions.
const char* const cloudSampling=R"(
fn cloudCrop(image: texture_2d<f32>, smp: sampler, bounds: vec4f, enabled: f32, uv: vec2f, dx: vec2f, dy: vec2f, global: f32) -> f32 {
    let local = vec2f(fract(uv.x - bounds.x), uv.y - bounds.y) / bounds.zw;
    if (enabled < 0.5 || any(local < vec2f(0.0)) || any(local > vec2f(1.0))) { return global; }
    let detail = textureSampleGrad(image, smp, local, dx / bounds.zw, dy / bounds.zw);
    let edge = min(local, 1.0 - local);
    return mix(global, detail.g, smoothstep(0.0, 0.04, min(edge.x, edge.y)) * detail.a);
}
fn cloudDensity(uv: vec2f, dx: vec2f, dy: vec2f) -> f32 {
    let global = textureSampleGrad(cloudMap, cloudSampler, uv, dx, dy).g;
    return mix(cloudCrop(previousDetailMap, previousSampler, material.previousDetailBounds, material.previousDetailEnabled, uv, dx, dy, global),
        cloudCrop(detailMap, detailSampler, material.detailBounds, material.detailEnabled, uv, dx, dy, global), material.detailBlend);
}
)";
const std::vector<std::pair<std::string,Uniform>> cloudUniforms{{"detailBounds",Uniform::vec4},{"previousDetailBounds",Uniform::vec4},{"detailEnabled",Uniform::f32},{"previousDetailEnabled",Uniform::f32},{"detailBlend",Uniform::f32}};
const char* const cloudFields="detailBounds: vec4f, previousDetailBounds: vec4f, detailEnabled: f32, previousDetailEnabled: f32, detailBlend: f32";
std::string cloudBindings(int first){ // cloudMap, detailMap, previousDetailMap at texture slots first..first+2.
    std::string s;const char* names[][2]{{"cloudMap","cloudSampler"},{"detailMap","detailSampler"},{"previousDetailMap","previousSampler"}};
    for(int i=0;i<3;++i){const int t=1+2*(first+i);s+="@group(2) @binding("+std::to_string(t)+") var "+names[i][0]+": texture_2d<f32>;\n@group(2) @binding("+std::to_string(t+1)+") var "+names[i][1]+": sampler;\n";}
    return s;
}
void earthLayers(){
    Program sun;sun.attributes=surfaceAttributes;sun.textures={"sunMap"};
    sun.wgsl=std::string(surface)+R"(
@group(2) @binding(1) var sunMap: texture_2d<f32>;
@group(2) @binding(2) var sunSampler: sampler;
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let n = normalize(v.localNormal);
    let photo = textureSample(sunMap, sunSampler, vec2f(0.5) + n.xy * 0.4625).rgb; // Orthographic HMI snapshot.
    var intensity = clamp(dot(photo, vec3f(0.2126, 0.7152, 0.0722)) / 0.74, 0.05, 1.1);
    intensity = mix(1.0, intensity, smoothstep(0.0, 0.65, n.z));
    let mu = max(0.0, dot(normalize(v.worldNormal), normalize(frame.camera.xyz - v.world)));
    return vec4f(linearToSrgb(vec3f(1.0, 0.96, 0.88) * intensity * (0.4 + 0.6 * mu) * airFilter(v.world)), 1.0); // Reddened through the air.
}
)";
    albert::gpu::define("albert:sun",sun);
    Program corona;corona.attributes=surfaceAttributes;
    corona.wgsl=std::string(surface)+R"(
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let mu = abs(dot(normalize(v.worldNormal), normalize(frame.camera.xyz - v.world)));
    let impact = sqrt(max(0.0, 1.0 - mu * mu)) * 1.15;
    let glow = exp(-max(0.0, impact - 1.0) * 24.0) * (1.0 - smoothstep(1.04, 1.15, impact));
    return vec4f(linearToSrgb(vec3f(1.0, 0.94, 0.83) * airFilter(v.world)), glow * 0.10);
}
)";
    albert::gpu::define("albert:corona",corona);
    // vehicle.cpp: a rocket plume's glow on its cone (uv.y 1 at the nozzle, 0 at the tail), brightest down its axis and near
    // the nozzle, fading to the edges and the tail, with bright shock diamonds near the nozzle in thick air; colours in linear
    // light, strength its brightness, filtered by the air.
    Program plume;plume.attributes=surfaceAttributes;plume.uniforms={{"core",Uniform::vec3},{"edge",Uniform::vec3},{"strength",Uniform::f32},{"diamonds",Uniform::f32}};
    plume.wgsl=std::string(surface)+R"(
struct Material { core: vec3f, edge: vec3f, strength: f32, diamonds: f32 };
@group(2) @binding(0) var<uniform> material: Material;
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let mu = abs(dot(normalize(v.worldNormal), normalize(frame.camera.xyz - v.world)));
    let along = clamp(v.uv.y, 0.0, 1.0);
    let bands = pow(0.5 + 0.5 * cos(along * 37.7), 6.0) * smoothstep(0.45, 0.85, along); // Shock diamonds near the nozzle (diamonds: 0-1).
    let glow = pow(mu, 1.6) * smoothstep(0.0, 0.7, along) * (0.35 + 0.65 * along) * (1.0 + 1.4 * material.diamonds * bands);
    let colour = mix(material.edge, material.core, mu * along);
    return vec4f(linearToSrgb(colour * airFilter(v.world)), glow * material.strength);
}
)";
    albert::gpu::define("albert:plume",plume);
    // vehicle.cpp: the shock layer's glow around a vehicle entering the air (a shell offset to its windward side): faint where
    // looked through face-on, brightest at its rim, the core colour face-on; strength from the heating.
    Program plasma;plasma.attributes=surfaceAttributes;plasma.uniforms={{"core",Uniform::vec3},{"edge",Uniform::vec3},{"strength",Uniform::f32},{"upwind",Uniform::vec3}};
    plasma.wgsl=std::string(surface)+R"(
struct Material { core: vec3f, edge: vec3f, strength: f32, upwind: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
// The shock layer: bright where it faces the air (the bow shock over the windward side, limb-brightened), a faint wake behind,
// nothing at the sides: no outline of a shell.
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let n = normalize(v.worldNormal);
    let facing = dot(n, normalize(material.upwind));
    let mu = abs(dot(n, normalize(frame.camera.xyz - v.world)));
    let shock = pow(max(facing, 0.0), 1.6) * (0.35 + 0.65 * (1.0 - mu));
    let wake = 0.25 * pow(max(-facing, 0.0), 3.0) * (1.0 - mu);
    let colour = mix(material.edge, material.core, clamp(facing, 0.0, 1.0));
    return vec4f(linearToSrgb(colour * airFilter(v.world)), (shock + wake) * material.strength);
}
)";
    albert::gpu::define("albert:plasma",plasma);
    Program clouds;clouds.attributes=surfaceAttributes;clouds.textures={"cloudMap","detailMap","previousDetailMap"};
    clouds.uniforms=cloudUniforms;for(auto u:{std::pair{"solarDirection",Uniform::vec3},{"moonPosition",Uniform::vec3},{"eclipseSunPosition",Uniform::vec3},{"eclipseEnabled",Uniform::f32},{"visibility",Uniform::f32}})clouds.uniforms.emplace_back(u.first,u.second);
    clouds.wgsl=std::string(surface)+"struct Material { "+cloudFields+", solarDirection: vec3f, moonPosition: vec3f, eclipseSunPosition: vec3f, eclipseEnabled: f32, visibility: f32 };\n@group(2) @binding(0) var<uniform> material: Material;\n"
        +cloudBindings(0)+cloudSampling+visibleSun+R"(
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let density = cloudDensity(v.uv, dpdx(v.uv), dpdy(v.uv));
    let light = max(0.0, dot(normalize(v.worldNormal), material.solarDirection));
    let transmission = mix(1.0, visibleSun(v.world, material.eclipseSunPosition, 695700.0 / 6378.137, material.moonPosition, 1737.4 / 6378.137), material.eclipseEnabled);
    let air = airLight(v.world, v.worldNormal); // Sunlight through the air above the cloud tops, and the sky's light.
    let lit = vec3f(1.0, 0.98, 0.95) * (0.003 + 2.4 * (light * transmission * air.sun + air.sky)); // 2.4: the sun's intensity at 1 AU.
    return vec4f(finish(airApply(v.world, lit)), density * 0.88 * material.visibility);
}
)";
    albert::gpu::define("albert:clouds",clouds);
    Program night;night.attributes=surfaceAttributes;night.textures={"cloudMap","detailMap","previousDetailMap","nightMap","nightDetailMap"};
    night.uniforms=cloudUniforms;for(auto u:{std::pair{"nightDetailBounds",Uniform::vec4},{"nightDetailEnabled",Uniform::f32},{"cloudOpacity",Uniform::f32},{"visibility",Uniform::f32},{"solarDirection",Uniform::vec3}})night.uniforms.emplace_back(u.first,u.second);
    night.wgsl=std::string(surface)+"struct Material { "+cloudFields+", nightDetailBounds: vec4f, nightDetailEnabled: f32, cloudOpacity: f32, visibility: f32, solarDirection: vec3f };\n@group(2) @binding(0) var<uniform> material: Material;\n"
        +cloudBindings(0)+R"(
@group(2) @binding(7) var nightMap: texture_2d<f32>;
@group(2) @binding(8) var nightSampler: sampler;
@group(2) @binding(9) var nightDetailMap: texture_2d<f32>;
@group(2) @binding(10) var nightDetailSampler: sampler;
)"+cloudSampling+R"(
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let dx = dpdx(v.uv); let dy = dpdy(v.uv);
    let night = 1.0 - smoothstep(-0.12, 0.015, dot(normalize(v.worldNormal), material.solarDirection));
    var emission = max(0.0, textureSample(nightMap, nightSampler, v.uv).r - 0.008);
    let b = material.nightDetailBounds; let regional = vec2f(fract(v.uv.x - b.x), v.uv.y - b.y) / b.zw;
    if (material.nightDetailEnabled > 0.5 && all(regional >= vec2f(0.0)) && all(regional <= vec2f(1.0))) {
        let s = textureSampleGrad(nightDetailMap, nightDetailSampler, regional, dx / b.zw, dy / b.zw);
        let detailed = max(0.0, min(s.r, s.g) - s.b * 0.65 - 0.008) * 1.6; // Keep city light, drop blue ocean.
        let edge = min(regional, 1.0 - regional);
        emission = mix(emission, detailed, smoothstep(0.0, 0.04, min(edge.x, edge.y)) * s.a);
    }
    let lights = vec3f(1.0, 0.69, 0.32) * pow(emission, 1.3);
    let transmission = exp(-4.5 * cloudDensity(v.uv, dx, dy) * material.cloudOpacity);
    return vec4f(finish(lights * night * transmission * 1.8 * airFilter(v.world)), material.visibility);
}
)";
    albert::gpu::define("albert:night",night);
    // Bruneton's sky on the frame's air, drawn first as the background (objects overwrite it with
    // their own aerial perspective); toward the ground it holds the air up to the sphere.
    Program air;air.attributes=surfaceAttributes;air.background=true;air.uniforms={{"visibility",Uniform::f32}};
    air.wgsl=std::string(surface)+R"(
struct Material { visibility: f32 };
@group(2) @binding(0) var<uniform> material: Material;
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    if (frame.air.w <= 0.0) { discard; }
    let c = frame.air.xyz; let ray = normalize(airPoint(v.world) - c); var sky = skyRadiance(c, ray, frame.airSun.xyz);
    if (length(c) < topRadius() && skyHitsGround(c, ray)) { sky = skyRadianceToPoint(c, horizonPoint(c, ray), frame.airSun.xyz, true); } // Below the sphere's horizon, where the coarser globe mesh leaves gaps: the horizon's haze.
    return vec4f(finish(sky.radiance * frame.sunColor.rgb), material.visibility * frame.air.w);
}
)";
    albert::gpu::define("albert:atmosphere",air);
    Program eclipse;eclipse.attributes={{"uv",2}};eclipse.textures={"alphaMap","surfaceMap"};
    eclipse.uniforms={{"sunPosition",Uniform::vec3},{"blockerPosition",Uniform::vec3},{"blockerRadius",Uniform::f32},{"lunar",Uniform::f32},{"hasAlpha",Uniform::f32},{"hasSurface",Uniform::f32},{"cutoff",Uniform::f32}};
    eclipse.wgsl=std::string(visibleSun)+R"(
struct Material { sunPosition: vec3f, blockerPosition: vec3f, blockerRadius: f32, lunar: f32, hasAlpha: f32, hasSurface: f32, cutoff: f32 };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var alphaMap: texture_2d<f32>;
@group(2) @binding(2) var alphaSampler: sampler;
@group(2) @binding(3) var surfaceMap: texture_2d<f32>;
@group(2) @binding(4) var surfaceSampler: sampler;
struct Varyings { @builtin(position) clip: vec4f, @location(0) point: vec3f, @location(1) uv: vec2f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) uv: vec2f) -> Varyings {
    var o: Varyings; o.point = (object.model * vec4f(position, 1.0)).xyz; o.uv = uv; o.clip = frame.projection * object.modelView * vec4f(position, 1.0); return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let a = textureSample(alphaMap, alphaSampler, v.uv).g; let s = textureSample(surfaceMap, surfaceSampler, v.uv).a;
    var alpha = 1.0; if (material.hasAlpha > 0.5) { alpha *= a; } if (material.hasSurface > 0.5) { alpha *= s; }
    if (alpha < material.cutoff) { discard; }
    let light = visibleSun(v.point, material.sunPosition, 695700.0 / 6378.137, material.blockerPosition, material.blockerRadius);
    var transmission = vec3f(max(light, 0.0002));
    if (material.lunar > 0.5) { // Display model of refracted terrestrial sunlight (not Danjon brightness).
        let sd = normalize(material.sunPosition - v.point); let od = normalize(material.blockerPosition - v.point);
        let separation = atan2(length(cross(sd, od)), dot(sd, od));
        let sunAngle = asin(min(1.0, (695700.0 / 6378.137) / length(material.sunPosition - v.point)));
        let earthAngle = asin(min(1.0, material.blockerRadius / length(material.blockerPosition - v.point)));
        let depth = clamp((earthAngle - sunAngle - separation) / max(earthAngle - sunAngle, 0.000001), 0.0, 1.0);
        transmission += vec3f(0.035, 0.0035, 0.0007) * (0.25 + 0.75 * (1.0 - depth)) * pow(1.0 - light, 8.0);
    }
    return vec4f(pow(clamp(transmission, vec3f(0.0), vec3f(1.0)), vec3f(1.0 / 2.2)), 1.0); // Multiplies the encoded framebuffer.
}
)";
    albert::gpu::define("albert:eclipse",eclipse);
}
}
namespace {
// paths.cpp line shaders: segments extruded in screen space, clipped at the near plane in view space.
const char* const lineExtrusion=R"(
fn extrude(p0: vec4f, q0: vec4f, side: f32, width: f32, nearClip: f32, viewport: vec2f) -> vec4f {
    var p = p0; var q = q0;
    if (p.z > -nearClip && q.z > -nearClip) { return vec4f(2.0, 2.0, 2.0, 1.0); }
    if (p.z > -nearClip) { p = mix(p, q, (-nearClip - p.z) / (q.z - p.z)); }
    if (q.z > -nearClip) { q = mix(q, p, (-nearClip - q.z) / (p.z - q.z)); }
    let clip = frame.projection * p; let other = frame.projection * q;
    let direction = (other.xy / other.w - clip.xy / clip.w) * viewport;
    let n = vec2f(-direction.y, direction.x) / max(length(direction), 0.0001);
    return vec4f(clip.xy + n * side * width / viewport * clip.w, clip.zw);
}
)";
void paths(){
    Program orbit;orbit.attributes={{"nextPosition",3},{"orbitTime",1},{"nextTime",1},{"lineSide",1}};
    orbit.uniforms={{"viewport",Uniform::vec2},{"nearClip",Uniform::f32},{"strokeWidth",Uniform::f32},{"present",Uniform::f32},{"horizon",Uniform::f32},{"dashDays",Uniform::f32},{"visibility",Uniform::f32},{"referenceVisibility",Uniform::f32}};
    orbit.wgsl=std::string(lineExtrusion)+R"(
struct Material { viewport: vec2f, nearClip: f32, strokeWidth: f32, present: f32, horizon: f32, dashDays: f32, visibility: f32, referenceVisibility: f32 };
@group(2) @binding(0) var<uniform> material: Material;
struct Varyings { @builtin(position) clip: vec4f, @location(0) time: f32, @location(1) @interpolate(linear) side: f32 };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) next: vec3f, @location(2) time: f32, @location(3) nextTime: f32, @location(4) lineSide: f32) -> Varyings {
    var o: Varyings; let rotation = mat3x3f(frame.view[0].xyz, frame.view[1].xyz, frame.view[2].xyz); // Positions are camera-relative.
    let p = vec4f(rotation * position, 1.0); let q = vec4f(rotation * next, 1.0);
    o.time = time; o.side = lineSide * select(1.0, -1.0, nextTime < time);
    if (p.z > -material.nearClip && q.z <= -material.nearClip) { o.time = mix(time, nextTime, (-material.nearClip - p.z) / (q.z - p.z)); }
    o.clip = extrude(p, q, lineSide, material.strokeWidth, material.nearClip, material.viewport);
    return o;
}
fn pulseIntegral(value: f32) -> f32 { return floor(value) * 0.63 + min(fract(value), 0.63); } // Footprint-averaged dashes.
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let age = v.time - material.present;
    let fade = 1.0 - smoothstep(0.02, 1.0, abs(age) / material.horizon);
    let future = step(0.0, age);
    let phase = age / material.dashDays;
    let footprint = max(fwidth(phase), 0.0001);
    var dash = (pulseIntegral(phase + footprint * 0.5) - pulseIntegral(phase - footprint * 0.5)) / footprint;
    dash = mix(clamp(dash, 0.0, 1.0), 0.63, smoothstep(0.12, 0.5, footprint));
    let colour = mix(vec3f(0.29, 0.67, 0.79), vec3f(0.94, 0.70, 0.40), future);
    let edge = abs(v.side);
    var coverage = 1.0 - smoothstep(0.35, 0.75, edge);
    coverage = max(coverage, 0.12 * (1.0 - smoothstep(0.6, 1.0, edge)));
    let guide = material.referenceVisibility * 0.42 * (1.0 - smoothstep(0.25, 0.95, edge * material.strokeWidth * 0.5));
    let highlight = coverage * fade * mix(0.78, 0.92 * dash, future);
    let alpha = guide + highlight * (1.0 - guide);
    let ink = mix(vec3f(0.32, 0.48, 0.55), colour, highlight / max(alpha, 0.00001));
    return vec4f(linearToSrgb(ink), material.visibility * alpha);
}
)";
    albert::gpu::define("albert:orbit",orbit);
    // Borders and vector layers: lines projected on the ellipsoid plus relief (sampled per vertex).
    const std::string borderHead=std::string(lineExtrusion)+R"(
struct Material { viewport: vec2f, nearClip: f32, surfaceOffset: f32, detailEnabled: f32, clampSurface: f32, widthPixels: f32, detailBounds: vec4f, visibility: f32, lineColor: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var heightMap: texture_2d<f32>;
@group(2) @binding(2) var heightSampler: sampler;
@group(2) @binding(3) var detailHeightMap: texture_2d<f32>;
@group(2) @binding(4) var detailSampler: sampler;
fn surface(point: vec3f) -> vec3f {
    if (material.clampSurface < 0.5) { return point; }
    let axes = vec3f(1.0, 6356.752314245 / 6378.137, 1.0);
    let radial = normalize(point / axes);
    let latitude = atan2(radial.y, axes.y * length(radial.xz));
    let uv = vec2f(atan2(-radial.z, radial.x) / 6.28318530718 + 0.5, latitude / 3.14159265359 + 0.5);
    var height = textureSampleLevel(heightMap, heightSampler, uv, 0.0).r;
    let b = material.detailBounds; let local = vec2f(fract(uv.x - b.x), uv.y - b.y) / b.zw;
    if (material.detailEnabled > 0.5 && all(local >= vec2f(0.0)) && all(local <= vec2f(1.0))) {
        let detailed = textureSampleLevel(detailHeightMap, detailSampler, local, 0.0).r;
        let edge = min(local, 1.0 - local);
        height = select(max(height, detailed), detailed, min(edge.x, edge.y) > 0.025); // The coarse globe is masked inside the patch core.
    }
    return radial * axes * (1.0 + height / 6378137.0 + material.surfaceOffset);
}
)";
    const std::vector<std::pair<std::string,Uniform>> borderUniforms{{"viewport",Uniform::vec2},{"nearClip",Uniform::f32},{"surfaceOffset",Uniform::f32},{"detailEnabled",Uniform::f32},{"clampSurface",Uniform::f32},{"widthPixels",Uniform::f32},{"detailBounds",Uniform::vec4},{"visibility",Uniform::f32},{"lineColor",Uniform::vec3}};
    Program border;border.attributes={{"nextPosition",3},{"lineSide",1},{"orbitTime",1},{"nextTime",1}};border.uniforms=borderUniforms;border.textures={"heightMap","detailHeightMap"};
    border.wgsl=borderHead+R"(
struct Varyings { @builtin(position) clip: vec4f, @location(0) @interpolate(linear) side: f32 };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) next: vec3f, @location(2) lineSide: f32, @location(3) time: f32, @location(4) nextTime: f32) -> Varyings {
    var o: Varyings;
    let p = object.modelView * vec4f(surface(position), 1.0); let q = object.modelView * vec4f(surface(next), 1.0);
    o.side = lineSide * select(1.0, -1.0, nextTime < time);
    o.clip = extrude(p, q, lineSide, material.widthPixels, material.nearClip, material.viewport);
    return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    return vec4f(linearToSrgb(material.lineColor), (1.0 - smoothstep(0.3, 0.95, abs(v.side))) * 0.75 * material.visibility);
}
)";
    albert::gpu::define("albert:border",border);
    Program marker=border;marker.attributes.emplace_back("markerCorner",2); // layers.cpp point markers: screen-space discs.
    marker.wgsl=borderHead+R"(
struct Varyings { @builtin(position) clip: vec4f, @location(0) @interpolate(linear) marker: vec2f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) next: vec3f, @location(2) lineSide: f32, @location(3) time: f32, @location(4) nextTime: f32, @location(5) corner: vec2f) -> Varyings {
    var o: Varyings; o.marker = corner;
    let p = object.modelView * vec4f(surface(position), 1.0); var clip = frame.projection * p;
    clip = vec4f(clip.xy + corner * material.widthPixels / material.viewport * clip.w, clip.zw);
    o.clip = select(clip, vec4f(2.0, 2.0, 2.0, 1.0), p.z > -material.nearClip);
    return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let radius = length(v.marker); let edge = max(fwidth(radius), 0.01);
    let coverage = 1.0 - smoothstep(1.0 - edge, 1.0, radius);
    if (coverage <= 0.0) { discard; }
    return vec4f(linearToSrgb(material.lineColor), coverage * material.visibility);
}
)";
    albert::gpu::define("albert:marker",marker);
}
}
namespace {
void solids(){ // solids.cpp: other planets and moons, haze shells and rings.
    Program surface;surface.attributes={{"normal",3},{"uv",2}};surface.textures={"heightMap","baseHeightMap","dayMap","baseMap","surfaceMask"};
    surface.uniforms={{"displacement",Uniform::f32},{"isPatch",Uniform::f32},{"baseRadius",Uniform::f32},{"detailBlend",Uniform::f32},{"shapeSurface",Uniform::f32},{"bounds",Uniform::vec4},
        {"solarDirection",Uniform::vec3},{"intensity",Uniform::f32},{"angularRadius",Uniform::f32},{"masked",Uniform::f32},{"auxiliaryStrength",Uniform::f32},{"cloudSurface",Uniform::f32},{"haze",Uniform::f32},{"hazeAlbedo",Uniform::vec3}};
    surface.wgsl=R"(
struct Material { displacement: f32, isPatch: f32, baseRadius: f32, detailBlend: f32, shapeSurface: f32, bounds: vec4f, solarDirection: vec3f, intensity: f32, angularRadius: f32,
    masked: f32, auxiliaryStrength: f32, cloudSurface: f32, haze: f32, hazeAlbedo: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var heightMap: texture_2d<f32>;
@group(2) @binding(2) var heightSampler: sampler;
@group(2) @binding(3) var baseHeightMap: texture_2d<f32>;
@group(2) @binding(4) var baseHeightSampler: sampler;
@group(2) @binding(5) var dayMap: texture_2d<f32>;
@group(2) @binding(6) var daySampler: sampler;
@group(2) @binding(7) var baseMap: texture_2d<f32>;
@group(2) @binding(8) var baseSampler: sampler;
@group(2) @binding(9) var surfaceMask: texture_2d<f32>;
@group(2) @binding(10) var maskSampler: sampler;
struct Varyings { @builtin(position) clip: vec4f, @location(0) uv: vec2f, @location(1) globalUv: vec2f, @location(2) normal: vec3f, @location(3) position: vec3f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f) -> Varyings {
    var o: Varyings; o.uv = uv;
    o.globalUv = select(material.bounds.xy + uv * material.bounds.zw, uv, material.shapeSurface > 0.5);
    let radial = normalize(position);
    var p = position + radial * textureSampleLevel(heightMap, heightSampler, uv, 0.0).r * material.displacement;
    if (material.isPatch > 0.5) {
        let coarse = material.baseRadius + (textureSampleLevel(baseHeightMap, baseHeightSampler, o.globalUv, 0.0).r + 20.0) / 6378137.0;
        p = radial * mix(coarse, length(position), material.detailBlend);
    }
    let view = object.modelView * vec4f(p, 1.0);
    o.normal = normalize(object.normalView * select(radial, normal, material.shapeSurface > 0.5)); o.position = view.xyz;
    o.clip = frame.projection * view;
    return o;
}
const PI = 3.141592653589793;
fn terrain(local: vec2f, global: vec2f) -> f32 {
    return mix(heightAt(baseHeightMap, global, true), heightAt(heightMap, local, false), material.detailBlend) / 6378137.0; // f32 bilinear: no stair steps.
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let mask = textureSample(surfaceMask, maskSampler, v.globalUv).g;
    if (material.masked > 0.5 && mask < 0.5) { discard; }
    let b = material.bounds;
    let localUv = select(v.uv, vec2f(fract(v.uv.x - b.x), v.uv.y - b.y) / b.zw, material.shapeSurface > 0.5);
    let sampleColour = textureSample(dayMap, daySampler, localUv);
    let edge = min(localUv, 1.0 - localUv);
    let blend = material.detailBlend * smoothstep(0.0, 0.025, min(edge.x, edge.y));
    let albedo = srgbToLinear(mix(textureSample(baseMap, baseSampler, v.globalUv).rgb, sampleColour.rgb, blend));
    var normal = normalize(v.normal); let sigmaX = dpdx(v.position); let sigmaY = dpdy(v.position);
    let height = terrain(v.uv, v.globalUv); // Derivative bump mapping from the relief.
    let gradient = vec2f(terrain(v.uv + dpdx(v.uv), v.globalUv + dpdx(v.globalUv)) - height, terrain(v.uv + dpdy(v.uv), v.globalUv + dpdy(v.globalUv)) - height);
    let r1 = cross(sigmaY, normal); let r2 = cross(normal, sigmaX); let determinant = dot(sigmaX, r1);
    normal = normalize(abs(determinant) * normal - sign(determinant) * (gradient.x * r1 + gradient.y * r2));
    let light = normalize(material.solarDirection); let view = normalize(-v.position);
    let mu = dot(normal, light); let nl = max(mu, 0.0); let nv = max(dot(normal, view), 0.0);
    let a = max(material.angularRadius, 0.00001); let x = clamp(mu / a, -1.0, 1.0); let s = sqrt(max(0.0, 1.0 - x * x));
    var irradiance = mu * (0.5 + (asin(x) + x * s) / PI) + 2.0 * a * s * s * s / (3.0 * PI); // Finite solar disc at the terminator.
    if (mu >= a) { irradiance = mu; } else if (mu <= -a) { irradiance = 0.0; }
    let lp = light - normal * mu; let vp = view - normal * nv; // Oren-Nayar diffuse.
    let gamma = max(0.0, dot(lp, vp) / max(length(lp) * length(vp), 0.00001));
    let lo = min(nl, nv); let hi = max(nl, nv);
    let rough = 0.70122 + 0.38017 * gamma * sqrt(max(0.0, 1.0 - lo * lo)) * sqrt(max(0.0, 1.0 - hi * hi)) / max(hi, 0.02);
    let aid = material.auxiliaryStrength * (material.intensity / 2.4) * (1.0 - smoothstep(-0.10, 0.0, dot(normalize(v.normal), light)));
    let hazeLight = pow(clamp((dot(normalize(v.normal), light) + 0.35) / 1.35, 0.0, 1.0), 1.3);
    let direct = mix(max(irradiance, 0.0) * mix(rough, 1.0, material.cloudSurface), hazeLight, material.haze);
    return vec4f(finish(mix(albedo, material.hazeAlbedo, material.haze) * (material.intensity * direct / PI + aid)), 1.0);
}
)";
    albert::gpu::define("albert:solid",surface);
    Program haze;haze.uniforms={{"centre",Uniform::vec3},{"radius",Uniform::f32},{"altitude",Uniform::f32},{"scale",Uniform::f32},{"layer",Uniform::f32},{"solarDirection",Uniform::vec3},
        {"intensity",Uniform::f32},{"visibility",Uniform::f32},{"inside",Uniform::f32},{"hazeAlbedo",Uniform::vec3}};
    haze.wgsl=R"(
struct Material { centre: vec3f, radius: f32, altitude: f32, scale: f32, layer: f32, solarDirection: vec3f, intensity: f32, visibility: f32, inside: f32, hazeAlbedo: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
struct Varyings { @builtin(position) clip: vec4f, @location(0) position: vec3f };
@vertex fn vs_main(@location(0) position: vec3f) -> Varyings { var o: Varyings; let view = object.modelView * vec4f(position, 1.0); o.position = view.xyz; o.clip = frame.projection * view; return o; }
const PI = 3.141592653589793; const G = 0.65;
fn phase(c: f32) -> f32 { let d = 1.0 + G * G - 2.0 * G * c; return (1.0 - G * G) / (4.0 * PI * d * sqrt(d)); }
@fragment fn fs_main(v: Varyings, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    if ((material.inside < 0.5) != front) { discard; } // One surface per ray.
    let d = normalize(v.position); let l = normalize(material.solarDirection); let t0 = dot(material.centre, d); let q = t0 * d - material.centre;
    let b = length(q); let h = b - material.radius;
    let mu = sqrt(max(0.0, 1.0 - (b / material.radius) * (b / material.radius)));
    let column = select(1.0 / max(mu, 0.05), exp(-h / material.scale) * sqrt(2.0 * PI * (material.radius + h) / material.scale), h >= 0.0); // Chapman above the limb.
    let top = exp(-pow((h - material.altitude) / material.layer, 2.0)) * sqrt(2.0 * PI * (material.radius + max(h, 0.0)) / material.layer);
    let lit = smoothstep(-0.25, 0.05, dot(select(-d, q / b, b > 0.0), l));
    let forward = 0.15 + 0.85 * phase(dot(l, d)) / phase(1.0);
    let colour = material.hazeAlbedo * (1.0 - exp(-0.025 * column)) + vec3f(0.10, 0.17, 0.36) * (1.0 - exp(-0.02 * top));
    return vec4f(finish(colour * material.intensity * material.visibility * lit * forward / PI), 1.0);
}
)";
    albert::gpu::define("albert:haze",haze);
    const char* ringVertex=R"(
struct Varyings { @builtin(position) clip: vec4f, @location(0) point: vec3f };
@vertex fn vs_main(@location(0) position: vec3f) -> Varyings { var o: Varyings; o.point = position; o.clip = frame.projection * object.modelView * vec4f(position, 1.0); return o; }
)";
    Program thin;thin.textures={"profile"};thin.uniforms={{"inner",Uniform::f32},{"outer",Uniform::f32},{"radius",Uniform::f32},{"localSun",Uniform::vec3},{"localCamera",Uniform::vec3},
        {"irradiance",Uniform::f32},{"incidence",Uniform::f32},{"gain",Uniform::f32},{"particleAlbedo",Uniform::f32},{"dustAlbedo",Uniform::f32},{"dustG",Uniform::f32}};
    thin.wgsl=std::string(R"(
struct Material { inner: f32, outer: f32, radius: f32, localSun: vec3f, localCamera: vec3f, irradiance: f32, incidence: f32, gain: f32, particleAlbedo: f32, dustAlbedo: f32, dustG: f32 };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var profile: texture_2d<f32>;
@group(2) @binding(2) var profileSampler: sampler;
)")+ringVertex+R"(
const PI = 3.141592653589793;
fn lambert(a: f32) -> f32 { return (sin(a) + (PI - a) * cos(a)) / PI; }
fn henyey(c: f32) -> f32 { let g = material.dustG; let d = 1.0 + g * g - 2.0 * g * c; return (1.0 - g * g) / (d * sqrt(d)); }
fn slab(tau: f32, mu: f32, mu0: f32, lit: bool) -> f32 { // I/F of a slab per unit albedo × phase / 4.
    if (tau <= 0.0) { return 0.0; }
    if (lit) { return mu0 / (mu + mu0) * (1.0 - exp(-tau * (1.0 / mu + 1.0 / mu0))); }
    return select(mu0 / (mu - mu0) * (exp(-tau / mu) - exp(-tau / mu0)), tau / mu * exp(-tau / mu), abs(mu - mu0) < 1e-4);
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let u = (length(v.point.xz) - material.inner) / (material.outer - material.inner);
    let tau = textureSample(profile, profileSampler, vec2f(clamp(u, 0.0, 1.0), 0.5)).rg;
    let l = normalize(material.localSun); let w = normalize(material.localCamera - v.point);
    let mu0 = max(abs(l.y), 1e-3); let mu = max(abs(w.y), 1e-3); let lit = l.y * w.y > 0.0;
    let c = clamp(dot(l, w), -1.0, 1.0); let alpha = acos(c);
    let p = v.point / material.radius; let b = dot(p, l); let q = dot(p, p) - 1.0; let disc = b * b - q; let edge = max(fwidth(disc), 1e-5);
    if (u < 0.0 || u > 1.0) { discard; }
    let shadow = 1.0 - step(0.0, -b) * smoothstep(-edge, edge, disc);
    let reflectance = material.particleAlbedo * lambert(alpha) * slab(tau.r, mu, mu0, lit) + material.dustAlbedo * henyey(-c) / 4.0 * slab(tau.g, mu, mu0, lit);
    return vec4f(finish(vec3f(reflectance * shadow * material.gain * material.irradiance / PI)), 1.0 - exp(-(tau.r + tau.g) / mu)); // Premultiplied: alpha is transmission loss.
}
)";
    albert::gpu::define("albert:thin-rings",thin);
    Program image;image.textures={"profile"};image.uniforms={{"radius",Uniform::f32},{"localSun",Uniform::vec3},{"irradiance",Uniform::f32},{"incidence",Uniform::f32}};
    image.wgsl=std::string(R"(
struct Material { radius: f32, localSun: vec3f, irradiance: f32, incidence: f32 };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var profile: texture_2d<f32>;
@group(2) @binding(2) var profileSampler: sampler;
)")+ringVertex+R"(
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let p = v.point / material.radius; let u = (length(p.xz) - 1.23) / (2.3309 - 1.23);
    let map = textureSample(profile, profileSampler, vec2f(u, 0.5));
    let l = normalize(material.localSun); let b = dot(p, l); let c = dot(p, p) - 1.0; let disc = b * b - c;
    let edge = max(fwidth(disc), 0.00001); let shadow = 1.0 - step(0.0, -b) * smoothstep(-edge, edge, disc);
    return vec4f(finish(srgbToLinear(map.rgb) * material.irradiance * (0.08 + 0.92 * sqrt(material.incidence)) * shadow / 3.14159265), map.a);
}
)";
    albert::gpu::define("albert:rings",image);
}
}
namespace {
// comets.cpp phaseFunction: Halley-Marcus phase (log table), reddish dust, CO+ ions, asinh display stretch.
const char* const cometPhase=R"(
@group(2) @binding(1) var phase: texture_2d<f32>;
@group(2) @binding(2) var phaseSampler: sampler;
const DUST = vec3f(1.06, 1.0, 0.94); const IONS = vec3f(0.22, 0.48, 1.0); const PI = 3.141592653589793;
fn dustPhase(cosine: f32) -> f32 { let deg = degrees(acos(clamp(cosine, -1.0, 1.0))); return exp(textureSampleLevel(phase, phaseSampler, vec2f((deg + 0.5) / 181.0, 0.5), 0.0).r); }
fn arcsinh(x: f32) -> f32 { return log(x + sqrt(x * x + 1.0)); }
fn shade(reflectance: f32, colour: vec3f) -> vec3f {
    let soft = material.reference * 5e-5; let enhanced = 0.15 * arcsinh(reflectance / soft) / arcsinh(material.reference / soft);
    let value = exp(mix(log(max(reflectance, 1e-30)), log(max(enhanced, 1e-30)), material.stretch));
    return colour * value * material.intensity / PI;
}
)";
void comets(){
    const std::vector<std::pair<std::string,Uniform>> shared{{"intensity",Uniform::f32},{"stretch",Uniform::f32},{"reference",Uniform::f32}};
    auto with=[&](std::vector<std::pair<std::string,Uniform>> extra){extra.insert(extra.begin(),shared.begin(),shared.end());return extra;};
    Program coma;coma.textures={"phase"};coma.uniforms=with({{"centre",Uniform::vec3},{"solar",Uniform::vec3},{"afrho",Uniform::f32},{"apex",Uniform::f32},{"radius",Uniform::f32},{"nucleus",Uniform::f32}});
    coma.wgsl=std::string(R"(
struct Material { intensity: f32, stretch: f32, reference: f32, centre: vec3f, solar: vec3f, afrho: f32, apex: f32, radius: f32, nucleus: f32 };
@group(2) @binding(0) var<uniform> material: Material;
)")+cometPhase+R"(
struct Varyings { @builtin(position) clip: vec4f, @location(0) position: vec3f };
@vertex fn vs_main(@location(0) position: vec3f) -> Varyings { var o: Varyings; let view = object.modelView * vec4f(position, 1.0); o.position = view.xyz; o.clip = frame.projection * view; return o; }
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f { // Afρ/(8b) column over the pixel footprint, fountain envelope.
    let d = normalize(v.position); let along = dot(material.centre, d); let q = along * d - material.centre; let b = length(q);
    let footprint = fwidth(b); let reach = sqrt(b * b + 0.25 * footprint * footprint);
    let ahead = 0.5 + atan2(along, reach) / PI;
    let column = material.afrho / (8.0 * reach) * ahead * exp(-b / (10.0 * material.apex));
    let parabolic = (b + dot(q, material.solar)) / (2.0 * material.apex); let envelope = 1.0 / (1.0 + parabolic * parabolic);
    let edge = 1.0 - smoothstep(0.8 * material.radius, material.radius, b);
    let hidden = select(1.0, 0.5, b < material.nucleus);
    return vec4f(finish(shade(column * envelope * edge * hidden * dustPhase(dot(material.solar, -d)), DUST)), 1.0);
}
)";
    albert::gpu::define("albert:coma",coma);
    Program ions;ions.textures={"phase"};ions.uniforms=shared;ions.attributes={{"axial",1},{"across",1}};
    ions.wgsl=std::string(R"(
struct Material { intensity: f32, stretch: f32, reference: f32 };
@group(2) @binding(0) var<uniform> material: Material;
)")+cometPhase+R"(
struct Varyings { @builtin(position) clip: vec4f, @location(0) axial: f32, @location(1) across: f32 };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) axial: f32, @location(2) across: f32) -> Varyings {
    var o: Varyings; o.axial = axial; o.across = across; o.clip = frame.projection * object.modelView * vec4f(position, 1.0); return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f { return vec4f(finish(shade(v.axial * exp(-0.5 * v.across * v.across), IONS)), 1.0); }
)";
    albert::gpu::define("albert:ions",ions);
    Program cap;cap.textures={"phase","profile"};cap.uniforms=with({{"radius",Uniform::f32},{"weight",Uniform::f32}});
    cap.wgsl=std::string(R"(
struct Material { intensity: f32, stretch: f32, reference: f32, radius: f32, weight: f32 };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(3) var profile: texture_2d<f32>;
@group(2) @binding(4) var profileSampler: sampler;
)")+cometPhase+R"(
struct Varyings { @builtin(position) clip: vec4f, @location(0) disc: vec2f };
@vertex fn vs_main(@location(0) position: vec3f) -> Varyings { // A disc facing the camera at the nucleus.
    var o: Varyings; o.disc = position.xy; let centre = object.modelView * vec4f(0.0, 0.0, 0.0, 1.0);
    o.clip = frame.projection * (centre + vec4f(position.xy * material.radius, 0.0, 0.0)); return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let rho = length(v.disc);
    let column = textureSampleLevel(profile, profileSampler, vec2f(sqrt(min(rho, 1.0)) * 63.0 / 64.0 + 0.5 / 64.0, 0.5), 0.0).r;
    if (rho > 1.0) { discard; }
    return vec4f(finish(shade(material.weight * column, IONS)), 1.0);
}
)";
    albert::gpu::define("albert:ion-cap",cap);
    Program tail;tail.textures={"phase"};tail.uniforms=with({{"solar",Uniform::vec3}});tail.attributes={{"normal",3},{"density",1},{"thickness",1}};
    tail.wgsl=std::string(R"(
struct Material { intensity: f32, stretch: f32, reference: f32, solar: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
)")+cometPhase+R"(
struct Varyings { @builtin(position) clip: vec4f, @location(0) density: f32, @location(1) thickness: f32, @location(2) world: vec3f, @location(3) normal: vec3f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) density: f32, @location(3) thickness: f32) -> Varyings {
    var o: Varyings; o.density = density; o.thickness = thickness;
    o.normal = normalize(mat3x3f(object.model[0].xyz, object.model[1].xyz, object.model[2].xyz) * normal);
    let world = object.model * vec4f(position, 1.0); o.world = world.xyz; o.clip = frame.projection * frame.view * world; return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f { // Thin dust sheet: σ·phase/4 per slant.
    let view = normalize(frame.camera.xyz - v.world);
    let slant = max(abs(dot(normalize(v.normal), view)), v.thickness);
    return vec4f(finish(shade(v.density * 0.25 * dustPhase(dot(material.solar, view)) / slant, DUST)), 1.0);
}
)";
    albert::gpu::define("albert:dust-tail",tail);
}
}
namespace {
// lighting.cpp assistedMaterial: threepp's MeshStandardMaterial lighting plus the didactic fill on
// the unlit side; rocks.cpp adds procedural mineral albedo and grain bump.
std::string assisted(bool flat,bool rock){
    std::string s=R"(
struct Material { diffuse: vec3f, opacity: f32, emissive: vec3f, roughness: f32, metalness: f32, auxiliaryStrength: f32, auxiliarySolarDirection: vec3f, doubleSided: f32, rockSeed: f32, rockScale: f32 };
@group(2) @binding(0) var<uniform> material: Material;
struct Varyings { @builtin(position) clip: vec4f, @location(0) position: vec3f, @location(1) normal: vec3f, @location(2) local: vec3f, @location(3) world: vec3f, @location(4) worldNormal: vec3f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f) -> Varyings {
    var o: Varyings; let view = object.modelView * vec4f(position, 1.0);
    o.clip = frame.projection * view; o.position = view.xyz; o.normal = object.normalView * normal; o.local = position;
    o.world = (object.model * vec4f(position, 1.0)).xyz; o.worldNormal = normalize(object.normalWorld * normal); return o;
}
fn rockHash(q: vec3f) -> f32 { var p = fract(q * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z); }
fn rockNoise(p: vec3f) -> f32 {
    let i = floor(p); var f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(rockHash(i), rockHash(i + vec3f(1, 0, 0)), f.x), mix(rockHash(i + vec3f(0, 1, 0)), rockHash(i + vec3f(1, 1, 0)), f.x), f.y),
               mix(mix(rockHash(i + vec3f(0, 0, 1)), rockHash(i + vec3f(1, 0, 1)), f.x), mix(rockHash(i + vec3f(0, 1, 1)), rockHash(i + vec3f(1, 1, 1)), f.x), f.y), f.z);
}
fn rockGrain(q: vec3f) -> f32 { // Octave fade by footprint; fwidth stays in uniform control flow (fixed loop).
    var p = q; var value = 0.0; var weight = 0.53;
    for (var i = 0; i < 4; i++) {
        let footprint = max(max(fwidth(p.x), fwidth(p.y)), fwidth(p.z));
        value += weight * mix(0.5, rockNoise(p), 1.0 - smoothstep(0.3, 1.0, footprint));
        p = p * 2.13 + vec3f(7.3, 2.6, 9.1); weight *= 0.47;
    }
    return value;
}
@fragment fn fs_main(v: Varyings, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let face = select(-1.0, 1.0, front);
    var geometryNormal = normalize(v.normal); if (material.doubleSided > 0.5) { geometryNormal = geometryNormal * face; }
)";
    if(flat)s+="    geometryNormal = flatNormal(v.position);\n";
    s+="    var albedo = material.diffuse; var n = geometryNormal;\n";
    if(rock)s+=R"(
    let mineral = rockGrain(v.local * 17.0 + vec3f(material.rockSeed));
    let grain = rockGrain(v.local * 155.0 + vec3f(material.rockSeed));
    albedo = albedo * mix(0.42, 1.25, mineral) * mix(0.70, 1.12, grain);
    let q0 = dpdx(v.position); let q1 = dpdy(v.position); // Grain bump (sign-invariant under the flipped y).
    let r0 = cross(q1, n); let r1 = cross(n, q0); let determinant = dot(q0, r0);
    n = normalize(n - sign(determinant) * (r0 * dpdx(grain) + r1 * dpdy(grain)) * (material.rockScale * 0.0012) / max(abs(determinant), 1e-20));
)";
    s+=R"(
    let air = airLight(v.world, v.worldNormal); // Sunlight and sky light under the frame's air (vehicles on the pad).
    var colour = standardLight(albedo, material.roughness, material.metalness, n, geometryNormal, v.position, air.sky * frame.sunColor.rgb, shadowFactor(v.world, v.worldNormal) * air.sun) + material.emissive;
    colour += albedo * material.auxiliaryStrength * (1.0 - smoothstep(-0.10, 0.0, dot(n, normalize(material.auxiliarySolarDirection))));
    return vec4f(finish(airApply(v.world, colour)), material.opacity);
}
)";
    return s;
}
void objects(){
    Program p;p.attributes={{"normal",3}};
    p.uniforms={{"diffuse",Uniform::vec3},{"opacity",Uniform::f32},{"emissive",Uniform::vec3},{"roughness",Uniform::f32},{"metalness",Uniform::f32},{"auxiliaryStrength",Uniform::f32},
        {"auxiliarySolarDirection",Uniform::vec3},{"doubleSided",Uniform::f32},{"rockSeed",Uniform::f32},{"rockScale",Uniform::f32}};
    p.wgsl=assisted(false,false);albert::gpu::define("albert:assisted",p);
    p.wgsl=assisted(true,false);albert::gpu::define("albert:assisted-flat",p);
    p.wgsl=assisted(false,true);albert::gpu::define("albert:rock",p);
    Program debris;debris.points=true;debris.uniforms={{"opacity",Uniform::f32},{"ink",Uniform::vec3}};
    debris.wgsl=std::string(quadCorner)+R"(
struct Material { opacity: f32, ink: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
struct Varyings { @builtin(position) clip: vec4f, @location(0) corner: vec2f };
@vertex fn vs_main(@location(0) position: vec3f, @builtin(vertex_index) i: u32) -> Varyings { // 2 px points.
    var o: Varyings; let c = quad(i); let clip = frame.projection * object.modelView * vec4f(position, 1.0);
    o.clip = vec4f(clip.xy + c * 2.0 / frame.viewport.xy * clip.w, clip.zw); o.corner = c; return o;
}
@fragment fn fs_main(v: Varyings) -> @location(0) vec4f {
    let coverage = 1.0 - smoothstep(0.12, 0.5, length(v.corner) * 0.5);
    if (coverage < 0.01) { discard; }
    return vec4f(material.ink, material.opacity * coverage);
}
)";
    albert::gpu::define("albert:debris",debris);
}
}
void albert::defineWebGpuPrograms(){stars();earthLayers();paths();solids();comets();objects();}
