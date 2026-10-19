// Bruneton tables precomputed with WebGPU compute, read back and checked against closed forms:
// the zenith transmittance from the ground (exponential profiles and the ozone tent) and the single
// scattering of a vertical ray with the sun at the zenith (T(0,top) times the optical depth).
// Exits 77 (skipped) without a WebGPU adapter.
#include "gpu/atmosphere.hpp"
#include "gpu/programs.hpp"
#include "gpu/scene.hpp"
#include "threepp/textures/DataTexture.hpp"
#include <threepp/threepp.hpp>
#include <threepp/materials/ShaderMaterial.hpp>
#include <string>
#include <webgpu/wgpu.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace {
using albert::gpu::Atmosphere;
float half(std::uint16_t h){const int e=(h>>10)&31,m=h&1023;const float s=(h>>15)?-1.f:1.f;
    if(e==0)return s*std::ldexp(static_cast<float>(m),-24);if(e==31)return m?NAN:s*INFINITY;return s*std::ldexp(static_cast<float>(m|1024),e-25);}
// RGBA texels of one layer (z) of a table; formats rgba16float or rgba32float.
std::vector<float> read(albert::gpu::Context& gpu,WGPUTexture texture,unsigned z){
    const unsigned w=wgpuTextureGetWidth(texture),h=wgpuTextureGetHeight(texture);const bool wide=wgpuTextureGetFormat(texture)==WGPUTextureFormat_RGBA32Float;
    const unsigned texel=wide?16:8,row=(w*texel+255)/256*256;WGPUBufferDescriptor bd{};bd.size=row*h;bd.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;WGPUBuffer b=wgpuDeviceCreateBuffer(gpu.device(),&bd);
    WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);WGPUTexelCopyTextureInfo src{};src.texture=texture;src.origin={0,0,z};WGPUTexelCopyBufferInfo dst{};dst.buffer=b;dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=h;
    const WGPUExtent3D size{w,h,1};wgpuCommandEncoderCopyTextureToBuffer(e,&src,&dst,&size);WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu.queue(),1,&c);
    bool done=false;WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&done;cb.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
    wgpuBufferMapAsync(b,WGPUMapMode_Read,0,bd.size,cb);while(!done)wgpuInstanceProcessEvents(gpu.instance());
    const auto* p=static_cast<const unsigned char*>(wgpuBufferGetConstMappedRange(b,0,bd.size));std::vector<float> out(static_cast<std::size_t>(w)*h*4);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w*4;++x){float v;if(wide)std::memcpy(&v,p+y*row+x*4,4);else{std::uint16_t q;std::memcpy(&q,p+y*row+x*2,2);v=half(q);}out[static_cast<std::size_t>(y)*w*4+x]=v;}
    wgpuBufferUnmap(b);wgpuBufferRelease(b);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);return out;
}
std::vector<unsigned char> readPixels(albert::gpu::Context& gpu,WGPUTexture texture,unsigned w,unsigned h){ // RGBA8 rows, red first.
    const unsigned row=(w*4+255)/256*256;WGPUBufferDescriptor bd{};bd.size=row*h;bd.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;WGPUBuffer b=wgpuDeviceCreateBuffer(gpu.device(),&bd);
    WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);WGPUTexelCopyTextureInfo src{};src.texture=texture;WGPUTexelCopyBufferInfo dst{};dst.buffer=b;dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=h;
    const WGPUExtent3D size{w,h,1};wgpuCommandEncoderCopyTextureToBuffer(e,&src,&dst,&size);WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu.queue(),1,&c);
    bool done=false;WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&done;cb.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
    wgpuBufferMapAsync(b,WGPUMapMode_Read,0,bd.size,cb);while(!done)wgpuInstanceProcessEvents(gpu.instance());
    const auto* p=static_cast<const unsigned char*>(wgpuBufferGetConstMappedRange(b,0,bd.size));std::vector<unsigned char> out(static_cast<std::size_t>(w)*h*4);const bool bgra=gpu.format()==WGPUTextureFormat_BGRA8Unorm;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto* q=p+y*row+x*4;auto* d=&out[(static_cast<std::size_t>(y)*w+x)*4];d[0]=bgra?q[2]:q[0];d[1]=q[1];d[2]=bgra?q[0]:q[2];d[3]=q[3];}
    wgpuBufferUnmap(b);wgpuBufferRelease(b);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);return out;
}
int failures=0;
constexpr int samples=200; // SCATTER_SAMPLES of the single scattering pass.
void near(const char* what,double got,double want,double tolerance){
    const bool ok=std::abs(got-want)<=tolerance;std::printf("%s %s: %.6g (want %.6g +- %.2g)\n",ok?"ok  ":"FAIL",what,got,want,tolerance);if(!ok)++failures;
}
}
int main(){
    try{
        std::unique_ptr<albert::gpu::Context> gpu;
        try{gpu=std::make_unique<albert::gpu::Context>();}catch(const std::exception& e){std::printf("skipped: %s\n",e.what());return 77;}
        std::printf("adapter: %s\n",gpu->adapter().c_str());
        auto model=albert::gpu::earthAtmosphere();
        // Optical depths at the zenith from the ground: H (1 - e^(-depth/H)) per exponential layer, 15 km of ozone.
        const double depth=model.top-model.bottom;double tau[3];
        for(int c=0;c<3;++c)tau[c]=model.rayleigh[c]*8*(1-std::exp(-depth/8))+model.mieExtinction[c]*1.2*(1-std::exp(-depth/1.2))+model.absorption[c]*15;
        for(int orders:{1,4}){
            model.orders=orders;const auto start=std::chrono::steady_clock::now();
            Atmosphere air(*gpu,model);air.precompute(*gpu);wgpuDevicePoll(gpu->device(),true,nullptr);
            std::printf("orders %d: %.0f ms\n",orders,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            const auto t=read(*gpu,air.table(0),0); // Texel (0, 0): r = bottom, mu = 1.
            const char* names[]{"T zenith R","T zenith G","T zenith B"};
            if(orders==1)for(int c=0;c<3;++c)near(names[c],t[c],std::exp(-tau[c]),1e-3);
            // Scattering texel: any nu, mu_s index 31 (mu_s = 1), mu row 64 (mu = 1), r layer 0.
            const auto s=read(*gpu,air.table(1),0);const auto m=read(*gpu,air.table(2),0);const std::size_t at=(64*256+31)*4;
            if(orders==1){
                const char* r[]{"single Rayleigh R","single Rayleigh G","single Rayleigh B"};
                // The shader's trapezoidal rule (samples+1 points) and the exact integral of the profile.
                auto column=[&](double scale,bool exact){if(exact)return scale*(1-std::exp(-depth/scale));double sum=0;const double dx=depth/samples;
                    for(int i=0;i<=samples;++i)sum+=std::exp(-i*dx/scale)*(i==0||i==samples?.5:1.);return sum*dx;};
                for(int c=0;c<3;++c){const double want=model.rayleigh[c]*column(8,false)*std::exp(-tau[c]);near(r[c],s[at+c],want,want*5e-3);}
                const double mie=model.mieScattering[1]*column(1.2,false)*std::exp(-tau[1]);near("single Mie G",m[at+1],mie,mie*5e-3);
                std::printf("quadrature error at the zenith: Rayleigh %+.2f%%, Mie %+.2f%%\n",100*(column(8,false)/column(8,true)-1),100*(column(1.2,false)/column(1.2,true)-1));
            }else{
                const auto e=read(*gpu,air.table(3),0); // Sky irradiance on the ground, mu_s = 1 (last texel of row 0).
                std::printf("zenith sky radiance factor (no phase) %.5f %.5f %.5f, sky irradiance at noon %.5f %.5f %.5f\n",s[at],s[at+1],s[at+2],e[63*4],e[63*4+1],e[63*4+2]);
                for(int c=0;c<3;++c){if(!(s[at+c]>=0&&std::isfinite(s[at+c])&&e[63*4+c]>0&&std::isfinite(e[63*4+c])))++failures;}
            }
        }
        { // The render functions (programs.cpp `air`) on known rays: radiance relative to the sun.
            model.orders=4;Atmosphere earth(*gpu,model);earth.precompute(*gpu);const Atmosphere& air=earth;
            const std::string code=std::string(albert::gpu::prelude)+albert::gpu::atmosphereWgsl+albert::gpu::air+R"(
@group(2) @binding(0) var<storage, read_write> probe: array<vec4f>;
@compute @workgroup_size(1) fn main() {
    let b = bottomRadius(); let camera = vec3f(0.0, 0.0, b + 0.01); let sun = vec3f(cos(0.9599), 0.0, sin(0.9599)); // Sun 55 degrees up.
    for (var i = 0u; i < 4u; i++) {
        let e = array<f32, 4>(1.5708, 0.4363, 0.0349, -0.0349)[i]; // Zenith, 25 degrees, 2 degrees up, 2 down (ground 0.5 km away? no: the sphere).
        let s = skyRadiance(camera, vec3f(-cos(e), 0.0, sin(e)), sun); probe[2u * i] = vec4f(s.radiance, 0.0); probe[2u * i + 1u] = vec4f(s.transmittance, 0.0);
    }
    // airBetween: a spacecraft 420 km up between a camera 3 radii out and the planet (no air between);
    // from the ground, a body far away 0.5 degrees below the model's horizon (the horizon's air, not black).
    let far = vec3f(0.0, 0.0, 3.0 * b); let craft = vec3f(0.0, 0.0, b + 420.0);
    let a = airBetween(far, craft - far, sun); probe[8] = vec4f(a.radiance, 0.0); probe[9] = vec4f(a.transmittance, 0.0);
    let dip = acos(b / (b + 0.01)) + 0.0087; let low = camera + vec3f(-cos(dip), 0.0, -sin(dip)) * 384400.0;
    let m = airBetween(camera, low - camera, vec3f(-cos(0.0), 0.0, -0.02)); probe[10] = vec4f(m.radiance, 0.0); probe[11] = vec4f(m.transmittance, 0.0);
    // From near the Moon (as the app's frame: 384 400 km, off-axis) to 4096 points within 50 km, in
    // every direction: the Earth's air never lies between (f32 cancellation once made it).
    let moon = vec3f(-135483.594, -113303.617, -353712.875); var wrong = 0.0; var worst = 1.0; // The camera of the app's frame at Tranquility Base, 20 October 2026.
    for (var i = 0u; i < 4096u; i++) {
        let u = fract(sin(vec3f(f32(i) * 12.9898, f32(i) * 78.233, f32(i) * 37.719)) * 43758.5453) * 2.0 - 1.0;
        let toward = moon + normalize(u + vec3f(1e-3)) * (1.0 + 49.0 * fract(f32(i) * 0.618034));
        let t = airBetween(moon, toward - moon, sun).transmittance.g; if (t < 0.999999) { wrong += 1.0; worst = min(worst, t); }
    }
    probe[12] = vec4f(wrong, worst, 0.0, 0.0); probe[13] = vec4f(0.0);
}
)";
            // Far from the Earth and near its ground, in a module of their own (D3D12's compiler garbled the
            // first probes of one shader holding every loop): the frame binds the planet ~1.4e8 km away.
            const std::string farCode=std::string(albert::gpu::prelude)+albert::gpu::atmosphereWgsl+albert::gpu::air+R"(
@group(2) @binding(0) var<storage, read_write> probe: array<vec4f>;
@compute @workgroup_size(1) fn main() {
    let b = bottomRadius(); let sun = vec3f(cos(0.9599), 0.0, sin(0.9599));
    // Near Mars (~1.5e8 km from the Earth): segments from 1 m to 50 km keep a direction (no NaN, no air).
    let mars = vec3f(1.21e8, 3.4e7, -6.3e7); var marsWrong = 0.0;
    for (var i = 0u; i < 4096u; i++) {
        let u = fract(sin(vec3f(f32(i) * 12.9898, f32(i) * 78.233, f32(i) * 37.719)) * 43758.5453) * 2.0 - 1.0;
        let s = airBetween(mars, normalize(u + vec3f(1e-3)) * pow(10.0, -3.0 + 4.7 * fract(f32(i) * 0.618034)), sun);
        if (!(s.transmittance.g == 1.0 && s.radiance.g == 0.0)) { marsWrong += 1.0; }
    }
    // airSky as the app calls it: world points near the camera, the Earth's frame ~1.4e8 km away.
    var skyWrong = 0.0;
    for (var i = 0u; i < 4096u; i++) {
        let u = fract(sin(vec3f(f32(i) * 12.9898, f32(i) * 78.233, f32(i) * 37.719)) * 43758.5453) * 2.0 - 1.0;
        let s = airSky(normalize(u + vec3f(1e-3)) * pow(10.0, -3.0 + 4.7 * fract(f32(i) * 0.618034)));
        if (!(s.transmittance.g == 1.0 && s.radiance.g == 0.0)) { skyWrong += 1.0; }
    }
    // Near the ground (f32 steps of ~0.5 m at 6380 km), off-axis: segments from 1 cm to 300 m keep finite
    // air (review: a segment under one step made point - camera zero, a NaN direction).
    let ground = normalize(vec3f(0.48, -0.61, 0.63)) * (b + 0.002); var lowWrong = 0.0;
    for (var i = 0u; i < 1024u; i++) {
        let u = fract(sin(vec3f(f32(i) * 12.9898, f32(i) * 78.233, f32(i) * 37.719)) * 43758.5453) * 2.0 - 1.0;
        let s = airBetween(ground, normalize(u + vec3f(1e-3)) * pow(10.0, -5.0 + 4.5 * fract(f32(i) * 0.618034)), sun);
        if (!(s.transmittance.g > 0.85 && s.transmittance.g <= 1.0 && s.radiance.g >= 0.0 && s.radiance.g < 1.0)) { lowWrong += 1.0; } // Ground ratios: ~0.92 at 250 m.
    }
    probe[0] = vec4f(marsWrong, skyWrong, lowWrong, 0.0);
}
)";
            WGPUSamplerDescriptor sd{};sd.addressModeU=sd.addressModeV=sd.addressModeW=WGPUAddressMode_ClampToEdge;sd.magFilter=sd.minFilter=WGPUFilterMode_Linear;sd.lodMaxClamp=1;sd.maxAnisotropy=1;
            WGPUSampler sampler=wgpuDeviceCreateSampler(gpu->device(),&sd);
            // The frame (far module only): camera at the world origin, the planet frame's origin ~1.4e8 km away.
            float frame[128]{};frame[76]=frame[81]=frame[86]=frame[91]=1;frame[88]=frame[92]=1.21e8f;frame[89]=frame[93]=3.4e7f;frame[90]=frame[94]=-6.3e7f;frame[95]=1;frame[96]=.57f;frame[98]=.82f;frame[35]=1;
            WGPUBufferDescriptor fd{};fd.size=sizeof frame;fd.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;WGPUBuffer frameBuffer=wgpuDeviceCreateBuffer(gpu->device(),&fd);wgpuQueueWriteBuffer(gpu->queue(),frameBuffer,0,frame,sizeof frame);
            // A frame at the Earth whose air ground is lowered 27 m (the Cape's terrain): identity planet frame.
            float lowered[128]{};lowered[76]=lowered[81]=lowered[86]=lowered[91]=1;lowered[95]=1;lowered[96]=.57f;lowered[98]=.82f;lowered[99]=-.027f;lowered[35]=1;
            WGPUBuffer loweredBuffer=wgpuDeviceCreateBuffer(gpu->device(),&fd);wgpuQueueWriteBuffer(gpu->queue(),loweredBuffer,0,lowered,sizeof lowered);
            auto run=[&](const std::string& source,WGPUBuffer frameIn,const Atmosphere* tables=nullptr,WGPUTextureView cloudMap=nullptr,WGPUSampler cloudSampler=nullptr){ // One dispatch (with a frame, if given); the 64 floats of its probes.
                const Atmosphere& air=tables?*tables:earth;
                const bool withFrame=frameIn!=nullptr;
                WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code={source.data(),source.size()};WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;
                WGPUShaderModule module=wgpuDeviceCreateShaderModule(gpu->device(),&md);
                WGPUComputePipelineDescriptor pd{};pd.compute.module=module;pd.compute.entryPoint={"main",4};WGPUComputePipeline pipe=wgpuDeviceCreateComputePipeline(gpu->device(),&pd);
                WGPUBufferDescriptor bd{};bd.size=256;bd.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc;WGPUBuffer out=wgpuDeviceCreateBuffer(gpu->device(),&bd);
                bd.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;WGPUBuffer back=wgpuDeviceCreateBuffer(gpu->device(),&bd);
                WGPUBindGroupEntry e0[8]{};e0[0].binding=3;e0[0].buffer=air.parameters();e0[0].size=Atmosphere::parameterBytes; // skyRadiance: no irradiance table.
                const WGPUTextureView views[3]{air.transmittance(),air.scattering(),air.mie()};for(std::uint32_t i=0;i<3;++i){e0[1+i].binding=4+i;e0[1+i].textureView=views[i];}
                e0[4].binding=8;e0[4].sampler=sampler;e0[5].binding=0;e0[5].buffer=frameIn;e0[5].size=sizeof frame;
                WGPUBindGroupLayout l0=wgpuComputePipelineGetBindGroupLayout(pipe,0),l2=wgpuComputePipelineGetBindGroupLayout(pipe,2);
                e0[6].binding=9;e0[6].textureView=cloudMap;e0[7].binding=10;e0[7].sampler=cloudSampler;
                WGPUBindGroupDescriptor g0{};g0.layout=l0;g0.entryCount=cloudMap?8u:withFrame?6u:5u;g0.entries=e0;WGPUBindGroup group0=wgpuDeviceCreateBindGroup(gpu->device(),&g0);
                WGPUBindGroupEntry e2{};e2.binding=0;e2.buffer=out;e2.size=256;WGPUBindGroupDescriptor g2{};g2.layout=l2;g2.entryCount=1;g2.entries=&e2;WGPUBindGroup group2=wgpuDeviceCreateBindGroup(gpu->device(),&g2);
                WGPUCommandEncoder en=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);WGPUComputePassEncoder cp=wgpuCommandEncoderBeginComputePass(en,nullptr);
                wgpuComputePassEncoderSetPipeline(cp,pipe);wgpuComputePassEncoderSetBindGroup(cp,0,group0,0,nullptr);wgpuComputePassEncoderSetBindGroup(cp,2,group2,0,nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(cp,1,1,1);wgpuComputePassEncoderEnd(cp);wgpuCommandEncoderCopyBufferToBuffer(en,out,0,back,0,256);
                WGPUCommandBuffer cb=wgpuCommandEncoderFinish(en,nullptr);wgpuQueueSubmit(gpu->queue(),1,&cb);
                bool done=false;WGPUBufferMapCallbackInfo mc{};mc.mode=WGPUCallbackMode_AllowProcessEvents;mc.userdata1=&done;mc.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
                wgpuBufferMapAsync(back,WGPUMapMode_Read,0,256,mc);while(!done)wgpuInstanceProcessEvents(gpu->instance());
                const auto* f=static_cast<const float*>(wgpuBufferGetConstMappedRange(back,0,256));std::vector<float> values(f,f+64);wgpuBufferUnmap(back);
                for(auto g:{group0,group2})wgpuBindGroupRelease(g);for(auto l:{l0,l2})wgpuBindGroupLayoutRelease(l);wgpuCommandBufferRelease(cb);wgpuCommandEncoderRelease(en);wgpuComputePassEncoderRelease(cp);
                for(auto x:{out,back})wgpuBufferRelease(x);wgpuComputePipelineRelease(pipe);wgpuShaderModuleRelease(module);
                return values;};
            const std::string groundCode=std::string(albert::gpu::prelude)+albert::gpu::atmosphereWgsl+albert::gpu::air+R"(
@group(2) @binding(0) var<storage, read_write> probe: array<vec4f>;
@compute @workgroup_size(1) fn main() { // Radii raised by the lowered ground: the terrain 27 m under the sphere sits on the model's ground.
    let b = bottomRadius(); let up = normalize(vec3f(0.48, -0.61, 0.63));
    probe[0] = vec4f(length(airPoint(up * (b - 0.027))) - b, length(airPoint(up * (b + 1.0))) - b, 0.0, 0.0);
    probe[1] = vec4f(skyRadiance(up * (b + 0.01), up, up).transmittance, 0.0); // The tables bound as in the other modules.
}
)";
            // Mars: the dust's optical depth above the datum is the model's 0.5 (with CO2's ~0.003 at 550 nm).
            auto marsModel=albert::gpu::marsAtmosphere();marsModel.orders=1;Atmosphere mars(*gpu,marsModel);mars.precompute(*gpu);
            const std::string marsCode=std::string(albert::gpu::prelude)+albert::gpu::atmosphereWgsl+albert::gpu::air+R"(
@group(2) @binding(0) var<storage, read_write> probe: array<vec4f>;
@compute @workgroup_size(1) fn main() { // Zenith transmittance from the datum (3396.19 km), and the tables bound as elsewhere.
    probe[0] = vec4f(transmittanceToTop(airTransmittance, airSampler, 3396.19, 1.0), 0.0);
    probe[1] = vec4f(skyRadiance(vec3f(0.0, 0.0, 3396.2), vec3f(0.0, 0.0, 1.0), vec3f(0.0, 0.0, 1.0)).radiance, 0.0);
    // Sunset (the sun 2 degrees up): 2 degrees above the sun, and 90 degrees aside 30 degrees up.
    let ground = vec3f(0.0, 0.0, 3396.2); let sun = vec3f(cos(0.0349), 0.0, sin(0.0349));
    probe[2] = vec4f(skyRadiance(ground, vec3f(cos(0.0698), 0.0, sin(0.0698)), sun).radiance, 0.0);
    probe[3] = vec4f(skyRadiance(ground, vec3f(0.0, cos(0.5236), sin(0.5236)), sun).radiance, 0.0);
}
)";
            // Cloud shadows read the layer's map in geodetic latitude (as its shell): a map whose density is v,
            // a body of axis ratio 0.5, a point at parametric latitude 45 degrees under the layer, the sun overhead.
            float shadowFrame[128]{};shadowFrame[76]=shadowFrame[81]=shadowFrame[86]=shadowFrame[91]=1;shadowFrame[95]=1;shadowFrame[96]=shadowFrame[97]=std::sqrt(.5f);
            shadowFrame[100]=12;shadowFrame[101]=1;shadowFrame[103]=.5f;
            WGPUBuffer shadowBuffer=wgpuDeviceCreateBuffer(gpu->device(),&fd);wgpuQueueWriteBuffer(gpu->queue(),shadowBuffer,0,shadowFrame,sizeof shadowFrame);
            constexpr unsigned rows=256;std::vector<unsigned char> gradient(4*rows*4);for(unsigned r=0;r<rows;++r)for(unsigned c=0;c<4;++c){auto* t=&gradient[4*(r*4+c)];t[0]=t[2]=0;t[1]=static_cast<unsigned char>(std::lround((r+.5)/rows*255));t[3]=255;}
            WGPUTextureDescriptor cd{};cd.size={4,rows,1};cd.format=WGPUTextureFormat_RGBA8Unorm;cd.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;cd.mipLevelCount=1;cd.sampleCount=1;cd.dimension=WGPUTextureDimension_2D;
            WGPUTexture cloudTexture=wgpuDeviceCreateTexture(gpu->device(),&cd);
            {WGPUTexelCopyTextureInfo dst{};dst.texture=cloudTexture;WGPUTexelCopyBufferLayout l{};l.bytesPerRow=16;l.rowsPerImage=rows;const WGPUExtent3D e{4,rows,1};wgpuQueueWriteTexture(gpu->queue(),&dst,gradient.data(),gradient.size(),&l,&e);}
            WGPUTextureView cloudView=wgpuTextureCreateView(cloudTexture,nullptr);WGPUSamplerDescriptor cs{};cs.addressModeU=WGPUAddressMode_Repeat;cs.addressModeV=cs.addressModeW=WGPUAddressMode_ClampToEdge;
            cs.magFilter=cs.minFilter=WGPUFilterMode_Linear;cs.lodMaxClamp=1;cs.maxAnisotropy=1;WGPUSampler cloudSampler=wgpuDeviceCreateSampler(gpu->device(),&cs);
            const std::string shadowCode=std::string(albert::gpu::prelude)+albert::gpu::atmosphereWgsl+albert::gpu::air+R"(
@group(2) @binding(0) var<storage, read_write> probe: array<vec4f>;
@compute @workgroup_size(1) fn main() {
    let p = normalize(vec3f(1.0, 1.0, 0.0)) * (bottomRadius() + 0.001);
    probe[0] = vec4f(1.0 - cloudShadow(p, length(p)), 0.0, 0.0, 0.0); // The map's v where the sun ray crosses the layer.
    probe[1] = vec4f(skyRadiance(p, normalize(p), normalize(p)).transmittance, 0.0); // The tables bound as elsewhere.
}
)";
            const auto values=run(code,nullptr),farValues=run(farCode,frameBuffer),groundValues=run(groundCode,loweredBuffer),marsValues=run(marsCode,nullptr,&mars),
                shadowValues=run(shadowCode,shadowBuffer,nullptr,cloudView,cloudSampler);const float* v=values.data();
            {const double geodetic=std::atan2(1.,.5)/3.14159265358979+.5;std::printf("cloud map read at v %.4f (geodetic %.4f, parametric 0.7500)\n",shadowValues[0],geodetic);
             near("Cloud shadows read the layer's map in geodetic latitude",shadowValues[0],geodetic,.006);}
            {const double h=11.1,above=marsModel.top-3396.19,column=h*(1-std::exp(-above/h)),depth=.5*(1-std::exp(-above/h))+2.5e-4*column;
             std::printf("Mars zenith transmittance from the datum %.4f %.4f %.4f (want %.4f at 550 nm)\n",marsValues[0],marsValues[1],marsValues[2],std::exp(-depth));
             near("Mars: dust optical depth 0.5 above the datum, as the model states",marsValues[1],std::exp(-depth),.003);
             const float* a=&marsValues[8];const float* b=&marsValues[12];
             std::printf("Mars sunset: beside the sun %.3g %.3g %.3g (blue/red %.2f), aside %.3g %.3g %.3g (blue/red %.2f)\n",a[0],a[1],a[2],a[2]/a[0],b[0],b[1],b[2],b[2]/b[0]);
             near("Mars sunset: blue glow beside the sun (blue over red)",a[2]>a[0]&&b[2]<b[0]?1:0,1,.5);}
            std::printf("lowered air ground: terrain 27 m under the sphere at %.4f km, 1 km above it at %.4f km\n",groundValues[0],groundValues[1]);
            near("Lowered air ground: the terrain under the camera on the model's ground",groundValues[0],0,1e-3);
            near("... and every radius raised alike",groundValues[1],1.027,1e-3);const char* names[]{"zenith","25 deg","2 deg","-2 deg"};
            for(int i=0;i<4;++i)std::printf("sky %-7s radiance %.4f %.4f %.4f  transmittance %.3f %.3f %.3f\n",names[i],v[8*i],v[8*i+1],v[8*i+2],v[8*i+4],v[8*i+5],v[8*i+6]);
            std::printf("craft before the air: radiance %.4f transmittance %.3f; body below the horizon at sunset: transmittance %.3g %.3g %.3g\n",v[33],v[37],v[44],v[45],v[46]);
            near("No air between a camera in space and a craft in front of the air",v[37]+v[33],1,1e-6);
            std::printf("near the Moon: %.0f of 4096 short segments saw the Earth's air (lowest transmittance %.3g)\n",v[48],v[49]);
            near("No Earth air between points near the Moon (f32 cancellation)",v[48],0,.5);
            std::printf("near Mars: %.0f of 4096 segments (1 m to 50 km) not clear\n",farValues[0]);
            near("Short segments near Mars keep their direction (no NaN, no air)",farValues[0],0,.5);
            std::printf("airSky near Mars: %.0f of 4096 world points not clear\n",farValues[1]);
            near("Aerial perspective near Mars: no NaN from the far planet frame (f32 collapse)",farValues[1],0,.5);
            std::printf("near the ground: %.0f of 1024 short segments not finite\n",farValues[2]);
            near("Segments shorter than an f32 step at the ground stay finite",farValues[2],0,.5);
            if(!(v[44]>0&&std::isfinite(v[44])&&std::isfinite(v[40]))){std::printf("FAIL body below the model horizon is black or not finite\n");++failures;}else std::printf("ok   body below the model horizon sees the horizon's air\n");
            { // Lunar photometry (McEwen's lunar-Lambert) summed over the disc in the shader against the Moon's measured phase curve,
              // normalised at 30 degrees: ROLO (Kieffer and Stone 2005, 553.8 nm, zero libration, from rimopy's coefficients) and
              // Lane and Irvine (1973, Table V, V band); and earthshine from the Earth 384,400 km away (60.27 radii).
                float shine[128]{};shine[0]=shine[5]=shine[10]=shine[15]=1;shine[40]=1;shine[43]=1;shine[107]=1; // View identity, the sun towards +x, the Earth at the origin.
                WGPUBuffer shineBuffer=wgpuDeviceCreateBuffer(gpu->device(),&fd);wgpuQueueWriteBuffer(gpu->queue(),shineBuffer,0,shine,sizeof shine);
                const std::string lunarCode=std::string(albert::gpu::prelude)+albert::gpu::atmosphereWgsl+albert::gpu::air+albert::gpu::photometry+R"(
@group(2) @binding(0) var<storage, read_write> probe: array<vec4f>;
fn disc(alpha: f32) -> f32 { // A uniform R30 = 0.064 disc seen along +z, the sun `alpha` degrees off the line of sight: I/F summed over its projection.
    let n = 160; let l = vec3f(sin(radians(alpha)), 0.0, cos(radians(alpha))); var sum = 0.0;
    for (var i = 0; i < n; i++) { for (var j = 0; j < n; j++) {
        let x = (f32(i) + 0.5) / f32(n) * 2.0 - 1.0; let y = (f32(j) + 0.5) / f32(n) * 2.0 - 1.0; let s = x * x + y * y;
        if (s < 1.0) { let p = vec3f(x, y, sqrt(1.0 - s)); let mu0 = dot(p, l); if (mu0 > 0.0) { sum += lunarFactor(mu0, p.z, alpha, 0.064); } } } }
    return sum;
}
@compute @workgroup_size(1) fn main() {
    let angles = array<f32, 16>(1.55, 7.0, 10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 97.0, 100.0, 110.0, 120.0, 0.0);
    for (var k = 0; k < 16; k++) { probe[k / 4][k % 4] = disc(angles[k]); }
    // Full phase along the equator, 0.5, 0.8 and 0.95 of the radius out, and the centre: no limb darkening.
    probe[4] = vec4f(lunarFactor(0.866, 0.866, 0.0, 0.064), lunarFactor(0.6, 0.6, 0.0, 0.064), lunarFactor(0.3122, 0.3122, 0.0, 0.064), lunarFactor(1.0, 1.0, 0.0, 0.064));
    // Earthshine at full Earth and at quadrature 60.27 radii out, and 1.2 radii out (the air's domain); towards the Earth.
    probe[5] = vec4f(earthLight(vec3f(60.27, 0.0, 0.0)).w, earthLight(vec3f(0.0, 60.27, 0.0)).w, earthLight(vec3f(1.2, 0.0, 0.0)).w, 0.0);
    probe[6] = vec4f(earthLight(vec3f(60.27, 0.0, 0.0)).xyz, 0.0);
    probe[7] = vec4f(skyRadiance(vec3f(0.0, 0.0, 6400.0), vec3f(0.0, 0.0, 1.0), vec3f(0.0, 0.0, 1.0)).transmittance, 0.0); // The tables bound as elsewhere.
}
)";
                const auto lunar=run(lunarCode,shineBuffer);wgpuBufferRelease(shineBuffer);
                const double angles[]{1.55,7,10,20,30,40,50,60,70,80,90,97,100,110,120};
                auto at=[&](double alpha){for(int k=0;k<15;++k)if(angles[k]==alpha)return -2.5*std::log10(lunar[static_cast<std::size_t>(k)]/lunar[4]);return 1e9;}; // Magnitudes past 30 degrees.
                const double rolo[][2]{{20,-.263},{40,.262},{50,.530},{60,.810},{70,1.109},{80,1.434},{90,1.793},{97,2.067}};
                const double lane[][2]{{20,-.27},{40,.27},{50,.51},{60,.77},{70,1.04},{80,1.34},{90,1.68},{100,2.07},{110,2.52}};
                for(const auto& r:rolo){std::printf("lunar phase %3.0f deg: %+.3f mag (ROLO %+.3f)\n",r[0],at(r[0]),r[1]);near("Lunar phase curve against ROLO (0.06 mag)",at(r[0]),r[1],.06);}
                for(const auto& r:lane){std::printf("lunar phase %3.0f deg: %+.3f mag (Lane and Irvine %+.3f)\n",r[0],at(r[0]),r[1]);near("Lunar phase curve against Lane and Irvine (0.15 mag)",at(r[0]),r[1],.15);}
                const double full=lunar[1]/lunar[10],opposition=-2.5*std::log10(lunar[0]/lunar[4]);
                std::printf("full (7 deg) over quarter (90 deg) Moon: %.2f (ROLO 9.6, Lane and Irvine 9.8, Lambert 3.14); 1.55 deg: %+.3f mag (ROLO %+.3f)\n",full,opposition,-2.5*std::log10(2.50));
                near("Full Moon about ten times the quarter",full,10.3,1.3);near("Opposition surge against ROLO at 1.55 deg (0.2 mag)",opposition,-2.5*std::log10(2.50),.2);
                for(int i=0;i<3;++i)near("No limb darkening at full phase",lunar[16+i]/lunar[19],1,.01);
                std::printf("earthshine at 384,400 km: full Earth %.4g, quadrature %.4g of the sunlight (0.162 W/m2 at full Earth: %.4g); 1.2 radii: %.3g\n",lunar[20],lunar[21],1.1948e-4,lunar[22]);
                near("Earthshine at full Earth: 0.434 (R/d)^2",lunar[20],.434/(60.27*60.27),1e-6);near("... at quadrature, Mallama and Hilton's curve",lunar[21],.434*std::pow(10.,.4*90*(1.060e-3-2.054e-4*90))/(60.27*60.27),5e-7);
                near("... none within the air's reach",lunar[22],0,0);near("... towards the Earth",lunar[24],-1,1e-6);
            }
            // The scene renderer with this air: a camera 10 m above a grey globe of radius 1 (6378.137 km)
            // facing the horizon, the sun 30 degrees up behind it, a red box in front of the sky.
            using namespace threepp;
            albert::gpu::Program sky;sky.background=true;sky.attributes={{"normal",3}};
            sky.wgsl=R"(
struct V { @builtin(position) clip: vec4f, @location(0) world: vec3f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f) -> V { var o: V; let w = object.model * vec4f(position, 1.0); o.world = w.xyz; o.clip = frame.projection * frame.view * w; return o; }
@fragment fn fs_main(v: V) -> @location(0) vec4f { if (frame.air.w <= 0.0) { discard; } let s = skyRadiance(frame.air.xyz, normalize(airPoint(v.world) - frame.air.xyz), frame.airSun.xyz); return vec4f(finish(s.radiance * frame.sunColor.rgb), 1.0); }
)";
            albert::gpu::define("test:sky",sky);
            constexpr unsigned iw=96,ih=64;
            WGPUTextureDescriptor td{};td.size={iw,ih,1};td.format=gpu->format();td.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;td.mipLevelCount=1;td.sampleCount=1;td.dimension=WGPUTextureDimension_2D;
            WGPUTexture target=wgpuDeviceCreateTexture(gpu->device(),&td);WGPUTextureView view=wgpuTextureCreateView(target,nullptr);
            albert::gpu::SceneRenderer renderer(*gpu);renderer.samples=1;
            Scene scene;auto globeMaterial=MeshStandardMaterial::create();globeMaterial->color=Color(.3f,.3f,.3f);globeMaterial->roughness=1;
            auto globe=Mesh::create(SphereGeometry::create(1,512,256),globeMaterial);scene.add(globe);
            auto shellMaterial=ShaderMaterial::create();shellMaterial->name="test:sky";globe->add(Mesh::create(SphereGeometry::create(1.0126f,64,32),shellMaterial));
            auto boxMaterial=MeshBasicMaterial::create();boxMaterial->color=Color(1,0,0);auto box=Mesh::create(BoxGeometry::create(2e-6f,2e-6f,2e-6f),boxMaterial);
            const float ground=1.f+1e-5f/6.378137f; // 10 m up.
            box->position.set(-1.6e-5f,ground+2e-6f,-3e-5f);scene.add(box);
            auto sunlight=DirectionalLight::create(0xffffff,2.4f);sunlight->position.set(0,std::sin(.5236f),std::cos(.5236f));scene.add(sunlight); // Toward the default target, the origin.
            PerspectiveCamera camera(50,static_cast<float>(iw)/ih,1e-7f,10);camera.position.set(0,ground,0);camera.lookAt(Vector3(0,ground,-1));
            threepp::Texture* clouds=nullptr;float cloudOpacity=0,wind=0; // Cloud shadows: a layer at 12 km; the sea's wind.
            auto frameOf=[&](float strength){renderer.air={&earth,globe.get(),6378.137f,strength};renderer.air.clouds=clouds;renderer.air.cloudShadow=cloudOpacity;renderer.air.surfaceWind=wind;
                WGPUCommandEncoder en2=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(scene,camera,view,iw,ih,en2);
                WGPUCommandBuffer cb2=wgpuCommandEncoderFinish(en2,nullptr);wgpuQueueSubmit(gpu->queue(),1,&cb2);wgpuCommandBufferRelease(cb2);wgpuCommandEncoderRelease(en2);
                return readPixels(*gpu,target,iw,ih);};
            const auto plain=frameOf(0),hazy=frameOf(1);
            auto px=[&](const std::vector<unsigned char>& im,unsigned x,unsigned y){const auto* q=&im[(static_cast<std::size_t>(y)*iw+x)*4];return std::array<int,3>{q[0],q[1],q[2]};};
            const auto skyPixel=px(hazy,80,10),groundPlain=px(plain,48,60),groundHazy=px(hazy,48,60),boxPixel=px(hazy,12,27),noSky=px(plain,80,10);
            std::printf("scene: sky %d %d %d (none %d %d %d), ground %d %d %d -> %d %d %d, box %d %d %d\n",skyPixel[0],skyPixel[1],skyPixel[2],noSky[0],noSky[1],noSky[2],
                groundPlain[0],groundPlain[1],groundPlain[2],groundHazy[0],groundHazy[1],groundHazy[2],boxPixel[0],boxPixel[1],boxPixel[2]);
            auto check=[&](bool ok,const char* what){std::printf("%s %s\n",ok?"ok  ":"FAIL",what);if(!ok)++failures;};
            check(skyPixel[2]>skyPixel[0]+20&&skyPixel[2]>60&&noSky[2]<10,"Daylight sky drawn as the background, blue");
            check(boxPixel[0]>boxPixel[2]+60,"An object in front of the sky keeps its colour");
            // Near ground (tens of metres): the same order of brightness, warmer (sunlight through the air), not the sky.
            check(groundHazy!=groundPlain&&groundHazy[0]>groundPlain[0]/2&&groundHazy[0]<groundPlain[0]*3/2&&groundHazy[0]>=groundHazy[2],"Ground lit through the air, not covered by the sky");
            // Smooth ground (water) mirrors the sky near the horizon: bluer and brighter than rough ground.
            globeMaterial->roughness=.05f;const auto smooth=frameOf(1);globeMaterial->roughness=1;const auto rough=frameOf(1);
            const auto water=px(smooth,48,36),soil=px(rough,48,36);
            std::printf("near the horizon: smooth %d %d %d, rough %d %d %d\n",water[0],water[1],water[2],soil[0],soil[1],soil[2]);
            check(water[2]>soil[2]+10&&water[2]-water[0]>soil[2]-soil[0],"Smooth water mirrors the sky (bluer, brighter)");
            { // Under an overcast layer (density 1, opacity 0.88) the ground loses most of its sunlight, keeping the sky's.
                auto overcast=DataTexture::create(ImageData(std::vector<unsigned char>(8*4*4,255)),8,4);overcast->needsUpdate();
                clouds=overcast.get();cloudOpacity=.88f;const auto shaded=px(frameOf(1),48,60);clouds=nullptr;cloudOpacity=0;const auto clear=px(frameOf(1),48,60);
                std::printf("ground under an overcast layer %d %d %d, clear %d %d %d\n",shaded[0],shaded[1],shaded[2],clear[0],clear[1],clear[2]);
                check(shaded[0]<clear[0]*7/10&&shaded[0]>0,"Clouds shadow the ground (sunlight through the layer)");
            }
            { // The sea's glitter (water: roughness texels under 0.5) widens with the wind (Cox-Munk): the sun 5
              // degrees up ahead; at 15 m/s the glint is dimmer at its peak and spread over more pixels than at 1 m/s.
                auto water=MeshStandardMaterial::create();water->color=Color(.02f,.05f,.1f);water->roughness=1;water->name="albert:water";
                auto mask=DataTexture::create(ImageData(std::vector<unsigned char>(8*4*4,50)),8,4);mask->needsUpdate();water->roughnessMap=mask;globe->setMaterial(water);
                sunlight->position.set(0,std::sin(.0873f),-std::cos(.0873f));
                const float exposure=renderer.exposure;renderer.exposure=exposure*.02f; // The glint unclipped.
                auto glint=[&](float w){wind=w;const auto im=frameOf(1);int peak=0,half=0;for(unsigned y=ih/2;y<ih;++y)for(unsigned x=0;x<iw;++x)peak=std::max(peak,px(im,x,y)[0]);
                    for(unsigned y=ih/2;y<ih;++y)for(unsigned x=0;x<iw;++x)half+=px(im,x,y)[0]*2>peak;return std::pair{peak,half};};
                const auto [calmPeak,calmArea]=glint(1);const auto [windyPeak,windyArea]=glint(15);wind=0;
                std::printf("sea glint: 1 m/s peak %d (%d pixels over half), 15 m/s peak %d (%d pixels over half)\n",calmPeak,calmArea,windyPeak,windyArea);
                check(calmPeak>windyPeak*3/2&&windyArea>calmArea&&windyPeak<255,"The sea's glitter widens and dims with the wind (Cox-Munk)");
                water->name="vehicle paint";const auto [paintCalm,paintCalmArea]=glint(1);const auto [paintWindy,paintWindyArea]=glint(15);wind=0;renderer.exposure=exposure;
                std::printf("a smooth roughness map not named water: 1 m/s peak %d, 15 m/s peak %d\n",paintCalm,paintWindy);
                check(paintCalm==paintWindy&&paintCalmArea==paintWindyArea,"Only water materials take the sea's wind (a vehicle's roughness map keeps its own)");
                globe->setMaterial(globeMaterial);sunlight->position.set(0,std::sin(.5236f),std::cos(.5236f));
            }
            wgpuTextureViewRelease(view);wgpuTextureRelease(target);
        }
        if(albert::gpu::errors()){std::printf("FAIL WebGPU errors: %u\n",albert::gpu::errors());++failures;}
    }catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}
    std::printf(failures?"%d failure(s)\n":"atmosphere ok\n",failures);return failures?1:0;
}
