// Included by webgpu_programs.cpp after the shared surface vertex program.
const char* const launchCloudShader=R"(
struct Material { bank: vec4f, extent: vec3f, source: vec3f, samples: f32 };
@group(2) @binding(0) var<uniform> material: Material;
fn cloudHash(p: vec3f) -> f32 {
    let v = bitcast<vec3u>(vec3i(p));
    var n = v.x * 1597334677u ^ v.y * 3812015801u ^ v.z * 2798796415u;
    n = (n ^ (n >> 16u)) * 2246822519u;
    return f32(n ^ (n >> 13u)) / 4294967295.0;
}
fn cloudNoise(p: vec3f) -> f32 {
    let i = floor(p); let f = fract(p); let u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(cloudHash(i), cloudHash(i + vec3f(1,0,0)), u.x), mix(cloudHash(i + vec3f(0,1,0)), cloudHash(i + vec3f(1,1,0)), u.x), u.y),
        mix(mix(cloudHash(i + vec3f(0,0,1)), cloudHash(i + vec3f(1,0,1)), u.x), mix(cloudHash(i + vec3f(0,1,1)), cloudHash(i + vec3f(1,1,1)), u.x), u.y), u.z);
}
fn launchDensity(p: vec3f, detail: bool) -> f32 {
    let h = p.z + material.extent.z * 0.45;
    let radial = p.xy / material.extent.xy;
    let r = length(radial);
    let angle = atan2(radial.y, radial.x);
    let front = 0.75 + 0.09 * sin(7.0 * angle + 0.8) + 0.065 * sin(11.0 * angle - 1.2);
    let scale = 9.0 + material.extent.z * 0.18;
    // Stable metres, gently advected, with large cauliflower lobes and a softer turbulent fringe.
    let flow = vec3f(0,0,-material.bank.x * 0.32);
    let coarse = cloudNoise((p + flow) / scale);
    var fine = coarse;
    if (detail) { fine = cloudNoise((p + flow * 1.3) / (scale * 0.43) + vec3f(17.2,3.4,8.1)); }
    let top = material.extent.z * (0.44 + 0.36 * coarse) * (1.0 - 0.35 * smoothstep(0.2,0.9,r));
    let z = (h - top * 0.3) / max(1.0,top * 0.72);
    let body = 1.0 - (r / front) * (r / front) - z * z + 0.85 * (coarse - 0.5) + 0.32 * (fine - 0.5);
    // The trench leaves two stronger lobes; the broad bank connects them across the apron.
    let trench = 0.72 + 0.28 * abs(radial.y) / max(0.01,r);
    return smoothstep(0.0,0.65,body) * (0.55 + 0.7 * coarse + 0.3 * fine) * trench * smoothstep(0.0,1.5,h);
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
    let step = (near + halfChord) / material.samples;
    let metres = length(direction * material.extent) * step;
    let up = normalize(basis[2]);
    var sun = up; if (frame.sun.w > 0.0) { sun = normalize(frame.sun.xyz); }
    let localSun = inverse * sun;
    let incident = airLight(v.world,up);
    let sunlight = incident.sun * frame.sunColor.rgb;
    let sky = incident.sky * frame.sunColor.rgb + frame.ambient.rgb;
    var transmission = 1.0; var scattered = vec3f(0.0);
    for (var i = 0; i < 32; i++) {
        if (f32(i) >= material.samples) { break; }
        let p = (middle + direction * (near - (f32(i) + 0.5) * step)) * material.extent;
        let rho = launchDensity(p,material.samples > 12.0);
        if (rho > 0.001) {
            let h = p.z + material.extent.z * 0.45;
            let shadow = launchDensity(p + localSun * (12.0 + material.extent.z * 0.16),material.samples > 20.0);
            let direct = exp(-shadow * material.bank.y * 1.5);
            let wet = material.bank.z * smoothstep(6.0,58.0,h);
            let dust = mix(vec3f(0.36,0.24,0.15),vec3f(0.57,0.53,0.47),smoothstep(2.0,45.0,h));
            let albedo = mix(dust,vec3f(0.96,0.97,0.98),wet);
            let ambient = 0.24 + 0.56 * smoothstep(0.0,material.extent.z,h);
            let lit = albedo * (sky * ambient + sunlight * (0.3 + 0.7 * direct)) * 0.32;
            let distance = length((p - material.source) / vec3f(65,65,90));
            let fire = material.bank.w * exp(-distance * distance * 1.6);
            let colour = lit + vec3f(5.0,1.15,0.12) * fire;
            let alpha = 1.0 - exp(-rho * material.bank.y * metres * 0.045);
            scattered += transmission * alpha * colour; transmission *= 1.0 - alpha;
            if (transmission < 0.008) { break; }
        }
    }
    let alpha = 1.0 - transmission;
    return vec4f(finish(airApply(v.world,scattered / max(alpha,0.00001))) * alpha,alpha);
}
)";
