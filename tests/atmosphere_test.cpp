// Bruneton tables precomputed with WebGPU compute, read back and checked against closed forms:
// the zenith transmittance from the ground (exponential profiles and the ozone tent) and the single
// scattering of a vertical ray with the sun at the zenith (T(0,top) times the optical depth).
// Exits 77 (skipped) without a WebGPU adapter.
#include "gpu/atmosphere.hpp"
#include <webgpu/wgpu.h>
#include <chrono>
#include <cmath>
#include <cstdio>
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
        if(albert::gpu::errors()){std::printf("FAIL WebGPU errors: %u\n",albert::gpu::errors());++failures;}
    }catch(const std::exception& e){std::printf("FAIL %s\n",e.what());return 1;}
    std::printf(failures?"%d failure(s)\n":"atmosphere ok\n",failures);return failures?1:0;
}
