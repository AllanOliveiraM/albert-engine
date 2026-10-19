#include "gpu/clouds.hpp"
#include "gpu/atmosphere.hpp"
#include "gpu/programs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <array>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace albert::gpu {
namespace {
constexpr unsigned shapeSize=64,detailSize=32;
WGPUStringView sv(const std::string& s){return {s.data(),s.size()};}
WGPUStringView sv(const char* s){return {s,std::strlen(s)};} // Not through a temporary string.
// Periodic noise over a tile, made once on the CPU (~0.1 s): the shape, Perlin dilated by Worley (Schneider, The real-time
// volumetric cloudscapes of Horizon Zero Dawn, 2015) at 4 to 32 periods a tile, equalised to a uniform distribution so a
// threshold of 1 - cover leaves that fraction cloudy; the detail, Worley fBm at 2, 4 and 8 cells.
std::uint32_t mix32(std::uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
float random(int x,int y,int z,int period,std::uint32_t seed){ // [0, 1), periodic.
    auto w=[&](int c){return static_cast<std::uint32_t>((c%period+period)%period);};
    return static_cast<float>(mix32(w(x)+0x9e3779b9u*(w(y)+0x85ebca6bu*(w(z)+0xc2b2ae35u*seed)))>>8)/16777216.f;}
float worley(const float p[3],int cells,std::uint32_t seed){ // 1 - the distance to the nearest feature point (cells).
    int c[3];float f[3];for(int i=0;i<3;++i){const float q=p[i]*static_cast<float>(cells);c[i]=static_cast<int>(std::floor(q));f[i]=q-std::floor(q);}
    float d=9;
    for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){const int o[3]{x,y,z};float r2=0;
        for(int i=0;i<3;++i){const float r=static_cast<float>(o[i])+random(c[0]+x,c[1]+y,c[2]+z,cells,seed*3+static_cast<std::uint32_t>(i))-f[i];r2+=r*r;}d=std::min(d,r2);}
    return 1-std::min(std::sqrt(d),1.f);
}
float perlin(const float p[3],int period,std::uint32_t seed){ // Gradient noise, about -1 to 1.
    int c[3];float f[3],u[3];for(int i=0;i<3;++i){const float q=p[i]*static_cast<float>(period);c[i]=static_cast<int>(std::floor(q));f[i]=q-std::floor(q);u[i]=f[i]*f[i]*f[i]*(f[i]*(f[i]*6-15)+10);}
    float v[8];
    for(int k=0;k<8;++k){const int o[3]{k&1,(k>>1)&1,(k>>2)&1};float g[3],n=0,dot=0;
        for(int i=0;i<3;++i){g[i]=random(c[0]+o[0],c[1]+o[1],c[2]+o[2],period,seed*3+static_cast<std::uint32_t>(i))*2-1;n+=g[i]*g[i];}
        n=1/std::sqrt(std::max(n,1e-8f));for(int i=0;i<3;++i)dot+=g[i]*n*(f[i]-static_cast<float>(o[i]));v[k]=dot;}
    auto lerp=[](float a,float b,float t){return a+(b-a)*t;};
    return 1.15f*lerp(lerp(lerp(v[0],v[1],u[0]),lerp(v[2],v[3],u[0]),u[1]),lerp(lerp(v[4],v[5],u[0]),lerp(v[6],v[7],u[0]),u[1]),u[2]);
}
std::vector<std::uint8_t> shapeNoise(){
    const unsigned n=shapeSize;std::vector<float> value(static_cast<std::size_t>(n)*n*n);
    for(unsigned z=0;z<n;++z)for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
        const float p[3]{(static_cast<float>(x)+.5f)/static_cast<float>(n),(static_cast<float>(y)+.5f)/static_cast<float>(n),(static_cast<float>(z)+.5f)/static_cast<float>(n)};
        float pn=0,a=1,total=0;for(int o=0;o<4;++o){pn+=a*perlin(p,4<<o,1);total+=a;a*=.5f;}
        const float fbm=worley(p,4,2)*.625f+worley(p,8,3)*.25f+worley(p,16,4)*.125f;
        value[(static_cast<std::size_t>(z)*n+y)*n+x]=fbm+std::clamp(pn/total*.5f+.5f,0.f,1.f)*(1-fbm);
    }
    std::vector<std::uint32_t> order(value.size());for(std::uint32_t i=0;i<order.size();++i)order[i]=i;
    std::sort(order.begin(),order.end(),[&](std::uint32_t a,std::uint32_t b){return value[a]<value[b];});
    std::vector<std::uint8_t> out(value.size());for(std::size_t r=0;r<order.size();++r)out[order[r]]=static_cast<std::uint8_t>((r*256)/order.size()); // Rank: uniform.
    return out;
}
std::vector<std::uint8_t> detailNoise(){
    const unsigned n=detailSize;std::vector<std::uint8_t> out(static_cast<std::size_t>(n)*n*n);
    for(unsigned z=0;z<n;++z)for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
        const float p[3]{(static_cast<float>(x)+.5f)/static_cast<float>(n),(static_cast<float>(y)+.5f)/static_cast<float>(n),(static_cast<float>(z)+.5f)/static_cast<float>(n)};
        out[(static_cast<std::size_t>(z)*n+y)*n+x]=static_cast<std::uint8_t>(std::lround(255*(worley(p,2,6)*.625f+worley(p,4,7)*.25f+worley(p,8,8)*.125f)));
    }
    return out;
}
const char* const marchEntry=R"(
@group(1) @binding(5) var colourOut: texture_storage_2d<rgba16float, write>;
@group(1) @binding(6) var depthOut: texture_storage_2d<r32float, write>;
@compute @workgroup_size(8, 8, 1) fn marchPass(@builtin(global_invocation_id) id: vec3u) {
    let size = vec2u(clouds.size.xy); if (any(id.xy >= size)) { return; }
    let ndc = vec2f((f32(id.x) + 0.5) / f32(size.x) * 2.0 - 1.0, 1.0 - (f32(id.y) + 0.5) / f32(size.y) * 2.0);
    let jitter = fract(52.9829189 * fract(dot(vec2f(id.xy), vec2f(0.06711056, 0.00583715)))); // Interleaved gradient noise: no banding.
    let r = cloudMarch(ndc, jitter); textureStore(colourOut, id.xy, r.colour); textureStore(depthOut, id.xy, vec4f(r.depth, 0.0, 0.0, 0.0));
}
)";
// Over the scene: the march upsampled (bilinear: colour and transmittance are premultiplied, so they filter), alpha-blended in
// the target's encoding as the other transparent layers, at the clouds' depth.
const char* const compositeCode=R"(
struct CloudParams { layer: vec4f, drift: vec4f, size: vec4f };
@group(1) @binding(1) var cloudColour: texture_2d<f32>;
@group(1) @binding(2) var cloudDepth: texture_2d<f32>;
@group(1) @binding(3) var cloudLinear: sampler;
@group(1) @binding(4) var<uniform> clouds: CloudParams;
struct V { @builtin(position) p: vec4f, @location(0) uv: vec2f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> V { let c = vec2f(f32((i << 1u) & 2u), f32(i & 2u)); var v: V; v.p = vec4f(c * 2.0 - 1.0, 0.0, 1.0); v.uv = vec2f(c.x, 1.0 - c.y); return v; }
struct Out { @location(0) colour: vec4f, @builtin(frag_depth) depth: f32 };
fn cloudPixel(v: V) -> Out {
    let c = textureSampleLevel(cloudColour, cloudLinear, v.uv, 0.0); let cover = 1.0 - c.a; let alpha = cover * clouds.layer.w;
    let size = vec2i(textureDimensions(cloudDepth)); let z = textureLoad(cloudDepth, clamp(vec2i(v.uv * vec2f(size)), vec2i(0), size - 1), 0).r;
    var o: Out; o.colour = vec4f(finish(c.rgb / max(cover, 1e-3)), alpha); o.depth = clamp(frame.viewport.z / z, 0.0, 1.0); return o;
}
// Mostly opaque clouds also write their depth, so transparent objects behind them (paths, borders) stay hidden; thin ones do not.
@fragment fn fs_dense(v: V) -> Out { let o = cloudPixel(v); if (o.colour.a < 0.5) { discard; } return o; }
@fragment fn fs_thin(v: V) -> Out { let o = cloudPixel(v); if (o.colour.a < 0.002 || o.colour.a >= 0.5) { discard; } return o; }
)";
}
const char* const Clouds::wgsl=R"(
struct CloudParams {
    layer: vec4f, // Base and top (km over the air's ground; the top under full cover), extinction (/km), strength.
    drift: vec4f, // The noise's offset (km, planet frame); w: 1 for a flat slab.
    size: vec4f,  // The march's width and height (texels), its steps.
};
@group(1) @binding(1) var<uniform> clouds: CloudParams;
@group(1) @binding(2) var cloudShape: texture_3d<f32>;
@group(1) @binding(3) var cloudDetail: texture_3d<f32>;
@group(1) @binding(4) var cloudNoise: sampler;
const CLOUD_TILE = 12.0; const CLOUD_DETAIL = 1.5; // The noises' periods (km).
const CLOUD_G = 0.85; // Droplets' asymmetry in the visible.
fn cloudRemap(v: f32, lo: f32, hi: f32) -> f32 { return clamp((v - lo) / max(hi - lo, 1e-4), 0.0, 1.0); }
fn cloudTop(cover: f32) -> f32 { if (clouds.drift.w > 0.5) { return clouds.layer.y; } return clouds.layer.x + (clouds.layer.y - clouds.layer.x) * (0.2 + 0.8 * cover * cover); } // Fair-weather cumulus (~0.9 km) to deep decks.
// The density (a fraction of the extinction) at a point h km over the air's ground, and the column's top there: cloud where
// the shape noise (uniform) times the height profile exceeds 1 - cover, so that fraction of the sky is cloudy at mid-height.
fn cloudDensity(p: vec3f, h: f32, detail: f32) -> vec2f { // detail: the erosion's weight (0: the shape only).
    let cover = textureSampleLevel(airClouds, airCloudSampler, cloudUv(p), 0.0).g; let base = clouds.layer.x; let top = cloudTop(cover);
    if (h < base || h > top || cover <= 0.0) { return vec2f(0.0, top); }
    if (clouds.drift.w > 0.5) { return vec2f(cover, top); }
    let f = (h - base) / (top - base); let profile = smoothstep(0.0, 0.1, f) * (1.0 - f * f); // Flat bases, narrowing to domed tops.
    // Columns: the shape at the base under the point (the cloudy fraction seen from above is then the cover); the profile domes their tops.
    let q = p - clouds.drift.xyz; let shape = textureSampleLevel(cloudShape, cloudNoise, (q - p * ((h - base) / length(p))) / CLOUD_TILE, 0.0).r;
    var bumps = 0.0; if (detail > 0.0) { bumps = textureSampleLevel(cloudShape, cloudNoise, q * (4.0 / CLOUD_TILE) + 0.37, 0.0).r - 0.5; } // Their sides, in 3D (zero mean).
    let grown = cover + 0.12 * (1.0 - cover) * (1.0 - cover) * min(cover * 10.0, 1.0) * detail; // What the erosion takes, given back (clouds_test).
    var d = clamp((shape * profile + 0.25 * bumps - (1.0 - grown)) / 0.2, 0.0, 1.0);
    if (detail > 0.0 && d > 0.0 && d < 1.0) { // Eroded edges: billows.
        let fine = textureSampleLevel(cloudDetail, cloudNoise, q / CLOUD_DETAIL, 0.0).r; let e = (1.0 - fine) * 0.8 * detail;
        d = clamp((d - e) / (1.0 - e), 0.0, 1.0);
    }
    return vec2f(d, top);
}
fn cloudSunDepth(p: vec3f, sun: vec3f) -> f32 { // Optical depth toward the sun, to 3.2 km (five steps from 120 m, each 1.9 times longer).
    var tau = 0.0; var t = 0.0; var dt = 0.12;
    for (var i = 0; i < 5; i++) {
        let q = p + sun * (t + 0.5 * dt); t += dt; let h = length(q) - bottomRadius(); if (h > clouds.layer.y) { break; }
        tau += cloudDensity(q, h, 0.0).x * dt; dt *= 1.9;
    }
    return tau * clouds.layer.z;
}
fn cloudLobe(g: f32, c: f32) -> f32 { return (1.0 - g * g) / (4.0 * AIR_PI * pow(1.0 + g * g - 2.0 * g * c, 1.5)); }
fn cloudPhase(c: f32) -> f32 { return 0.96 * cloudLobe(0.9, c) + 0.04 * cloudLobe(-0.4, c); } // g = 0.85; a back lobe for the glory's side.
// The diffuse radiance of a δ-Eddington column (conservative, black below) toward a direction of cosine muv from up: depth
// and column are δ-scaled optical depths from its top and of the whole; sun and sky the irradiances reaching its top (the
// sun's on a surface facing it, the sky's on a horizontal one), relative to the sun's colour.
fn cloudDiffuse(depth: f32, column: f32, mu0In: f32, sun: vec3f, sky: vec3f, muv: f32) -> vec3f {
    let gs = CLOUD_G / (1.0 + CLOUD_G); let mu0 = max(mu0In, 0.01); let d = (1.0 - gs) * column + 4.0 / 3.0;
    let k = 3.0 * mu0 / (4.0 * AIR_PI) * sun * select(0.0, 1.0, mu0In > 0.0); let e = exp(-column / mu0); let ed = exp(-depth / mu0);
    let a = -k * (mu0 * (1.0 - e) + 2.0 / 3.0 * (1.0 + e)) / d; let b = k * mu0 + 2.0 / 3.0 * (k + a); // I1 = k e^(-τ/μ0) + a, I0 = (1 - g)aτ - kμ0 e^(-τ/μ0) + b.
    let a2 = -(sky / AIR_PI) / d; let b2 = sky / AIR_PI + 2.0 / 3.0 * a2;              // The sky's diffuse light on the top.
    let i0 = (1.0 - gs) * (a + a2) * depth - k * mu0 * ed + b + b2; let i1 = k * ed + a + a2;
    return max(i0 + gs * muv * i1, vec3f(0.0));
}
fn cloudSegment(c: vec3f, ray: vec3f) -> vec2f { // Where the ray is in the layer's shell: start, end (end <= start: nowhere).
    let r = length(c); let rmu = dot(c, ray); let inner = bottomRadius() + clouds.layer.x; let outer = bottomRadius() + clouds.layer.y; let ground = bottomRadius();
    let di = rmu * rmu - (r - inner) * (r + inner); let dout = rmu * rmu - (r - outer) * (r + outer);
    if (r < inner) { // Below: up through it, unless the ground comes first.
        if (rmu < 0.0 && rmu * rmu >= (r - ground) * (r + ground)) { return vec2f(0.0); }
        return vec2f(-rmu + sqrt(di), -rmu + sqrt(dout));
    }
    if (r < outer) { return vec2f(0.0, select(-rmu + sqrt(dout), -rmu - sqrt(max(di, 0.0)), rmu < 0.0 && di >= 0.0)); } // Inside: to its base or out of its top.
    if (rmu >= 0.0 || dout < 0.0) { return vec2f(0.0); }
    return vec2f(-rmu - sqrt(dout), select(-rmu + sqrt(dout), -rmu - sqrt(max(di, 0.0)), di >= 0.0)); // Above: in through the top, out through the base or the top.
}
struct CloudRay { colour: vec4f, depth: f32 }; // Radiance (sun-coloured) and view transmittance; view depth (world units).
fn cloudMarch(ndc: vec2f, jitter: f32) -> CloudRay {
    let none = CloudRay(vec4f(0.0, 0.0, 0.0, 1.0), 1e30);
    if (frame.air.w <= 0.0 || frame.projection[3][3] > 0.5) { return none; } // Perspective cameras in the air.
    let toWorld = transpose(mat3x3f(frame.view[0].xyz, frame.view[1].xyz, frame.view[2].xyz));
    let local = normalize(vec3f(ndc.x / frame.projection[0][0], ndc.y / frame.projection[1][1], -1.0));
    let planetRay = (frame.planet * vec4f(toWorld * local, 0.0)).xyz; let kmPerUnit = length(planetRay); let ray = planetRay / kmPerUnit;
    let c = frame.air.xyz; let segment = cloudSegment(c, ray); let start = segment.x; let span = min(segment.y, start + 300.0) - start;
    if (!(span > 0.0) || start > 2000.0) { return none; }
    let sun = frame.airSun.xyz; let phase = cloudPhase(dot(ray, sun)); let scaled = 1.0 - CLOUD_G * CLOUD_G;
    let n = u32(clouds.size.z); let curve = clamp(span / 40.0, 0.0, 0.9); // Long rays: steps growing with distance.
    var light = vec3f(0.0); var direct = 1.0; var diffuse = 1.0; var weighted = 0.0; var weights = 0.0; var previous = start;
    for (var i = 0u; i < n; i++) {
        let x1 = f32(i + 1u) / f32(n); let next = start + span * mix(x1, x1 * x1, curve); let dt = next - previous; previous = next;
        let xm = (f32(i) + jitter) / f32(n); let t = start + span * mix(xm, xm * xm, curve);
        let p = c + ray * t; let r = length(p); let h = r - bottomRadius(); let s = cloudDensity(p, h, 1.0 - smoothstep(15.0, 40.0, t)); // The detail (47 m texels, no mipmaps) fades out far away.
        if (s.x <= 0.0) { continue; }
        let sigma = clouds.layer.z * s.x; let up = p / r; let mu0 = dot(up, sun); let rc = clampRadius(r);
        let sunlight = mix(vec3f(1.0), transmittanceToSun(airTransmittance, airSampler, rc, mu0), frame.air.w);
        let sky = lookupIrradiance(airIrradiance, airSampler, rc, mu0) * frame.air.w;
        let single = sunlight * phase * exp(-cloudSunDepth(p, sun));
        let field = cloudDiffuse(scaled * sigma * (s.y - h), scaled * sigma * (s.y - clouds.layer.x), mu0, sunlight, sky, -dot(ray, up));
        let a1 = 1.0 - exp(-sigma * dt); let a2 = 1.0 - exp(-scaled * sigma * dt); // Each step integrated exactly at its density.
        light += direct * a1 * single + diffuse * a2 * field;
        weighted += t * direct * a1; weights += direct * a1; direct *= 1.0 - a1; diffuse *= 1.0 - a2;
        if (diffuse < 0.005) { break; }
    }
    if (weights <= 0.0) { return none; }
    let distance = weighted / weights; let air = airBetween(c, ray * distance, sun);
    let colour = (light * mix(vec3f(1.0), air.transmittance, frame.air.w) + air.radiance * frame.air.w * (1.0 - direct)) * frame.sunColor.rgb;
    return CloudRay(vec4f(colour, direct), distance / kmPerUnit * -local.z);
}
)";
struct Clouds::Impl {
    WGPUDevice device=nullptr;WGPUQueue queue=nullptr;WGPUBuffer params=nullptr;
    WGPUTexture shape=nullptr,detail=nullptr,colour=nullptr,depth=nullptr;WGPUTextureView shapeView=nullptr,detailView=nullptr,colourView=nullptr,depthView=nullptr;
    WGPUSampler repeat=nullptr,clamp=nullptr;WGPUShaderModule marchModule=nullptr,compositeModule=nullptr;
    WGPUBindGroupLayout marchLayout=nullptr,compositeLayout=nullptr;WGPUPipelineLayout marchPipelineLayout=nullptr,compositePipelineLayout=nullptr;
    WGPUComputePipeline marchPipeline=nullptr;std::map<std::array<int,3>,WGPURenderPipeline> composites; // Format, samples, dense.
    WGPUBindGroup marchGroup=nullptr,compositeGroup=nullptr;unsigned width=0,height=0;
    WGPUShaderModule module(const std::string& code,const char* label){
        WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code=sv(code);WGPUShaderModuleDescriptor d{};d.nextInChain=&w.chain;d.label=sv(label);return wgpuDeviceCreateShaderModule(device,&d);}
    WGPUTexture texture(WGPUTextureDimension dimension,WGPUExtent3D size,WGPUTextureFormat format,WGPUTextureView& view){
        WGPUTextureDescriptor t{};t.size=size;t.format=format;t.dimension=dimension;t.mipLevelCount=1;t.sampleCount=1;t.usage=WGPUTextureUsage_StorageBinding|WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopySrc;
        WGPUTexture texture=wgpuDeviceCreateTexture(device,&t);view=wgpuTextureCreateView(texture,nullptr);return texture;}
    void resize(unsigned w,unsigned h){
        if(colour&&w==width&&h==height)return;
        for(auto g:{marchGroup,compositeGroup})if(g)wgpuBindGroupRelease(g);for(auto v:{colourView,depthView})if(v)wgpuTextureViewRelease(v);for(auto t:{colour,depth})if(t)wgpuTextureRelease(t);
        colour=texture(WGPUTextureDimension_2D,{w,h,1},WGPUTextureFormat_RGBA16Float,colourView);depth=texture(WGPUTextureDimension_2D,{w,h,1},WGPUTextureFormat_R32Float,depthView);width=w;height=h;
        WGPUBindGroupEntry m[6]{};for(std::uint32_t i=0;i<6;++i)m[i].binding=1+i;
        m[0].buffer=params;m[0].size=48;m[1].textureView=shapeView;m[2].textureView=detailView;m[3].sampler=repeat;m[4].textureView=colourView;m[5].textureView=depthView;
        WGPUBindGroupDescriptor d{};d.layout=marchLayout;d.entryCount=6;d.entries=m;marchGroup=wgpuDeviceCreateBindGroup(device,&d);
        WGPUBindGroupEntry c[4]{};for(std::uint32_t i=0;i<4;++i)c[i].binding=1+i;
        c[0].textureView=colourView;c[1].textureView=depthView;c[2].sampler=clamp;c[3].buffer=params;c[3].size=48;
        d.layout=compositeLayout;d.entryCount=4;d.entries=c;compositeGroup=wgpuDeviceCreateBindGroup(device,&d);
    }
};
Clouds::Noise Clouds::noise(){return{shapeNoise(),detailNoise()};}
Clouds::Clouds(Context& context,WGPUBindGroupLayout frameLayout,const Noise& noise):impl_(std::make_unique<Impl>()){
    auto& d=*impl_;d.device=context.device();d.queue=context.queue();
    WGPUBufferDescriptor bd{};bd.size=48;bd.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;d.params=wgpuDeviceCreateBuffer(d.device,&bd);
    auto volume=[&](unsigned n,const std::vector<std::uint8_t>& data,WGPUTextureView& view){
        WGPUTextureDescriptor t{};t.size={n,n,n};t.format=WGPUTextureFormat_R8Unorm;t.dimension=WGPUTextureDimension_3D;t.mipLevelCount=1;t.sampleCount=1;t.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
        WGPUTexture texture=wgpuDeviceCreateTexture(d.device,&t);view=wgpuTextureCreateView(texture,nullptr);
        WGPUTexelCopyTextureInfo dst{};dst.texture=texture;WGPUTexelCopyBufferLayout l{};l.bytesPerRow=n;l.rowsPerImage=n;wgpuQueueWriteTexture(d.queue,&dst,data.data(),data.size(),&l,&t.size);return texture;};
    if(noise.shape.size()!=std::size_t{shapeSize}*shapeSize*shapeSize||noise.detail.size()!=std::size_t{detailSize}*detailSize*detailSize)throw std::invalid_argument("Cloud noise of the wrong size");
    d.shape=volume(shapeSize,noise.shape,d.shapeView);d.detail=volume(detailSize,noise.detail,d.detailView);
    WGPUSamplerDescriptor s{};s.addressModeU=s.addressModeV=s.addressModeW=WGPUAddressMode_Repeat;s.magFilter=s.minFilter=WGPUFilterMode_Linear;s.mipmapFilter=WGPUMipmapFilterMode_Nearest;s.lodMaxClamp=1;s.maxAnisotropy=1;
    d.repeat=wgpuDeviceCreateSampler(d.device,&s);s.addressModeU=s.addressModeV=s.addressModeW=WGPUAddressMode_ClampToEdge;d.clamp=wgpuDeviceCreateSampler(d.device,&s);
    { // The march: the scene's frame group, then its own.
        WGPUBindGroupLayoutEntry e[6]{};for(std::uint32_t i=0;i<6;++i){e[i].binding=1+i;e[i].visibility=WGPUShaderStage_Compute;}
        e[0].buffer.type=WGPUBufferBindingType_Uniform;
        for(int i=1;i<3;++i){e[i].texture.sampleType=WGPUTextureSampleType_Float;e[i].texture.viewDimension=WGPUTextureViewDimension_3D;}
        e[3].sampler.type=WGPUSamplerBindingType_Filtering;
        e[4].storageTexture.access=e[5].storageTexture.access=WGPUStorageTextureAccess_WriteOnly;e[4].storageTexture.format=WGPUTextureFormat_RGBA16Float;e[5].storageTexture.format=WGPUTextureFormat_R32Float;
        e[4].storageTexture.viewDimension=e[5].storageTexture.viewDimension=WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=6;ld.entries=e;d.marchLayout=wgpuDeviceCreateBindGroupLayout(d.device,&ld);
        WGPUBindGroupLayout layouts[]{frameLayout,d.marchLayout};WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=2;pd.bindGroupLayouts=layouts;d.marchPipelineLayout=wgpuDeviceCreatePipelineLayout(d.device,&pd);
        d.marchModule=d.module(std::string(prelude)+atmosphereWgsl+air+wgsl+marchEntry,"albert:clouds");
        WGPUComputePipelineDescriptor c{};c.layout=d.marchPipelineLayout;c.compute.module=d.marchModule;c.compute.entryPoint=sv("marchPass");d.marchPipeline=wgpuDeviceCreateComputePipeline(d.device,&c);
    }
    { // The composite: the frame group (bound by the scene pass), then the march's results.
        WGPUBindGroupLayoutEntry e[4]{};for(std::uint32_t i=0;i<4;++i){e[i].binding=1+i;e[i].visibility=WGPUShaderStage_Fragment;}
        e[0].texture.sampleType=WGPUTextureSampleType_Float;e[1].texture.sampleType=WGPUTextureSampleType_UnfilterableFloat;e[0].texture.viewDimension=e[1].texture.viewDimension=WGPUTextureViewDimension_2D;
        e[2].sampler.type=WGPUSamplerBindingType_Filtering;e[3].buffer.type=WGPUBufferBindingType_Uniform;
        WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=4;ld.entries=e;d.compositeLayout=wgpuDeviceCreateBindGroupLayout(d.device,&ld);
        WGPUBindGroupLayout layouts[]{frameLayout,d.compositeLayout};WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=2;pd.bindGroupLayouts=layouts;d.compositePipelineLayout=wgpuDeviceCreatePipelineLayout(d.device,&pd);
        d.compositeModule=d.module(std::string(prelude)+compositeCode,"albert:clouds composite");
    }
}
Clouds::~Clouds(){
    auto& d=*impl_;
    for(auto& [k,p]:d.composites)wgpuRenderPipelineRelease(p);if(d.marchPipeline)wgpuComputePipelineRelease(d.marchPipeline);
    for(auto g:{d.marchGroup,d.compositeGroup})if(g)wgpuBindGroupRelease(g);
    for(auto l:{d.marchPipelineLayout,d.compositePipelineLayout})if(l)wgpuPipelineLayoutRelease(l);for(auto l:{d.marchLayout,d.compositeLayout})if(l)wgpuBindGroupLayoutRelease(l);
    for(auto m:{d.marchModule,d.compositeModule})if(m)wgpuShaderModuleRelease(m);for(auto s:{d.repeat,d.clamp})if(s)wgpuSamplerRelease(s);
    for(auto v:{d.shapeView,d.detailView,d.colourView,d.depthView})if(v)wgpuTextureViewRelease(v);for(auto t:{d.shape,d.detail,d.colour,d.depth})if(t)wgpuTextureRelease(t);
    if(d.params)wgpuBufferRelease(d.params);
}
WGPUTexture Clouds::colour()const{return impl_->colour;}
WGPUTexture Clouds::depth()const{return impl_->depth;}
void Clouds::march(WGPUCommandEncoder encoder,WGPUBindGroup frame,const View& v,const WGPUPassTimestampWrites* timing){
    auto& d=*impl_;const unsigned divisor=v.divisor?v.divisor:1,w=(v.width+divisor-1)/divisor,h=(v.height+divisor-1)/divisor;if(!w||!h)return;
    d.resize(w,h);
    const float p[12]{v.baseKm,v.topKm,v.extinction,v.strength,v.drift[0],v.drift[1],v.drift[2],v.flat?1.f:0.f,static_cast<float>(w),static_cast<float>(h),static_cast<float>(v.steps?v.steps:1),0};
    wgpuQueueWriteBuffer(d.queue,d.params,0,p,sizeof p);
    WGPUComputePassDescriptor cd{};cd.timestampWrites=timing;WGPUComputePassEncoder pass=wgpuCommandEncoderBeginComputePass(encoder,&cd);
    wgpuComputePassEncoderSetPipeline(pass,d.marchPipeline);wgpuComputePassEncoderSetBindGroup(pass,0,frame,0,nullptr);wgpuComputePassEncoderSetBindGroup(pass,1,d.marchGroup,0,nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass,(w+7)/8,(h+7)/8,1);wgpuComputePassEncoderEnd(pass);wgpuComputePassEncoderRelease(pass);
}
void Clouds::composite(WGPURenderPassEncoder pass,WGPUTextureFormat format,unsigned samples){
    auto& d=*impl_;if(!d.compositeGroup)return;
    for(int dense=1;dense>=0;--dense){
        auto& pipe=d.composites[{static_cast<int>(format),static_cast<int>(samples),dense}];
        if(!pipe){
            WGPUBlendState b{};b.color={WGPUBlendOperation_Add,WGPUBlendFactor_SrcAlpha,WGPUBlendFactor_OneMinusSrcAlpha};b.alpha={WGPUBlendOperation_Add,WGPUBlendFactor_Zero,WGPUBlendFactor_One};
            WGPUColorTargetState t{};t.format=format;t.blend=&b;t.writeMask=WGPUColorWriteMask_All;
            WGPUFragmentState f{};f.module=d.compositeModule;f.entryPoint=sv(dense?"fs_dense":"fs_thin");f.targetCount=1;f.targets=&t;
            WGPUDepthStencilState ds{};ds.format=WGPUTextureFormat_Depth32Float;ds.depthWriteEnabled=dense?WGPUOptionalBool_True:WGPUOptionalBool_False;ds.depthCompare=WGPUCompareFunction_GreaterEqual; // Reversed Z: behind nearer objects.
            ds.stencilFront.compare=ds.stencilBack.compare=WGPUCompareFunction_Always;ds.stencilFront.failOp=ds.stencilFront.depthFailOp=ds.stencilFront.passOp=WGPUStencilOperation_Keep;ds.stencilBack=ds.stencilFront;
            WGPURenderPipelineDescriptor rd{};rd.label=sv("albert:clouds composite");rd.layout=d.compositePipelineLayout;rd.vertex.module=d.compositeModule;rd.vertex.entryPoint=sv("vs_main");
            rd.primitive.topology=WGPUPrimitiveTopology_TriangleList;rd.depthStencil=&ds;rd.multisample.count=samples;rd.multisample.mask=~0u;rd.fragment=&f;
            pipe=wgpuDeviceCreateRenderPipeline(d.device,&rd);
        }
        wgpuRenderPassEncoderSetPipeline(pass,pipe);wgpuRenderPassEncoderSetBindGroup(pass,1,d.compositeGroup,0,nullptr);wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    }
}
}
