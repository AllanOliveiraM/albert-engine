// Volumetric clouds (gpu::Clouds) through the scene renderer, read back: a flat slab of uniform cloud under a camera 20 km
// over the pole, the sun at the zenith and the air's light nearly off (strength 1e-4, so the cloud's own physics shows):
// its transmittance along the view against Beer-Lambert, its reflectance at nadir (π L / E) against the δ-Eddington flux
// albedo of a conservative slab of g 0.85 (Joseph, Wiscombe and Weinman 1976) within 20%, its depth inside the slab; with the
// noise on, the fraction of the view behind cloud from above against the map's cover; and the composite: the slab brightens the frame where it is seen and a box nearer the eye hides it. Exits 77 (skipped) without a
// WebGPU adapter. With a path prefix as argument it also writes views of noisy cumulus under full air (<prefix>-*.ppm) to look at.
#include "gpu/atmosphere.hpp"
#include "gpu/clouds.hpp"
#include "gpu/context.hpp"
#include "gpu/programs.hpp"
#include "gpu/scene.hpp"
#include <webgpu/wgpu.h>
#include "threepp/textures/DataTexture.hpp"
#include <threepp/threepp.hpp>
#include <threepp/materials/ShaderMaterial.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
int failures=0;
void near(double got,double want,double tolerance,const std::string& what){const bool ok=std::abs(got-want)<=tolerance;
    std::printf("  %-58s %9.4f  reference %9.4f%s\n",what.c_str(),got,want,ok?"":"  <-- outside");if(!ok)++failures;}
void check(bool ok,const std::string& what){std::printf("  %s %s\n",ok?"ok  ":"FAIL",what.c_str());if(!ok)++failures;}
float half(std::uint16_t h){const std::uint32_t e=(h>>10)&0x1f,m=h&0x3ff;const float s=(h&0x8000)?-1.f:1.f;
    if(e==0)return s*std::ldexp(static_cast<float>(m),-24);if(e==31)return s*INFINITY;return s*std::ldexp(static_cast<float>(m|1024),static_cast<int>(e)-25);}
std::vector<std::uint8_t> read(albert::gpu::Context& gpu,WGPUTexture texture,unsigned w,unsigned h,unsigned texel){ // Rows unpadded.
    const unsigned row=(w*texel+255)/256*256;WGPUBufferDescriptor bd{};bd.size=static_cast<std::uint64_t>(row)*h;bd.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;
    WGPUBuffer b=wgpuDeviceCreateBuffer(gpu.device(),&bd);WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);
    WGPUTexelCopyTextureInfo src{};src.texture=texture;WGPUTexelCopyBufferInfo dst{};dst.buffer=b;dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=h;const WGPUExtent3D size{w,h,1};
    wgpuCommandEncoderCopyTextureToBuffer(e,&src,&dst,&size);WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu.queue(),1,&c);
    bool done=false;WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&done;cb.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
    wgpuBufferMapAsync(b,WGPUMapMode_Read,0,bd.size,cb);while(!done)wgpuInstanceProcessEvents(gpu.instance());
    const auto* p=static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(b,0,bd.size));std::vector<std::uint8_t> out(static_cast<std::size_t>(w)*h*texel);
    for(unsigned y=0;y<h;++y)std::memcpy(out.data()+static_cast<std::size_t>(y)*w*texel,p+static_cast<std::size_t>(y)*row,static_cast<std::size_t>(w)*texel);
    wgpuBufferUnmap(b);wgpuBufferRelease(b);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);return out;
}
double deltaEddington(double tau,double g,double mu0){ // Flux albedo of a conservative slab over a black ground.
    const double e=std::exp(-(1-g*g)*tau/mu0);return ((1-g)*tau+(2./3-mu0)*(1-e))/((1-g)*tau+4./3);}
}
int main(int argc,char** argv){try{
    std::setvbuf(stdout,nullptr,_IONBF,0); // Each line out before a driver's abort (CI keeps the output's tail).
    using namespace threepp;
    std::unique_ptr<albert::gpu::Context> gpu;try{gpu=std::make_unique<albert::gpu::Context>();}catch(const std::exception& e){std::printf("skipped: %s\n",e.what());return 77;}
    std::printf("Volumetric clouds on %s:\n",gpu->adapter().c_str());
    auto model=albert::gpu::earthAtmosphere();model.orders=1;albert::gpu::Atmosphere air(*gpu,model);air.precompute(*gpu);
    constexpr unsigned size=32;constexpr double km=1/6378.137;
    WGPUTextureDescriptor td{};td.size={size,size,1};td.format=gpu->format();td.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;td.mipLevelCount=1;td.sampleCount=1;td.dimension=WGPUTextureDimension_2D;
    WGPUTexture target=wgpuDeviceCreateTexture(gpu->device(),&td);WGPUTextureView view=wgpuTextureCreateView(target,nullptr);
    albert::gpu::SceneRenderer renderer(*gpu);renderer.samples=1;renderer.toneMapping=false;
    Scene scene;auto ground=MeshStandardMaterial::create();ground->color=Color(.1f,.1f,.1f);ground->roughness=1;
    auto globe=Mesh::create(SphereGeometry::create(1,256,128),ground);scene.add(globe);
    auto sunlight=DirectionalLight::create(0xffffff,1.f);sunlight->position.set(0,10,0);scene.add(sunlight); // The sun at the pole's zenith.
    auto overcast=DataTexture::create(ImageData(std::vector<unsigned char>(8*4*4,255)),8,4);overcast->needsUpdate(); // Full cover.
    PerspectiveCamera camera(30,1,1e-7f,10);camera.position.set(0,static_cast<float>(1+20*km),0);camera.up.set(0,0,1);camera.lookAt(Vector3(0,0,0));
    renderer.air={&air,globe.get(),6378.137f,1e-4f};renderer.air.clouds=overcast.get();renderer.air.cloudShadow=.88f;
    auto& v=renderer.volume;v.enabled=true;v.flat=true;v.divisor=1;v.steps=256;v.baseKm=2;v.topKm=4;
    auto frame=[&](){WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(scene,camera,view,size,size,e);
        WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu->queue(),1,&c);renderer.submitted();wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);};
    struct Look {double radiance,transmittance,depthKm;};
    auto look=[&](double tau,double thickness){v.topKm=static_cast<float>(2+thickness);v.extinction=static_cast<float>(tau/thickness);frame();
        const auto* clouds=renderer.clouds();if(!clouds)throw std::runtime_error("no clouds were marched");
        const auto c=read(*gpu,clouds->colour(),size,size,8);const auto d=read(*gpu,clouds->depth(),size,size,4);const std::size_t i=(size/2)*size+size/2;
        std::uint16_t h[4];std::memcpy(h,c.data()+i*8,8);float z;std::memcpy(&z,d.data()+i*4,4);
        return Look{(half(h[0])+half(h[1])+half(h[2]))/3.,half(h[3]),z*6378.137};};
    { // The noise is made on a thread: frames until it is ready draw no clouds.
        auto ms=[](auto t0){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();};
        auto t0=std::chrono::steady_clock::now();(void)albert::gpu::Clouds::noise();const double made=ms(t0);
        frame();check(!renderer.cloudsShown(),"The first frame waits for the noise (no clouds yet)");
        double built=0;for(int i=0;i<600&&!renderer.cloudsShown();++i){std::this_thread::sleep_for(std::chrono::milliseconds(10));t0=std::chrono::steady_clock::now();frame();built=ms(t0);}
        check(renderer.cloudsShown(),"Then the clouds show");
        std::printf(" noise %.0f ms (on a thread); the frame making the clouds' GPU objects %.0f ms\n",made,built);
    }
    std::printf(" view transmittance (Beer-Lambert):\n");
    for(const auto [tau,h]:{std::pair{1.,2.},std::pair{3.,2.}}){const auto l=look(tau,h);
        near(l.transmittance,std::exp(-tau),std::exp(-tau)*.01,"tau "+std::to_string(static_cast<int>(tau))+" over "+std::to_string(static_cast<int>(h))+" km");}
    std::printf(" nadir reflectance pi L / E, the sun at the zenith (delta-Eddington flux albedo, g 0.85):\n");
    for(const double tau:{10.,20.,50.}){const double h=tau/10,want=deltaEddington(tau,.85,1);const auto l=look(tau,h);
        near(3.14159265358979*l.radiance,want,want*.2,"tau "+std::to_string(static_cast<int>(tau)));
        check(l.depthKm>20-2-h&&l.depthKm<20-2,"its depth ("+std::to_string(l.depthKm).substr(0,6)+" km) inside the slab");}
    { // The composite: the slab over the dark ground, and a red box nearer than it.
        auto pixel=[&](){const auto p=read(*gpu,target,size,size,4);const std::size_t i=((size/2)*size+size/2)*4;
            const bool bgra=gpu->format()==WGPUTextureFormat_BGRA8Unorm;return std::array<int,3>{p[i+(bgra?2:0)],p[i+1],p[i+(bgra?0:2)]};};
        const auto l=look(20,2);(void)l;const auto cloudy=pixel();v.enabled=false;frame();const auto clear=pixel();v.enabled=true;
        auto red=MeshBasicMaterial::create();red->color=Color(1,0,0);auto box=Mesh::create(BoxGeometry::create(static_cast<float>(3*km),static_cast<float>(3*km),static_cast<float>(3*km)),red);
        box->position.set(0,static_cast<float>(1+15*km),0);scene.add(box);frame();const auto hidden=pixel();scene.remove(*box);
        std::printf(" composite: cloud %d %d %d, clear %d %d %d, box in front %d %d %d\n",cloudy[0],cloudy[1],cloudy[2],clear[0],clear[1],clear[2],hidden[0],hidden[1],hidden[2]);
        check(cloudy[0]>clear[0]+60&&std::abs(cloudy[0]-cloudy[2])<12,"The slab shows over the ground, white");
        check(hidden[0]>200&&hidden[1]<30&&hidden[2]<30,"A box nearer the eye hides it");
    }
    std::printf(" cloud fraction seen from 100 km, noise on (40 degrees: six of its 12 km tiles, near nadir as a satellite's):\n");
    {
        camera.fov=40;camera.updateProjectionMatrix();camera.position.set(0,static_cast<float>(1+100*km),0);camera.lookAt(Vector3(0,0,0));v.flat=false;v.baseKm=1.5f;v.topKm=4.5f;v.extinction=75;v.steps=128;
        for(const double cover:{.2,.4,.6,.8}){
            auto map=DataTexture::create(ImageData(std::vector<unsigned char>(8*4*4,static_cast<unsigned char>(std::lround(255*cover)))),8,4);map->needsUpdate();renderer.air.clouds=map.get();frame();
            const auto c=read(*gpu,renderer.clouds()->colour(),size,size,8);int cloudy=0;
            for(std::size_t i=0;i<static_cast<std::size_t>(size)*size;++i){std::uint16_t a;std::memcpy(&a,c.data()+i*8+6,2);cloudy+=half(a)<.5f;}
            near(static_cast<double>(cloudy)/(size*size),cover,.06,"cover "+std::to_string(cover).substr(0,3)+": pixels behind cloud (transmittance < 0.5)");
        }
        renderer.air.clouds=overcast.get();camera.fov=30;camera.updateProjectionMatrix();camera.position.set(0,static_cast<float>(1+20*km),0);camera.lookAt(Vector3(0,0,0));
    }
    if(argc>1){ // Views to look at: broken cumulus (cover 0.55) with noise, the air at full strength, over a sky shell.
        albert::gpu::Program sky;sky.background=true;sky.attributes={{"normal",3}};
        sky.wgsl=R"(
struct V { @builtin(position) clip: vec4f, @location(0) world: vec3f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f) -> V { var o: V; let w = object.model * vec4f(position, 1.0); o.world = w.xyz; o.clip = frame.projection * frame.view * w; return o; }
@fragment fn fs_main(v: V) -> @location(0) vec4f { if (frame.air.w <= 0.0) { discard; } let s = skyRadiance(frame.air.xyz, normalize(airPoint(v.world) - frame.air.xyz), frame.airSun.xyz); return vec4f(finish(s.radiance * frame.sunColor.rgb), 1.0); }
)";
        albert::gpu::define("test:sky",sky);auto shell=ShaderMaterial::create();shell->name="test:sky";globe->add(Mesh::create(SphereGeometry::create(1.0126f,64,32),shell));
        constexpr unsigned w=640,h=360;td.size={w,h,1};WGPUTexture image=wgpuDeviceCreateTexture(gpu->device(),&td);WGPUTextureView imageView=wgpuTextureCreateView(image,nullptr);
        auto broken=DataTexture::create(ImageData(std::vector<unsigned char>(8*4*4,140)),8,4);broken->needsUpdate();
        renderer.air={&air,globe.get(),6378.137f,1};renderer.air.clouds=broken.get();renderer.air.cloudShadow=.88f;renderer.toneMapping=true;renderer.samples=4;renderer.exposure=1.05f;
        v.flat=false;v.divisor=2;v.steps=64;v.baseKm=1.5f;v.topKm=5.5f;v.extinction=75;sunlight->intensity=2.4f;ground->color=Color(.12f,.16f,.09f);
        PerspectiveCamera eye(60,static_cast<float>(w)/h,1e-7f,10);
        auto draw=[&](double heightKm,double pitchDegrees,double sunElevation,double sunAzimuth){ // Azimuth from the view's direction (+z), toward +x.
            const double e=sunElevation*3.14159265358979/180,a=sunAzimuth*3.14159265358979/180,p=pitchDegrees*3.14159265358979/180;
            sunlight->position.set(static_cast<float>(std::cos(e)*std::sin(a)),static_cast<float>(std::sin(e)),static_cast<float>(std::cos(e)*std::cos(a)));
            const float y=static_cast<float>(1+heightKm*km);eye.position.set(0,y,0);eye.up.set(0,1,0);eye.lookAt(Vector3(0,y+static_cast<float>(std::tan(p)*.01),.01f));
            WGPUCommandEncoder en=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(scene,eye,imageView,w,h,en);
            WGPUCommandBuffer cb=wgpuCommandEncoderFinish(en,nullptr);wgpuQueueSubmit(gpu->queue(),1,&cb);renderer.submitted();wgpuCommandBufferRelease(cb);wgpuCommandEncoderRelease(en);};
        auto shoot=[&](const char* name,double heightKm,double pitchDegrees,double sunElevation,double sunAzimuth){draw(heightKm,pitchDegrees,sunElevation,sunAzimuth);
            const auto px=read(*gpu,image,w,h,4);const bool bgra=gpu->format()==WGPUTextureFormat_BGRA8Unorm;std::ofstream out(std::string(argv[1])+"-"+name+".ppm",std::ios::binary);
            out<<"P6\n"<<w<<" "<<h<<"\n255\n";for(std::size_t i=0;i<static_cast<std::size_t>(w)*h;++i){const char rgb[3]{static_cast<char>(px[i*4+(bgra?2:0)]),static_cast<char>(px[i*4+1]),static_cast<char>(px[i*4+(bgra?0:2)])};out.write(rgb,3);}};
        { // GPU time of a frame of the scene with and without the clouds (the view from below; the march at half resolution, 64 steps).
            auto timed=[&](bool on){v.enabled=on;shoot("timing",.3,12,40,-120);const auto t0=std::chrono::steady_clock::now();for(int i=0;i<30;++i)draw(.3,12,40,-120);
                wgpuDevicePoll(gpu->device(),true,nullptr);return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count()/30;};
            const double off=timed(false),on=timed(true);std::printf(" frame at 640 x 360: %.2f ms without clouds, %.2f ms with (%s)\n",off,on,gpu->adapter().c_str());std::remove((std::string(argv[1])+"-timing.ppm").c_str());
        }
        shoot("below",.3,12,40,-120);shoot("sunward",.3,6,12,10);shoot("above",9,-18,35,-120);shoot("inside",3,0,35,-120);
        wgpuTextureViewRelease(imageView);wgpuTextureRelease(image);
    }
    if(albert::gpu::errors()){std::printf("FAIL WebGPU errors: %u\n",albert::gpu::errors());++failures;}
    wgpuTextureViewRelease(view);wgpuTextureRelease(target);
    if(failures)throw std::runtime_error(std::to_string(failures)+" checks failed");
    std::printf("clouds_test: ok\n");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
