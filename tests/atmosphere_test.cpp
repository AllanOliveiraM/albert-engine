// Bruneton tables precomputed with WebGPU compute, read back and checked against closed forms:
// the zenith transmittance from the ground (exponential profiles and the ozone tent) and the single
// scattering of a vertical ray with the sun at the zenith (T(0,top) times the optical depth).
// Exits 77 (skipped) without a WebGPU adapter.
#include "gpu/atmosphere.hpp"
#include "gpu/programs.hpp"
#include "gpu/scene.hpp"
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
            model.orders=4;Atmosphere air(*gpu,model);air.precompute(*gpu);
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
    probe[12] = vec4f(wrong, worst, marsWrong, skyWrong); probe[13] = vec4f(lowWrong, 0.0, 0.0, 0.0);
}
)";
            WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code={code.data(),code.size()};WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;
            WGPUShaderModule module=wgpuDeviceCreateShaderModule(gpu->device(),&md);
            WGPUComputePipelineDescriptor pd{};pd.compute.module=module;pd.compute.entryPoint={"main",4};WGPUComputePipeline pipe=wgpuDeviceCreateComputePipeline(gpu->device(),&pd);
            WGPUBufferDescriptor bd{};bd.size=256;bd.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc;WGPUBuffer out=wgpuDeviceCreateBuffer(gpu->device(),&bd);
            bd.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;WGPUBuffer back=wgpuDeviceCreateBuffer(gpu->device(),&bd);
            WGPUSamplerDescriptor sd{};sd.addressModeU=sd.addressModeV=sd.addressModeW=WGPUAddressMode_ClampToEdge;sd.magFilter=sd.minFilter=WGPUFilterMode_Linear;sd.lodMaxClamp=1;sd.maxAnisotropy=1;
            WGPUSampler sampler=wgpuDeviceCreateSampler(gpu->device(),&sd);
            // The frame: camera at the world origin, the planet frame's origin (the Earth) ~1.4e8 km away (Mars).
            float frame[128]{};frame[76]=frame[81]=frame[86]=frame[91]=1;frame[88]=frame[92]=1.21e8f;frame[89]=frame[93]=3.4e7f;frame[90]=frame[94]=-6.3e7f;frame[95]=1;frame[96]=.57f;frame[98]=.82f;frame[35]=1;
            bd.size=sizeof frame;bd.usage=WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst;WGPUBuffer frameBuffer=wgpuDeviceCreateBuffer(gpu->device(),&bd);wgpuQueueWriteBuffer(gpu->queue(),frameBuffer,0,frame,sizeof frame);
            WGPUBindGroupEntry e0[6]{};e0[0].binding=3;e0[0].buffer=air.parameters();e0[0].size=Atmosphere::parameterBytes; // skyRadiance: no irradiance table.
            const WGPUTextureView views[3]{air.transmittance(),air.scattering(),air.mie()};for(std::uint32_t i=0;i<3;++i){e0[1+i].binding=4+i;e0[1+i].textureView=views[i];}
            e0[4].binding=8;e0[4].sampler=sampler;e0[5].binding=0;e0[5].buffer=frameBuffer;e0[5].size=sizeof frame;
            WGPUBindGroupLayout l0=wgpuComputePipelineGetBindGroupLayout(pipe,0),l2=wgpuComputePipelineGetBindGroupLayout(pipe,2);
            WGPUBindGroupDescriptor g0{};g0.layout=l0;g0.entryCount=6;g0.entries=e0;WGPUBindGroup group0=wgpuDeviceCreateBindGroup(gpu->device(),&g0);
            WGPUBindGroupEntry e2{};e2.binding=0;e2.buffer=out;e2.size=256;WGPUBindGroupDescriptor g2{};g2.layout=l2;g2.entryCount=1;g2.entries=&e2;WGPUBindGroup group2=wgpuDeviceCreateBindGroup(gpu->device(),&g2);
            WGPUCommandEncoder en=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);WGPUComputePassEncoder cp=wgpuCommandEncoderBeginComputePass(en,nullptr);
            wgpuComputePassEncoderSetPipeline(cp,pipe);wgpuComputePassEncoderSetBindGroup(cp,0,group0,0,nullptr);wgpuComputePassEncoderSetBindGroup(cp,2,group2,0,nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(cp,1,1,1);wgpuComputePassEncoderEnd(cp);wgpuCommandEncoderCopyBufferToBuffer(en,out,0,back,0,256);
            WGPUCommandBuffer cb=wgpuCommandEncoderFinish(en,nullptr);wgpuQueueSubmit(gpu->queue(),1,&cb);
            bool done=false;WGPUBufferMapCallbackInfo mc{};mc.mode=WGPUCallbackMode_AllowProcessEvents;mc.userdata1=&done;mc.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
            wgpuBufferMapAsync(back,WGPUMapMode_Read,0,256,mc);while(!done)wgpuInstanceProcessEvents(gpu->instance());
            const auto* v=static_cast<const float*>(wgpuBufferGetConstMappedRange(back,0,256));const char* names[]{"zenith","25 deg","2 deg","-2 deg"};
            for(int i=0;i<4;++i)std::printf("sky %-7s radiance %.4f %.4f %.4f  transmittance %.3f %.3f %.3f\n",names[i],v[8*i],v[8*i+1],v[8*i+2],v[8*i+4],v[8*i+5],v[8*i+6]);
            std::printf("craft before the air: radiance %.4f transmittance %.3f; body below the horizon at sunset: transmittance %.3g %.3g %.3g\n",v[33],v[37],v[44],v[45],v[46]);
            near("No air between a camera in space and a craft in front of the air",v[37]+v[33],1,1e-6);
            std::printf("near the Moon: %.0f of 4096 short segments saw the Earth's air (lowest transmittance %.3g)\n",v[48],v[49]);
            near("No Earth air between points near the Moon (f32 cancellation)",v[48],0,.5);
            std::printf("near Mars: %.0f of 4096 segments (1 m to 50 km) not clear\n",v[50]);
            near("Short segments near Mars keep their direction (no NaN, no air)",v[50],0,.5);
            std::printf("airSky near Mars: %.0f of 4096 world points not clear\n",v[51]);
            near("Aerial perspective near Mars: no NaN from the far planet frame (f32 collapse)",v[51],0,.5);
            std::printf("near the ground: %.0f of 1024 short segments not finite\n",v[52]);
            near("Segments shorter than an f32 step at the ground stay finite",v[52],0,.5);
            if(!(v[44]>0&&std::isfinite(v[44])&&std::isfinite(v[40]))){std::printf("FAIL body below the model horizon is black or not finite\n");++failures;}else std::printf("ok   body below the model horizon sees the horizon's air\n");
            wgpuBufferUnmap(back);
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
            auto frameOf=[&](float strength){renderer.air={&air,globe.get(),6378.137f,strength};
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
            wgpuTextureViewRelease(view);wgpuTextureRelease(target);
        }
        if(albert::gpu::errors()){std::printf("FAIL WebGPU errors: %u\n",albert::gpu::errors());++failures;}
    }catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}
    std::printf(failures?"%d failure(s)\n":"atmosphere ok\n",failures);return failures?1:0;
}
