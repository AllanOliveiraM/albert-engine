#include "gpu/ocean.hpp"
#include <webgpu/wgpu.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace albert::gpu {
namespace {
constexpr double g=9.80665,pi=3.14159265358979323846;
WGPUStringView sv(const char* s){return {s,std::strlen(s)};}
const char* const code=R"(
struct Params { wind: vec4f, time: vec4f }; // wind: speed (m/s), toward east, toward north; time: seconds, choppiness.
@group(0) @binding(0) var<uniform> params: Params;
@group(0) @binding(1) var<storage, read_write> spectrum: array<vec4f>; // h0(k), conj h0(-k), per cascade.
@group(0) @binding(2) var<storage, read_write> fields: array<vec4f>;   // Two buffers of two packed complex fields per cascade.
@group(0) @binding(3) var displacementOut: texture_storage_2d_array<rgba16float, write>;
@group(0) @binding(4) var derivativesOut: texture_storage_2d_array<rgba16float, write>;
const N: u32 = 256u; const G: f32 = 9.80665; const PI: f32 = 3.14159265358979;
fn patchSize(c: u32) -> f32 { return select(select(32.0, 128.0, c == 1u), 512.0, c == 0u); }
fn waveVector(n: vec2u, c: u32) -> vec2f { return (vec2f(n) - 128.0) * (2.0 * PI / patchSize(c)); } // East, north (rad/m).
fn inBand(k: f32, c: u32) -> bool { // Split at six fundamentals of the next cascade.
    let split1 = 12.0 * PI / 128.0; let split2 = 12.0 * PI / 32.0;
    if (c == 0u) { return k < split1; } if (c == 1u) { return k >= split1 && k < split2; } return k >= split2;
}
fn hash(x: u32) -> u32 { var h = x; h ^= h >> 16u; h *= 0x7feb352du; h ^= h >> 15u; h *= 0x846ca68bu; h ^= h >> 16u; return h; }
fn gaussian(n: vec2u, c: u32) -> vec2f { // Two normal deviates (Box-Muller), fixed per grid point and cascade.
    let s = hash(n.x + N * (n.y + N * c) + 0x9e3779b9u);
    let u1 = (f32(hash(s) >> 8u) + 0.5) / 16777216.0; let u2 = f32(hash(s ^ 0x85ebca6bu) >> 8u) / 16777216.0;
    let r = sqrt(-2.0 * log(u1)); return vec2f(r * cos(2.0 * PI * u2), r * sin(2.0 * PI * u2));
}
fn density(k: vec2f) -> f32 { // Directional Pierson-Moskowitz per unit of k² (m⁴).
    let kk = length(k); let speed = params.wind.x; if (kk < 1e-6 || speed < 0.1) { return 0.0; }
    let w = sqrt(G * kk); let wp = 0.877 * G / (1.026 * speed);
    let s = 0.0081 * G * G / pow(w, 5.0) * exp(-1.25 * pow(wp / w, 4.0));
    let c = dot(k / kk, params.wind.yz / speed); let spread = select(0.0, 2.0 / PI * c * c, c > 0.0);
    return s * spread * (G / (2.0 * w)) / kk;
}
fn amplitude(n: vec2u, c: u32) -> vec2f { // E|h0|² = S Δk²/2: the field's variance is then Σ S Δk².
    let k = waveVector(n, c); if (!inBand(length(k), c)) { return vec2f(0.0); }
    let dk = 2.0 * PI / patchSize(c); return gaussian(n, c) * sqrt(density(k) * dk * dk / 4.0);
}
fn mul(a: vec2f, b: vec2f) -> vec2f { return vec2f(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }
@compute @workgroup_size(16, 16, 1) fn spectrumPass(@builtin(global_invocation_id) id: vec3u) {
    let n = id.xy; let c = id.z; let m = (vec2u(N) - n) % vec2u(N); // The index of -k.
    let a = amplitude(n, c); let b = amplitude(m, c);
    spectrum[(c * N + n.y) * N + n.x] = vec4f(a, b.x, -b.y);
}
@compute @workgroup_size(16, 16, 1) fn evolvePass(@builtin(global_invocation_id) id: vec3u) {
    let n = id.xy; let c = id.z; let i = n.y * N + n.x; let k = waveVector(n, c); let kk = length(k);
    let w = sqrt(G * kk); let t = params.time.x; let e = vec2f(cos(w * t), sin(w * t)); let s = spectrum[c * N * N + i];
    let h = mul(s.xy, e) + mul(s.zw, vec2f(e.x, -e.y));
    let inv = select(0.0, 1.0 / kk, kk > 1e-6); let ih = vec2f(-h.y, h.x); // i h
    let dx = -ih * (k.x * inv); let dz = -ih * (k.y * inv); let gx = ih * k.x; let gz = ih * k.y; let xx = h * (k.x * k.x * inv); let zz = h * (k.y * k.y * inv);
    // Packed in pairs of real fields a + i b: (Dx, Dz), (η, ∂η/∂x), (∂η/∂z, ∂Dx/∂x), (∂Dz/∂z, 0).
    fields[(c * 2u) * N * N + i] = vec4f(dx + vec2f(-dz.y, dz.x), h + vec2f(-gx.y, gx.x));
    fields[(c * 2u + 1u) * N * N + i] = vec4f(gz + vec2f(-xx.y, xx.x), zz);
}
var<workgroup> line: array<vec4f, 256>;
fn butterfly(l: u32, s: u32) { // One stage of an in-place inverse FFT (radix 2, bit-reversed input) of two complex sequences.
    let m = 1u << s; let half = m >> 1u; let j = l % m;
    if (j < half) {
        let a = 2.0 * PI * f32(j) / f32(m); let w = vec2f(cos(a), sin(a));
        let u = line[l]; let v = line[l + half]; let t = vec4f(mul(w, v.xy), mul(w, v.zw));
        line[l] = u + t; line[l + half] = u - t;
    }
}
fn transform(l: u32) { // The eight stages in straight-line code: no barrier inside a loop (FXC on Direct3D 12).
    butterfly(l, 1u); workgroupBarrier(); butterfly(l, 2u); workgroupBarrier(); butterfly(l, 3u); workgroupBarrier(); butterfly(l, 4u); workgroupBarrier();
    butterfly(l, 5u); workgroupBarrier(); butterfly(l, 6u); workgroupBarrier(); butterfly(l, 7u); workgroupBarrier(); butterfly(l, 8u); workgroupBarrier();
}
@compute @workgroup_size(256, 1, 1) fn rowsPass(@builtin(local_invocation_id) l: vec3u, @builtin(workgroup_id) g: vec3u) {
    let base = (g.z * 2u + g.y) * N * N + g.x * N;
    line[reverseBits(l.x) >> 24u] = fields[base + l.x]; workgroupBarrier();
    transform(l.x); fields[base + l.x] = line[l.x];
}
@compute @workgroup_size(256, 1, 1) fn columnsPass(@builtin(local_invocation_id) l: vec3u, @builtin(workgroup_id) g: vec3u) {
    let base = (g.z * 2u + g.y) * N * N + g.x;
    line[reverseBits(l.x) >> 24u] = fields[base + l.x * N]; workgroupBarrier();
    transform(l.x); fields[base + l.x * N] = line[l.x];
}
@compute @workgroup_size(16, 16, 1) fn assemblePass(@builtin(global_invocation_id) id: vec3u) {
    let c = id.z; let i = id.y * N + id.x; let lambda = params.time.y;
    let sign = select(1.0, -1.0, ((id.x + id.y) & 1u) == 1u); // The spectrum is centred at index 128.
    let a = fields[(c * 2u) * N * N + i] * sign; let b = fields[(c * 2u + 1u) * N * N + i] * sign;
    textureStore(displacementOut, id.xy, c, vec4f(lambda * a.x, a.z, lambda * a.y, 0.0));
    textureStore(derivativesOut, id.xy, c, vec4f(a.w, b.x, lambda * b.y, lambda * b.z));
}
)";
}
struct Ocean::Impl {
    WGPUDevice device=nullptr;WGPUQueue queue=nullptr;WGPUBuffer params=nullptr,spectrum=nullptr,fields=nullptr;WGPUShaderModule module=nullptr;
    WGPUBindGroupLayout layout=nullptr;WGPUPipelineLayout pipelineLayout=nullptr;WGPUBindGroup group=nullptr;
    WGPUComputePipeline spectrumPass=nullptr,evolvePass=nullptr,rowsPass=nullptr,columnsPass=nullptr,assemblePass=nullptr;
    float speed=-1,east=0,north=0;bool software=false;
};
Ocean::Ocean(Context& context):impl_(new Impl){
    auto& d=*impl_;d.device=context.device();d.queue=context.queue();d.software=context.software();
    auto buffer=[&](std::size_t bytes,WGPUBufferUsage usage){WGPUBufferDescriptor b{};b.size=bytes;b.usage=usage|WGPUBufferUsage_CopyDst;return wgpuDeviceCreateBuffer(d.device,&b);};
    const std::size_t grid=static_cast<std::size_t>(size)*size*cascades*16;
    d.params=buffer(32,WGPUBufferUsage_Uniform);d.spectrum=buffer(grid,WGPUBufferUsage_Storage);d.fields=buffer(2*grid,WGPUBufferUsage_Storage);
    for(int i=0;i<2;++i){
        WGPUTextureDescriptor t{};t.size={size,size,cascades};t.format=WGPUTextureFormat_RGBA16Float;t.dimension=WGPUTextureDimension_2D;t.mipLevelCount=1;t.sampleCount=1;
        t.usage=WGPUTextureUsage_StorageBinding|WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopySrc;textures_[static_cast<std::size_t>(i)]=wgpuDeviceCreateTexture(d.device,&t);
        WGPUTextureViewDescriptor v{};v.format=t.format;v.dimension=WGPUTextureViewDimension_2DArray;v.mipLevelCount=1;v.arrayLayerCount=cascades;v.aspect=WGPUTextureAspect_All;
        views_[static_cast<std::size_t>(i)]=wgpuTextureCreateView(textures_[static_cast<std::size_t>(i)],&v);
    }
    WGPUBindGroupLayoutEntry e[5]{};
    for(std::uint32_t i=0;i<5;++i){e[i].binding=i;e[i].visibility=WGPUShaderStage_Compute;}
    e[0].buffer.type=WGPUBufferBindingType_Uniform;e[1].buffer.type=e[2].buffer.type=WGPUBufferBindingType_Storage;
    for(int i=3;i<5;++i){e[i].storageTexture.access=WGPUStorageTextureAccess_WriteOnly;e[i].storageTexture.format=WGPUTextureFormat_RGBA16Float;e[i].storageTexture.viewDimension=WGPUTextureViewDimension_2DArray;}
    WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=5;ld.entries=e;d.layout=wgpuDeviceCreateBindGroupLayout(d.device,&ld);
    WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&d.layout;d.pipelineLayout=wgpuDeviceCreatePipelineLayout(d.device,&pd);
    WGPUBindGroupEntry b[5]{};b[0].binding=0;b[0].buffer=d.params;b[0].size=32;b[1].binding=1;b[1].buffer=d.spectrum;b[1].size=grid;b[2].binding=2;b[2].buffer=d.fields;b[2].size=2*grid;
    b[3].binding=3;b[3].textureView=views_[0];b[4].binding=4;b[4].textureView=views_[1];
    WGPUBindGroupDescriptor gd{};gd.layout=d.layout;gd.entryCount=5;gd.entries=b;d.group=wgpuDeviceCreateBindGroup(d.device,&gd);
    WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code=sv(code);WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;d.module=wgpuDeviceCreateShaderModule(d.device,&md);
    auto pipeline=[&](const char* entry){WGPUComputePipelineDescriptor c{};c.layout=d.pipelineLayout;c.compute.module=d.module;c.compute.entryPoint=sv(entry);return wgpuDeviceCreateComputePipeline(d.device,&c);};
    d.spectrumPass=pipeline("spectrumPass");d.evolvePass=pipeline("evolvePass");d.rowsPass=pipeline("rowsPass");d.columnsPass=pipeline("columnsPass");d.assemblePass=pipeline("assemblePass");
}
Ocean::~Ocean(){
    auto& d=*impl_;
    for(auto p:{d.spectrumPass,d.evolvePass,d.rowsPass,d.columnsPass,d.assemblePass})if(p)wgpuComputePipelineRelease(p);
    if(d.group)wgpuBindGroupRelease(d.group);if(d.pipelineLayout)wgpuPipelineLayoutRelease(d.pipelineLayout);if(d.layout)wgpuBindGroupLayoutRelease(d.layout);if(d.module)wgpuShaderModuleRelease(d.module);
    for(auto x:{d.params,d.spectrum,d.fields})if(x)wgpuBufferRelease(x);
    for(auto v:views_)if(v)wgpuTextureViewRelease(v);for(auto t:textures_)if(t)wgpuTextureRelease(t);
    delete impl_;
}
void Ocean::update(WGPUCommandEncoder encoder,double seconds,float east,float north,float choppiness){
    auto& d=*impl_;const float speed=std::hypot(east,north);
    const float data[8]{speed,east,north,0,static_cast<float>(std::fmod(seconds,1e5)),choppiness,0,0};wgpuQueueWriteBuffer(d.queue,d.params,0,data,sizeof data);
    const bool respectrum=d.speed<0||std::abs(speed-d.speed)>.25f||std::acos(std::clamp((east*d.east+north*d.north)/std::max(speed*std::hypot(d.east,d.north),1e-6f),-1.f,1.f))>.05f;
    // A pass per dispatch; on a CPU adapter (WARP) each also goes in its own submission, under the driver's watchdog alone.
    // ALBERT_GPU_TRACE (with a CPU adapter): each pass waited for and named, to find one that loses the device.
    static const bool trace=std::getenv("ALBERT_GPU_TRACE")!=nullptr;
    auto run=[&](WGPUComputePipeline pl,unsigned x,unsigned y,unsigned z,const char* name){
        WGPUCommandEncoder e=d.software?wgpuDeviceCreateCommandEncoder(d.device,nullptr):encoder;WGPUComputePassEncoder p=wgpuCommandEncoderBeginComputePass(e,nullptr);
        wgpuComputePassEncoderSetBindGroup(p,0,d.group,0,nullptr);wgpuComputePassEncoderSetPipeline(p,pl);wgpuComputePassEncoderDispatchWorkgroups(p,x,y,z);wgpuComputePassEncoderEnd(p);wgpuComputePassEncoderRelease(p);
        if(d.software){WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(d.queue,1,&c);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);
            if(trace){wgpuDevicePoll(d.device,true,nullptr);std::fprintf(stderr,"ocean: %s done\n",name);std::fflush(stderr);}}};
    if(respectrum){run(d.spectrumPass,size/16,size/16,cascades,"spectrum");d.speed=speed;d.east=east;d.north=north;}
    run(d.evolvePass,size/16,size/16,cascades,"evolve");run(d.rowsPass,size,2,cascades,"rows");run(d.columnsPass,size,2,cascades,"columns");run(d.assemblePass,size/16,size/16,cascades,"assemble");
}
double Ocean::significantHeight(double u){const double wp=.877*g/(1.026*u);return 4*std::sqrt(.0081*g*g/(5*std::pow(wp,4)));}
double Ocean::resolvedSlope(double u){double s=0;for(unsigned c=0;c<cascades;++c)s+=bandSlope(u,c);return s;}
double Ocean::bandSlope(double u,unsigned c){ // ∫ k² S(ω) dω over the cascade's band (the first from its fundamental, the last to its Nyquist).
    const double split1=12*pi/patches[1],split2=12*pi/patches[2];
    const double k0=c==0?2*pi/patches[0]:c==1?split1:split2,k1=c==0?split1:c==1?split2:pi*size/patches[cascades-1];
    if(u<.1)return 0;const double wp=.877*g/(1.026*u);double s=0;
    const double w0=std::sqrt(g*k0),w1=std::sqrt(g*k1);constexpr int steps=4000;
    for(int i=0;i<steps;++i){const double w=w0*std::pow(w1/w0,(i+.5)/steps),dw=w*std::log(w1/w0)/steps,k=w*w/g;
        s+=k*k*.0081*g*g/std::pow(w,5)*std::exp(-1.25*std::pow(wp/w,4))*dw;}
    return s;
}
}
