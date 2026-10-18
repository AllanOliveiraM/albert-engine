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
    return vec4f(linearToSrgb(vec3f(1.0, 0.96, 0.88) * intensity * (0.4 + 0.6 * mu)), 1.0);
}
)";
    albert::gpu::define("albert:sun",sun);
    Program corona;corona.attributes=surfaceAttributes;
    corona.wgsl=std::string(surface)+R"(
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let mu = abs(dot(normalize(v.worldNormal), normalize(frame.camera.xyz - v.world)));
    let impact = sqrt(max(0.0, 1.0 - mu * mu)) * 1.15;
    let glow = exp(-max(0.0, impact - 1.0) * 24.0) * (1.0 - smoothstep(1.04, 1.15, impact));
    return vec4f(linearToSrgb(vec3f(1.0, 0.94, 0.83)), glow * 0.10);
}
)";
    albert::gpu::define("albert:corona",corona);
    Program clouds;clouds.attributes=surfaceAttributes;clouds.textures={"cloudMap","detailMap","previousDetailMap"};
    clouds.uniforms=cloudUniforms;for(auto u:{std::pair{"solarDirection",Uniform::vec3},{"moonPosition",Uniform::vec3},{"eclipseSunPosition",Uniform::vec3},{"eclipseEnabled",Uniform::f32},{"visibility",Uniform::f32}})clouds.uniforms.emplace_back(u.first,u.second);
    clouds.wgsl=std::string(surface)+"struct Material { "+cloudFields+", solarDirection: vec3f, moonPosition: vec3f, eclipseSunPosition: vec3f, eclipseEnabled: f32, visibility: f32 };\n@group(2) @binding(0) var<uniform> material: Material;\n"
        +cloudBindings(0)+cloudSampling+visibleSun+R"(
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let density = cloudDensity(v.uv, dpdx(v.uv), dpdy(v.uv));
    let light = max(0.0, dot(normalize(v.worldNormal), material.solarDirection));
    let transmission = mix(1.0, visibleSun(v.world, material.eclipseSunPosition, 695700.0 / 6378.137, material.moonPosition, 1737.4 / 6378.137), material.eclipseEnabled);
    return vec4f(finish(vec3f(1.0, 0.98, 0.95) * (0.003 + 2.4 * light * transmission)), density * 0.88 * material.visibility);
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
    return vec4f(finish(lights * night * transmission * 1.8), material.visibility);
}
)";
    albert::gpu::define("albert:night",night);
    Program air;air.attributes=surfaceAttributes;air.textures={"opticalDepthMap"};air.uniforms={{"localCamera",Uniform::vec3},{"visibility",Uniform::f32},{"localSun",Uniform::vec3}};
    air.wgsl=std::string(surface)+R"(
struct Material { localCamera: vec3f, visibility: f32, localSun: vec3f };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var opticalDepthMap: texture_2d<f32>;
@group(2) @binding(2) var opticalSampler: sampler;
const outerRadius = 1.012543; const metres = 6378137.0;
const betaR = vec3f(5.802e-6, 13.558e-6, 33.100e-6); const betaM = vec3f(21.0e-6);
fn sphere(origin: vec3f, ray: vec3f, radius: f32) -> vec2f {
    let b = dot(origin, ray); let d = b * b - dot(origin, origin) + radius * radius;
    if (d < 0.0) { return vec2f(1e20, -1e20); }
    let root = sqrt(d); return vec2f(-b - root, -b + root);
}
fn density(p: vec3f) -> vec2f { let h = max(0.0, (length(p) - 1.0) * metres); return exp(-h / vec2f(8000.0, 1200.0)); }
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let ray = normalize(v.local - material.localCamera);
    let air = sphere(material.localCamera, ray, outerRadius);
    let begin = max(air.x, 0.0); var end = air.y;
    let ground = sphere(material.localCamera, ray, 1.0);
    if (ground.x > 0.0 && ground.x < end) { end = ground.x; }
    if (end <= begin) { discard; }
    let stepLength = (end - begin) / 8.0;
    var optical = vec2f(0.0); var sumR = vec3f(0.0); var sumM = vec3f(0.0);
    for (var i = 0; i < 8; i++) {
        let p = material.localCamera + ray * (begin + (f32(i) + 0.5) * stepLength);
        let segment = density(p) * stepLength * metres;
        optical += segment * 0.5;
        let blocked = sphere(p, material.localSun, 1.0);
        if (blocked.x <= 0.0 || blocked.y <= 0.0) {
            let muSun = dot(normalize(p), material.localSun); let altitude = max(0.0, (length(p) - 1.0) * metres);
            var lookup = vec2f(muSun * 0.5 + 0.5, sqrt(clamp(altitude / 80000.0, 0.0, 1.0)));
            lookup = (lookup * vec2f(255.0, 127.0) + 0.5) / vec2f(256.0, 128.0); // Texel centres keep the horizon.
            let depth = optical + textureSampleLevel(opticalDepthMap, opticalSampler, lookup, 0.0).rg;
            let attenuation = exp(-betaR * depth.x - betaM * 1.1 * depth.y);
            sumR += attenuation * segment.x; sumM += attenuation * segment.y;
        }
        optical += segment * 0.5;
    }
    let mu = dot(ray, material.localSun);
    let phaseR = 0.0596831 * (1.0 + mu * mu); let g = 0.76;
    let phaseM = 0.0795775 * (1.0 - g * g) / pow(max(0.01, 1.0 + g * g - 2.0 * g * mu), 1.5);
    return vec4f(finish(2.5 * (betaR * phaseR * sumR + betaM * phaseM * sumM)), material.visibility);
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
void albert::defineWebGpuPrograms(){stars();earthLayers();}
