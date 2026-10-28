#pragma once
// Render-only surface detail in metres. Shared frame lighting, atmosphere tables and PCF shadows
// come from gpu/programs.cpp; no textures, extra passes or displaced simulation geometry.
// vehicleHull: tile arc (signed windward face for flaps), radius, aft datum (frost: liquid top), soot reach.
// vehicleSurface: kind (hull, appendage, frost, vents), section centre and height for the vent band.
namespace albert {
inline const char* vehicleSurfaceFunctions=R"(
fn vehicleHash(q: vec2f) -> f32 {
    var p = fract(vec3f(q.xyx) * 0.1031); p += dot(p, p.yzx + 33.33); return fract((p.x + p.y) * p.z);
}
fn vehicleLine(distance: f32, halfWidth: f32, pixel: f32) -> f32 {
    let aa = max(pixel, 0.0005);
    return clamp((min(distance + aa * 0.5, halfWidth) - max(distance - aa * 0.5, -halfWidth)) / aa, 0.0, 1.0);
}
fn vehicleRepeat(x: f32, period: f32, halfWidth: f32, pixel: f32) -> f32 {
    return vehicleLine(abs(fract(x / period + 0.5) - 0.5) * period, halfWidth, pixel)
        * (1.0 - smoothstep(period * 0.16, period * 0.48, pixel));
}
fn vehicleTileEdge(y: f32) -> f32 { return 0.033 * sin(y * 1.7) + 0.012 * sin(y * 4.3); }
// Pointy hexagons, 0.294 m across flats. Two staggered rectangular grids give the nearest centre.
fn vehicleHex(q: vec2f) -> vec3f {
    let cell = vec2f(1.7320508, 3.0);
    let a = q - cell * floor(q / cell) - 0.5 * cell;
    let b = q - cell * floor((q - 0.5 * cell) / cell) - cell;
    let d = select(b, a, dot(a, a) < dot(b, b));
    let edge = 0.8660254 - max(abs(d.x), dot(abs(d), vec2f(0.5, 0.8660254)));
    return vec3f(edge, q - d);
}
// Anisotropic GGX, with the barrel axis as tangent. Smith's correlated visibility and Schlick Fresnel.
fn vehicleSpecular(n: vec3f, t: vec3f, b: vec3f, l: vec3f, v: vec3f, f0: vec3f, rough: f32, brush: f32) -> vec3f {
    let halfVector = l + v; let h = halfVector * inverseSqrt(max(dot(halfVector, halfVector), 0.000001));
    let nl = max(dot(n, l), 0.0); let nv = max(dot(n, v), 0.0001);
    let ax = max(0.015, rough * rough * (1.0 + brush));
    let ay = max(0.015, rough * rough / (1.0 + brush));
    let ht = dot(h, t) / ax; let hb = dot(h, b) / ay; let hn = max(dot(h, n), 0.0);
    let d = ht * ht + hb * hb + hn * hn;
    let lv = length(vec3f(ax * dot(v, t), ay * dot(v, b), nv));
    let ll = length(vec3f(ax * dot(l, t), ay * dot(l, b), nl));
    let f = 1.0 - clamp(dot(v, h), 0.0, 1.0); let f2 = f * f;
    let fresnel = mix(f0, vec3f(1.0), f2 * f2 * f);
    return fresnel / max(2.0 * PI_L * ax * ay * d * d * (nl * lv + nv * ll), 0.000001);
}
// Local plasma irradiance on the exposed face, separate from tile emission. No scene light slots.
fn vehicleEntryLight(worldNormal: vec3f, world: vec3f, albedo: vec3f, metal: f32) -> vec3f {
    let facing = dot(normalize(worldNormal), material.entryLight.xyz);
    let exposed = smoothstep(-0.35, 0.65, facing);
    let colour = mix(vec3f(1.0, 0.095, 0.018), vec3f(1.0, 0.12, 0.38), material.entryLight.w);
    let reflected = reflect(-normalize(frame.camera.xyz - world), normalize(worldNormal));
    let broadReflection = 0.3 + 0.7 * smoothstep(-0.6, 0.8, dot(reflected, material.entryLight.xyz));
    return colour * material.entryLight.w * exposed * (albedo * (1.0 - metal) + vec3f(0.18 * broadReflection) * metal) * 1.6;
}
// The same physical sky as the background, sampled at vertices and interpolated. Roughness blends
// toward hemispherical irradiance in the fragment. Ground uses the atmosphere's mean Lambert albedo.
fn vehicleEnvironment(world: vec3f, reflected: vec3f) -> vec3f {
    var radiance = frame.ambient.rgb / PI_L;
    if (frame.air.w > 0.0) {
        let p = airPoint(world);
        let ray = normalize((frame.planet * vec4f(reflected, 0.0)).xyz);
        let atmosphereSky = skyRadiance(p, ray, frame.airSun.xyz);
        var directional = atmosphereSky.radiance;
        if (skyHitsGround(p, ray)) {
            let entry = airEntry(p, ray); let r = max(entry.r, bottomRadius());
            let mu = clamp(entry.rmu / r, -1.0, 1.0);
            let distance = distanceToBottom(r, mu);
            let ground = normalize(entry.camera + ray * distance);
            let muS = dot(ground, frame.airSun.xyz);
            let light = max(muS, 0.0) * transmittanceToSun(airTransmittance, airSampler, bottomRadius(), muS) * frame.sun.w
                + lookupIrradiance(airIrradiance, airSampler, bottomRadius(), muS);
            directional += atmosphere.groundAlbedo.rgb * light / PI_L;
        }
        radiance += directional * frame.sunColor.rgb * frame.air.w;
    }
    return radiance;
}
)";
inline const char* vehicleSurfaceFragment=R"(
    // A hull-local signed plane avoids subtracting large world positions at the waterline.
    let waterHeight = v.waterHeight;
    let inWater = dot(material.waterPlane.xyz,material.waterPlane.xyz) > 0.000001;
    if(inWater && waterHeight < -0.18){discard;}
    let wet = select(0.0,1.0-smoothstep(0.0,2.4,waterHeight),inWater);
    albedo *= 1.0-wet*0.42;
    let kind = material.vehicleSurface.x; let quality = material.vehicleQuality;
    let angle = atan2(v.local.x, v.local.z + select(0.0, 0.0001, dot(v.local.xz, v.local.xz) < 0.00000001));
    let radius = max(length(v.local.xz), 0.01);
    var q = vec2f(angle * radius, v.local.y);
    if (kind == 1.0) { q = v.local.xz; }
    if (kind == 2.0) { q.y = v.local.y * material.frostHeight; }
    let pixel = max(fwidth(q), vec2f(0.0001));
    let detail = quality * (1.0 - smoothstep(0.08, 0.32, max(pixel.x, pixel.y)));
    var tileAlbedo = vec3f(0.036);
    // Each tile glazed (a faint sheen, its own) and seated a hair off flush: under the sun each catches it a little differently,
    // which is what shows their hexagons on the real ship; near-black and even, a rare darker replacement.
    var tileTilt = vec2f(0.0); var tileRough = 0.88;
    if (quality > 0.5) {
        let hex = vehicleHex(q / 0.17);
        let gutter = vehicleLine(hex.x, 0.018, max(pixel.x, pixel.y) / 0.17);
        let tileSeed = vehicleHash(hex.yz);let tileSeed2 = vehicleHash(hex.yz + vec2f(17.3, 5.1));
        let tileShade = mix(0.029, 0.043, tileSeed) * select(1.0, 0.75, tileSeed < 0.003);
        let missing = select(0.0, 1.0, tileSeed > 0.9985);
        let resolved = mix(vec3f(tileShade), vec3f(0.017, 0.012, 0.009), missing) * (1.0 - 0.68 * gutter);
        tileAlbedo = mix(tileAlbedo, resolved, detail);
        tileTilt = (vec2f(tileSeed, tileSeed2) - 0.5) * 2.0 * detail * (1.0 - gutter);
        tileRough = mix(0.88, mix(0.5, 0.66, tileSeed2) + 0.32 * gutter, detail);
    }
    let hullY = select(v.local.y, q.y + material.vehicleHull.z, kind == 2.0);
    let boundary = material.vehicleHull.x * 0.5 + vehicleTileEdge(hullY);
    // Differentiate atan analytically: its -pi/pi wrap must not paint tiles down the leeward seam.
    let angularGradient = vec2f(v.local.z, -v.local.x) / (radius * radius);
    let angularPixel = max(abs(dot(angularGradient, dpdx(v.local.xz))) + abs(dot(angularGradient, dpdy(v.local.xz))), 0.001);
    var tile = (1.0 - smoothstep(-angularPixel, angularPixel, abs(angle) - boundary)) * select(0.0, 1.0, material.vehicleHull.x > 0.0);
    if (material.vehicleHull.x >= 6.28318) { tile = 1.0; }
    if (kind == 1.0) {
        let faceNormal = dot(normalize(v.worldNormal), normalize(object.normalWorld * vec3f(0, 1, 0)));
        tile = select(0.0, smoothstep(0.1, 0.8, faceNormal * sign(material.vehicleHull.x)), abs(material.vehicleHull.x) > 0.0);
    }
    if (quality < 0.5) { // Uniform fast path: the existing isotropic lighting, without bump, tangent or directional LUTs.
        let base = 1.0 - smoothstep(0.0, max(material.vehicleHull.w, 0.01), v.local.y - material.vehicleHull.z);
        let soot = base * 0.56 * material.vehicleSoot * select(0.0, 1.0, kind == 0.0 && material.vehicleHull.w > 0.0);
        var simple = mix(material.diffuse, vec3f(0.035, 0.029, 0.024), soot);
        var metal = material.metalness * (1.0 - soot); var rough = material.roughness + 0.4 * soot;
        var alpha = material.opacity;
        if (kind == 3.0) {
            let rim = max(vehicleRepeat(q.x, 6.2831853 * material.vehicleHull.y / 20.0, 0.055, pixel.x),
                smoothstep(material.vehicleSurface.z * 0.5 - 0.17, material.vehicleSurface.z * 0.5 - 0.08, abs(v.local.y - material.vehicleSurface.y)));
            simple = mix(vec3f(0.006), material.diffuse, rim); metal = rim; rough = mix(0.95, 0.4, rim);
        }
        if (kind == 1.0) {
            let edge = 1.0 - smoothstep(0.12, 0.45, abs(dot(normalize(v.worldNormal), normalize(object.normalWorld * vec3f(0, 1, 0)))));
            simple *= 1.0 + 0.7 * edge; rough = mix(rough, 0.25, edge * 0.65);
        }
        simple = mix(simple, vec3f(0.036), tile); rough = mix(rough, 0.88, tile); metal = mix(metal, 0.0, tile);
        if (kind == 2.0) {
            alpha *= (1.0 - smoothstep(-0.24, -0.015, q.y)) * smoothstep(-material.frostHeight, -material.frostHeight + 0.12, q.y) * (1.0 - tile);
            simple = material.diffuse * 0.93; metal = 0.0; rough = 0.96;
        }
        simple *= 1.0-wet*0.42; rough=mix(rough,0.19,wet);
        let air = airLight(v.world, v.worldNormal);
        var colour = standardLight(simple, rough, metal, n, geometryNormal, v.position, air.sky * frame.sunColor.rgb, shadowFactor(v.world, v.worldNormal) * air.sun);
        colour += material.emissive * select(1.0, tile, abs(material.vehicleHull.x) > 0.0 && (kind == 0.0 || kind == 1.0));
        colour += vehicleEntryLight(v.worldNormal, v.world, simple, metal);
        colour += simple * material.auxiliaryStrength * (1.0 - smoothstep(-0.10, 0.0, dot(n, normalize(material.auxiliarySolarDirection))));
        return vec4f(finish(airApply(v.world, colour)), alpha);
    }
    let frostGrain = sin(q.x * 73.0) * sin(q.y * 67.0) * exp(-dot(pixel, pixel) * 2500.0) * quality;
    // Longitudinal welds stagger between the 1.85 m steel rings. Filter periodic lines before they go subpixel.
    var ring = 0.0; var weld = 0.0; var grain = 0.0;
    if (quality > 0.5) {
        ring = vehicleRepeat(q.y - material.vehicleHull.z, 1.85, 0.011, pixel.y);
        let row = floor((q.y - material.vehicleHull.z) / 1.85);
        weld = vehicleRepeat(q.x + 0.64 * sin(row * 2.4), 2.8, 0.006, pixel.x) * detail;
        grain = sin(q.y * 2200.0 + sin(q.x * 9.0)) * exp(-pixel.y * pixel.y * 2200.0 * 2200.0);
    }
    let streak = 0.72 + 0.18 * sin(q.x * 3.1 + sin(q.y * 0.7)) + 0.1 * sin(q.x * 7.7);
    let base = 1.0 - smoothstep(0.0, max(material.vehicleHull.w, 0.01), v.local.y - material.vehicleHull.z);
    let soot = base * streak * material.vehicleSoot * select(0.0, 0.78, kind == 0.0 && material.vehicleHull.w > 0.0);
    albedo *= 1.0 - 0.24 * ring - 0.08 * weld + 0.025 * grain;
    albedo = mix(albedo, vec3f(0.035, 0.029, 0.024), soot);
    var rough = material.roughness + 0.1 * ring + 0.06 * weld + 0.4 * soot;
    var metal = material.metalness * (1.0 - soot);
    if (kind == 3.0) {
        let frame = max(vehicleRepeat(q.x, 6.2831853 * material.vehicleHull.y / 20.0, 0.055, pixel.x),
            smoothstep(material.vehicleSurface.z * 0.5 - 0.17, material.vehicleSurface.z * 0.5 - 0.08, abs(v.local.y - material.vehicleSurface.y)));
        albedo = mix(vec3f(0.006), material.diffuse, frame); metal = mix(0.0, 1.0, frame); rough = mix(0.95, 0.4, frame);
    }
    if (kind == 1.0) { // The thin plate/bar faces catch light along their edges without another outline pass.
        let edge = 1.0 - smoothstep(0.12, 0.45, abs(dot(normalize(v.worldNormal), normalize(object.normalWorld * vec3f(0, 1, 0)))));
        albedo *= 1.0 + 0.7 * edge; rough = mix(rough, 0.25, edge * 0.65);
    }
    albedo = mix(albedo, tileAlbedo, tile); rough = mix(rough, tileRough, tile); metal = mix(metal, 0.0, tile);
    var alpha = material.opacity;
    if (kind == 2.0) {
        let edge = q.y + 0.045 * sin(q.x * 13.0) + 0.025 * sin(q.x * 31.0);
        alpha *= (1.0 - smoothstep(-0.24, -0.015, edge)) * smoothstep(-material.frostHeight, -material.frostHeight + 0.12, q.y) * (1.0 - tile);
        albedo = material.diffuse * (0.93 + 0.07 * frostGrain);
        rough = 0.96; metal = 0.0;
    }
    // Millimetre relief only in shading, faded before differentiation to avoid distant sparkling.
    let relief = (0.0012 * grain + 0.001 * frostGrain * select(0.0, 1.0, kind == 2.0)) * detail;
    let viewRelief = relief * length(object.modelView[0].xyz); // Metres, even when the scene uses Earth radii.
    n = bumpNormal(v.position, n, dpdx(viewRelief), dpdy(viewRelief), 1.0);
    let axis = normalize(object.modelView[1].xyz);
    var tangent = axis - n * dot(axis, n);
    if (dot(tangent, tangent) < 0.0001) { tangent = cross(n, vec3f(0.0, 0.0, 1.0)); }
    if (dot(tangent, tangent) < 0.0001) { tangent = cross(n, vec3f(0.0, 1.0, 0.0)); }
    tangent = normalize(tangent); let bitangent = normalize(cross(n, tangent));
    n = normalize(n + (tangent * tileTilt.x + bitangent * tileTilt.y) * 0.035 * tile); // About 2 degrees at most.
    let normalPixel = max(abs(dpdx(geometryNormal)), abs(dpdy(geometryNormal)));
    rough = clamp(rough + max(max(normalPixel.x, normalPixel.y), normalPixel.z), 0.12, 1.0);
    rough = mix(rough,0.19,wet);
    let eye = viewDirection(v.position); let l = sunView();
    let f0 = mix(vec3f(0.04), albedo, metal);
    let f = 1.0 - clamp(dot(n, eye), 0.0, 1.0); let f2 = f * f;
    let fresnel = mix(f0, vec3f(1.0), f2 * f2 * f);
    let air = airLight(v.world, v.worldNormal);
    var environment = (frame.ambient.rgb + air.sky * frame.sunColor.rgb) / PI_L;
    if (metal > 0.05) { environment = mix(v.environment, environment, rough * rough); }
    let diffuse = albedo * (1.0 - metal);
    let sun = shadowFactor(v.world, v.worldNormal) * air.sun * frame.sunColor.rgb * frame.sun.w * max(dot(n, l), 0.0);
    let brush = 0.9 * (1.0 - tile) * select(0.0, 1.0, kind == 0.0) * quality;
    var colour = sun * (diffuse / PI_L + vehicleSpecular(n, tangent, bitangent, l, eye, f0, rough, brush));
    colour += localLight(n, v.position, diffuse, f0, rough);
    colour += siteIrradiance(v.world) * (diffuse + f0 * 0.65) / PI_L;
    colour += contactAmbient(v.position) * (frame.ambient.rgb + air.sky * frame.sunColor.rgb) * diffuse / PI_L;
    colour += contactAmbient(v.position) * environment * fresnel * (1.0 - 0.45 * rough);
    colour += material.emissive * mix(vec3f(1.0), tileAlbedo / 0.036, tile * 0.65) * select(1.0, tile, abs(material.vehicleHull.x) > 0.0 && (kind == 0.0 || kind == 1.0));
    colour += vehicleEntryLight(v.worldNormal, v.world, albedo, metal);
    colour += albedo * material.auxiliaryStrength * (1.0 - smoothstep(-0.10, 0.0, dot(n, normalize(material.auxiliarySolarDirection))));
    return vec4f(finish(airApply(v.world, colour)), alpha);
)";
}
