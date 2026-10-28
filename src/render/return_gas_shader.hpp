#pragma once
namespace albert {
inline const char* returnGasShader=R"(
struct Material { kind: f32, extent: vec3f, state: vec4f, body: vec4f, offset: vec3f, wind: vec3f, source: vec3f, seconds: f32, samples: f32,
    tip0: vec4f, tip1: vec4f, tip2: vec4f, tip3: vec4f, tip4: vec4f, tip5: vec4f, vent0: vec4f, vent1: vec4f, vent2: vec4f, vent3: vec4f };
@group(2) @binding(0) var<uniform> material: Material;
fn gasHash(p: vec3f) -> f32 {
    let v = bitcast<vec3u>(vec3i(p));
    var n = v.x * 1597334677u ^ v.y * 3812015801u ^ v.z * 2798796415u;
    n = (n ^ (n >> 16u)) * 2246822519u;
    return f32(n ^ (n >> 13u)) / 4294967295.0;
}
fn gasNoise(p: vec3f) -> f32 {
    let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(gasHash(i),gasHash(i + vec3f(1,0,0)),u.x),mix(gasHash(i + vec3f(0,1,0)),gasHash(i + vec3f(1,1,0)),u.x),u.y),
        mix(mix(gasHash(i + vec3f(0,0,1)),gasHash(i + vec3f(1,0,1)),u.x),mix(gasHash(i + vec3f(0,1,1)),gasHash(i + vec3f(1,1,1)),u.x),u.y),u.z);
}
// First intersection with the finite steel hull. Stop gas there instead of painting the far half over the metal.
fn hullDistance(p: vec3f, d: vec3f) -> f32 {
    let r = material.body.x; let half = material.body.y * 0.5;
    let a = dot(d.xz,d.xz); let b = dot(p.xz,d.xz); let c = dot(p.xz,p.xz) - r * r;
    var lo = -1e8; var hi = 1e8;
    if (a > 0.000001) {
        let discriminant = b * b - a * c;
        if (discriminant < 0.0) { return 1e8; }
        let root = sqrt(discriminant); lo = (-b - root) / a; hi = (-b + root) / a;
    } else if (c > 0.0) { return 1e8; }
    if (abs(d.y) > 0.000001) {
        let ends = vec2f(-half - p.y,half - p.y) / d.y;
        lo = max(lo,min(ends.x,ends.y)); hi = min(hi,max(ends.x,ends.y));
    } else if (abs(p.y) > half) { return 1e8; }
    return select(1e8,max(0.0,lo),hi >= max(0.0,lo));
}
fn gasVisible(p: vec3f, view: vec3f) -> f32 {
    return select(0.0,1.0,hullDistance(p + material.offset + view * 0.03,view) > 1e7);
}
// Analytic Gaussian filaments stay continuous when their width is smaller than a volume integration step.
fn filament(ray: vec3f, view: vec3f, start: vec3f, end: vec3f, radius: f32) -> f32 {
    let axis = end - start; let offset = start - ray;
    let crossAxis = axis - view * dot(axis,view);
    let crossOffset = offset - view * dot(offset,view);
    let t = clamp(-dot(crossOffset,crossAxis) / max(0.001,dot(crossAxis,crossAxis)),0.0,1.0);
    let d = crossOffset + crossAxis * t;
    return exp(-dot(d,d) / (radius * radius)) * (0.4 + 0.6 * sin(3.14159 * t)) * gasVisible(start + axis * t,view);
}
// Resolve the thin cone at its two exact ray crossings; a sparse march would blur or miss its leading face.
fn condensation(ray: vec3f, view: vec3f) -> f32 {
    let r = material.body.x; let axis = -normalize(material.wind);
    let centre = vec3f(0,material.body.z,0) - axis * (r * 0.45);
    let p = ray - centre; let height = r * 1.8; let slope = 0.65;
    let h = dot(p,axis); let dh = dot(view,axis);
    let cross = p - axis * h; let dc = view - axis * dh;
    let radius = r * 1.025 + h * slope;
    let a = dot(dc,dc) - slope * slope * dh * dh;
    let b = dot(cross,dc) - radius * slope * dh;
    let c = dot(cross,cross) - radius * radius;
    let discriminant = b * b - a * c;
    let aa = max(0.06,fwidth(h));
    if (discriminant <= 0.0 || abs(a) < 0.00001) { return 0.0; }
    let root = sqrt(discriminant);
    let roots = vec2f(-b - root,-b + root) / a;
    var optical = 0.0;
    for (var i = 0; i < 2; i++) {
        let point = p + view * roots[i]; let along = dot(point,axis);
        let leading = smoothstep(-aa,aa,along);
        let tail = 1.0 - smoothstep(height * 0.18,height,along);let trailing = tail * tail;
        let normal = normalize(point - axis * along - axis * (slope * (r * 1.025 + slope * along)));
        let column = 0.40 / max(0.22,abs(dot(normal,view)));
        optical += leading * trailing * column * gasVisible(point + centre,view);
    }
    return optical;
}
@fragment fn fs_main(v: Surface, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let basis = mat3x3f(object.model[0].xyz,object.model[1].xyz,object.model[2].xyz);
    let inverse = transpose(mat3x3f(normalize(basis[0]),normalize(basis[1]),normalize(basis[2])));
    let units = length(basis[0]) / material.extent.x;
    let eye = inverse * (frame.camera.xyz - v.world) / units / material.extent + v.local;
    let inside = frame.projection[3][3] < 0.5 && dot(eye,eye) < 1.0;
    if (front == inside) { discard; }
    var delta = frame.camera.xyz - v.world;
    if (frame.projection[3][3] > 0.5) { delta = transpose(mat3x3f(frame.view[0].xyz,frame.view[1].xyz,frame.view[2].xyz)) * vec3f(0,0,1); }
    let direction = normalize(inverse * delta / material.extent);
    let middle = v.local - direction * dot(v.local,direction);
    let halfChord = sqrt(max(0.0,1.0 - dot(middle,middle)));
    let near = select(halfChord,clamp(dot(eye,direction),-halfChord,halfChord),inside);
    let up = normalize(basis[1]); let incident = airLight(v.world,up);
    let lighting = (incident.sun * frame.sunColor.rgb + incident.sky * frame.sunColor.rgb + frame.ambient.rgb) * 0.28;
    if (material.kind > 1.5) {
        let ray = middle * material.extent; let view = normalize(direction * material.extent);
        let r = material.body.x; let l = material.body.y;
        let wind = normalize(material.wind); let aft = -wind;
        let tips = array<vec4f,6>(material.tip0,material.tip1,material.tip2,material.tip3,material.tip4,material.tip5);
        var density = 0.0;
        for (var i = 0; i < 6; i++) {
            if (i >= 3 && material.samples < 12.0) { break; }
            let tip = tips[i];
            density += tip.w * (material.state.y * 0.10 + material.state.x * 0.85) * filament(ray,view,tip.xyz,tip.xyz + aft * (4.0 + 4.0 * material.state.x),0.12 + 0.18 * material.state.x);
        }
        let vents = array<vec4f,4>(material.vent0,material.vent1,material.vent2,material.vent3);
        for (var i = 0; i < 4; i++) {
            if (i >= 2 && material.samples < 12.0) { break; }
            // Pressure-managed cold gas: short independent pulses, advected away from each outlet.
            let phase = fract(material.seconds * 0.25 + f32(i) * 0.271);
            let age = phase * 4.0;
            let vent = vents[i]; let outward = normalize(vec3f(vent.x,0,vent.z) + vec3f(0.0001,0,0));
            let start = vent.xyz + outward * (0.5 + age * 2.8) + aft * age * 3.0;
            let end = start + outward * (1.0 + age * 2.0);
            let pulse = smoothstep(0.0,0.12,age) * exp(-age * 0.8);
            density += vent.w * material.state.z * pulse * 3.0 * filament(ray,view,start,end,0.55 + age * 1.4);
        }
        let flicker = 0.86 + 0.09 * sin(material.seconds * 43.0) + 0.05 * sin(material.seconds * 17.0);
        density += material.state.x * condensation(ray,view) * flicker * 2.6;
        // Retained cold inventory after the burn vents at both ends while the nearly stationary hull is held.
        let age = fract(material.seconds * 0.125) * 8.0;
        let drift = vec3f(age * 1.5,0,0);
        let fade = smoothstep(0.0,0.2,age) * exp(-age * 0.5) * (1.0 - smoothstep(6.0,8.0,age));
        density += material.body.w * fade * 0.8 * filament(ray,view,vec3f(r,-l * 0.48,0) + drift,vec3f(r + 10.0,-l * 0.45,0) + drift,2.0 + age * 0.2);
        let alpha = 1.0 - exp(-density * 1.7);
        let white = lighting * mix(1.0,3.0,material.state.x);
        return vec4f(finish(white * vec3f(0.98,0.99,1.0) * airFilter(v.world)) * alpha,alpha);
    }
    if (material.kind > 0.5) {
        let view = normalize(direction * material.extent);
        let start = (middle + direction * near) * material.extent;
        let distance = min((near + halfChord) * length(direction * material.extent),hullDistance(start + material.offset,-view));
        let step = distance / material.samples; let r = material.body.x;
        var emission = vec3f(0.0); var ignition = vec3f(0.0); var transmission = 1.0;
        for (var i = 0; i < 12; i++) {
            if (f32(i) >= material.samples) { break; }
            let p = start - view * ((f32(i) + 0.5) * step);
            let relative = p - material.source; let h = relative.y; let rise = max(0.0,h); let radial = length(relative.xz);
            let flash = material.state.x; let collar = material.state.y;
            let bay = exp(-radial * radial / (r * r * (1.6 + 5.0 * flash)) - h * h / (r * r * 3.0));
            // Rotated, advected metre-space noise has no angular/axial harmonics to form a lattice on the hull.
            let advected = (relative - vec3f(0,material.seconds * 6.0,0)) / (r * 0.9);
            let rotated = vec3f(dot(advected,vec3f(0.80,0.36,0.48)),dot(advected,vec3f(-0.48,0.86,0.18)),dot(advected,vec3f(-0.35,-0.37,0.86)));
            let coarse = gasNoise(rotated);
            var billow = coarse;
            if (material.samples > 6.0) { billow = coarse * 0.75 + gasNoise(rotated * 2.1 + vec3f(7,19,3)) * 0.25; }
            let edge = r * (1.35 + 0.60 * exp(-h * h / (r * r * 2.0)) + 0.65 * smoothstep(0.0,r * 4.0,rise) + (coarse - 0.5) * 0.55);
            let width = r * (0.26 + rise / r * 0.055);
            let shroud = exp(-pow((radial - edge) / width,2.0)) * exp(-rise / (r * 2.8)) * smoothstep(-r * 1.5,0.0,h) * (0.4 + billow);
            let free = smoothstep(r * 1.01,r * 1.12,length((p + material.offset).xz));
            let gas = shroud * collar * free;
            let heat = exp(-rise / (r * 1.9));
            let orange = mix(vec3f(0.70,0.012,0.002),vec3f(3.5,0.38,0.015),smoothstep(0.12,0.60,heat));
            let colour = mix(orange,vec3f(5.5,3.0,0.45),smoothstep(0.60,1.0,heat)) * pow(heat,1.4);
            let fire = bay * flash * 3.0;
            let flashColour = mix(vec3f(2.0,0.28,0.015),vec3f(5.5,3.0,0.45),exp(-abs(h) / r));
            let smoky = lighting * vec3f(0.035,0.030,0.028) * (1.0 - heat);
            ignition += fire * flashColour * step * 1.6 / length(direction * material.extent); // Preserve the accepted flash's radiance scale.
            emission += transmission * gas * (colour + smoky) * step * 1.6 / r;
            transmission *= exp(-(fire * 0.08 / length(direction * material.extent) + gas * 0.30 / r) * step);
        }
        return vec4f(finish((ignition + emission) * airFilter(v.world)),1.0 - transmission);
    }
    let step = (near + halfChord) / material.samples;
    var emission = vec3f(0.0); var optical = 0.0;
    for (var i = 0; i < 12; i++) {
        if (f32(i) >= material.samples) { break; }
        let q = middle + direction * (near - (f32(i) + 0.5) * step);
        let p = q * material.extent; let radial = length(p.xz - material.source.xz);
        let ripple = 0.85 + 0.10 * sin(q.y * 24.0 + q.x * 11.0 - material.seconds * 2.0) + 0.05 * sin(q.z * 27.0 + material.seconds * 3.0);
        if (material.kind < 0.5) {
            let down = (material.source.y - p.y) / (material.extent.y * 1.85);
            let width = material.extent.x * (0.018 + 0.84 * sin(clamp(down,0.0,1.0) * 3.14159) * sqrt(max(0.0,down)));
            let rho = exp(-radial * radial / max(1.0,width * width) * 2.6) * exp(-down * 1.8)
                * smoothstep(0.0,0.015,down) * (1.0 - smoothstep(0.65,1.0,down)) * ripple * material.state.x * material.state.y;
            let colour = mix(vec3f(0.60,0.78,1.0),vec3f(1.0,0.53,0.24),smoothstep(0.02,0.5,down));
            emission += rho * colour * (vec3f(0.055) + lighting * 1.8) * step * 2.2;
            optical += rho * step * 0.12;
        }
    }
    let alpha = 1.0 - exp(-optical);
    return vec4f(finish(emission * airFilter(v.world)),alpha);
}
)";
}
