#pragma once
// Camera-independent, metre-space advection. A conforming curtain and one eight/four sample wake;
// no particles, textures, stochastic sampling, screen-space noise or full-screen pass.
namespace albert {
inline const char* entryPlasmaShader=R"(
struct Material { strength: f32, upwind: vec3f, seconds: f32, quality: f32, wake: f32, leading: vec4f, extent: vec3f, span: f32 };
@group(2) @binding(0) var<uniform> material: Material;
fn entryColour(hot: f32) -> vec3f {
    return mix(vec3f(1.0, 0.065, 0.008), mix(vec3f(1.0, 0.12, 0.27), vec3f(0.48, 0.045, 1.0), hot), material.strength);
}
@fragment fn fs_main(v: Surface) -> @location(0) vec4f {
    let x = object.model[0].xyz; let y = object.model[1].xyz; let z = object.model[2].xyz;
    if (material.wake > 0.5) {
        // Draw the back boundary, integrating only the chord between the eye and it. This remains continuous inside.
        var delta = frame.camera.xyz - v.world;
        if (frame.projection[3][3] > 0.5) { delta = transpose(mat3x3f(frame.view[0].xyz, frame.view[1].xyz, frame.view[2].xyz)) * vec3f(0, 0, 1); }
        let view = normalize(vec3f(dot(delta, x) / dot(x, x), dot(delta, y) / dot(y, y), dot(delta, z) / dot(z, z)));
        let eyeDelta = frame.camera.xyz - v.world;
        let eye = v.local + vec3f(dot(eyeDelta, x) / dot(x, x), dot(eyeDelta, y) / dot(y, y), dot(eyeDelta, z) / dot(z, z));
        let chord = max(0.0, -2.0 * dot(v.local, view));
        let path = select(chord, min(chord, length(eye - v.local)), frame.projection[3][3] < 0.5);
        let count = select(4, 8, material.quality > 0.5); let step = path / f32(count);
        var energy = vec3f(0.0);
        for (var i = 0; i < count; i++) {
            let q = v.local + view * ((f32(i) + 0.5) * step);
            let aft = clamp((1.0 - q.y) * 0.5, 0.0, 1.0);
            let radius = 0.24 + 0.44 * aft;
            let bend = vec2f(sin(aft * 19.0 - material.seconds), sin(aft * 13.0 - material.seconds * 2.0)) * aft * 0.08;
            let crossSection = q.xz - bend;
            let radial = dot(crossSection, crossSection) / (radius * radius);
            let phase = aft * 68.0 - material.seconds * 5.0;
            let strands = 0.76 + 0.16 * sin(phase + q.x * 17.0) + 0.08 * sin(phase * 1.8 + q.z * 21.0);
            let density = exp(-radial * 3.0 - aft * 3.5) * smoothstep(0.0, 0.025, aft)
                * (1.0 - smoothstep(0.7, 1.0, aft)) * strands;
            energy += entryColour(0.04 + 0.75 * aft) * density * step * min(4.0, length(view * material.extent) / material.extent.x) * 1.7;
        }
        return vec4f(finish(energy * material.strength * airFilter(v.world)), 0.0);
    }
    let n = normalize(v.worldNormal); let wind = normalize(material.upwind);
    let facing = max(dot(n, wind), 0.0);
    let localWind = vec3f(dot(normalize(x), wind), dot(normalize(y), wind), dot(normalize(z), wind));
    let along = dot(v.local, localWind); let across = v.local - localWind * along;
    let phase = along * 0.85 + material.seconds * 9.0;
    let wave = phase + 1.1 * sin(dot(across, vec3f(0.73, 0.31, 1.2)) + material.seconds * 2.0);
    let footprint = fwidth(wave);
    let bands = sin(wave) * exp(-footprint * footprint);
    var curtain = 0.73 + 0.24 * bands;
    var streak = 0.0;
    if (material.quality > 0.5) {
        let fine = dot(across, vec3f(8.3, 3.7, 5.1)) + 0.8 * sin(phase);
        let ridge = max(0.0, sin(fine)); let ridge2 = ridge * ridge;
        streak = ridge2 * ridge2 * ridge2 * exp(-fwidth(fine) * fwidth(fine))
            * smoothstep(0.78, 0.99, sin(along * 0.27 + material.seconds * 3.0));
        curtain += 0.08 * sin(along * 1.785 + material.seconds * 19.0 + across.x * 1.7) * exp(-footprint * footprint * 4.4);
    }
    let nose = smoothstep(material.span * 0.23, material.span * 0.48, v.local.y);
    let edgeZ = mix(material.leading.y, material.leading.z, clamp(v.local.x / max(material.leading.x, 0.01), 0.0, 1.0));
    let leading = (1.0 - smoothstep(0.12, 0.85, abs(v.local.z - edgeZ))) * material.leading.w;
    let stagnation = facing * facing;
    let flicker = 0.96 + 0.04 * sin(material.seconds * 17.0 + along * 0.3);
    let coverage = smoothstep(0.0, 0.55, facing) * (0.20 + 0.32 * stagnation + 0.16 * nose + 0.18 * leading);
    let radiance = entryColour(0.12 + 0.65 * (1.0 - facing)) * (1.0 + 1.5 * stagnation + nose)
        + vec3f(1.0, 0.29, 0.12) * (streak * 1.8 + leading * 0.8);
    return vec4f(finish(radiance * material.strength * airFilter(v.world)) * coverage * curtain * flicker, 0.0);
}
)";
}
