// The ocean's waves on the GPU (gpu::Ocean) against the Pierson-Moskowitz sea: the significant wave height of the field the
// cascades produce, read back, for a fully developed sea at 10 and 15 m/s, and its resolved mean square slope against the
// spectrum's integral over the same band. Exits 77 (skipped) without a WebGPU adapter.
#include "gpu/context.hpp"
#include "gpu/ocean.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
int failures=0;
void near(double got,double want,double tolerance,const std::string& what){const bool ok=std::abs(got-want)<=tolerance;
    std::printf("  %-50s %10.4f  reference %10.4f%s\n",what.c_str(),got,want,ok?"":"  <-- outside");if(!ok)++failures;}
float half(std::uint16_t h){const std::uint32_t s=(h&0x8000u)<<16,e=(h>>10)&0x1f,m=h&0x3ff;std::uint32_t f;
    if(e==0)f=m?(s|((113-0)<<23)|(m<<13)):s;else if(e==31)f=s|0x7f800000u|(m<<13);else f=s|((e+112)<<23)|(m<<13);
    if(e==0&&m){float v=std::ldexp(static_cast<float>(m),-24);return s?-v:v;}float out;std::memcpy(&out,&f,4);return out;}
std::vector<float> read(albert::gpu::Context& gpu,WGPUTexture texture){ // RGBA16F, 256 x 256 x 3, as floats.
    const unsigned n=albert::gpu::Ocean::size,layers=albert::gpu::Ocean::cascades,row=n*8;WGPUBufferDescriptor bd{};bd.size=static_cast<std::uint64_t>(row)*n*layers;bd.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;
    WGPUBuffer b=wgpuDeviceCreateBuffer(gpu.device(),&bd);WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);
    for(unsigned layer=0;layer<layers;++layer){ // One layer a copy: WARP (Direct3D 12) loses the device copying the array whole.
        WGPUTexelCopyTextureInfo src{};src.texture=texture;src.origin.z=layer;WGPUTexelCopyBufferInfo dst{};dst.buffer=b;dst.layout.offset=static_cast<std::uint64_t>(row)*n*layer;
        dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=n;const WGPUExtent3D size{n,n,1};wgpuCommandEncoderCopyTextureToBuffer(e,&src,&dst,&size);}WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu.queue(),1,&c);
    bool done=false;WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&done;cb.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
    wgpuBufferMapAsync(b,WGPUMapMode_Read,0,bd.size,cb);while(!done)wgpuInstanceProcessEvents(gpu.instance());
    const auto* p=static_cast<const std::uint16_t*>(wgpuBufferGetConstMappedRange(b,0,bd.size));std::vector<float> out(bd.size/2);for(std::size_t i=0;i<out.size();++i)out[i]=half(p[i]);
    wgpuBufferUnmap(b);wgpuBufferRelease(b);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);return out;
}
double variance(const std::vector<float>& v,int layer,int channel){
    const std::size_t n=albert::gpu::Ocean::size*albert::gpu::Ocean::size;double s=0,s2=0;
    for(std::size_t i=0;i<n;++i){const double x=v[(static_cast<std::size_t>(layer)*n+i)*4+static_cast<std::size_t>(channel)];s+=x;s2+=x*x;}
    return s2/static_cast<double>(n)-(s/static_cast<double>(n))*(s/static_cast<double>(n));
}
}
int main(){try{
    std::setvbuf(stdout,nullptr,_IONBF,0); // Each line out before a driver's abort (CI keeps the output's tail).
    std::unique_ptr<albert::gpu::Context> gpu;try{gpu=std::make_unique<albert::gpu::Context>();}catch(const std::exception& e){std::printf("skipped: %s\n",e.what());return 77;}
    // Windows' software adapter (WARP) loses the device on the cascades' texture array (the passes complete; CI traces):
    // the app runs no waves on CPU adapters, so this skips there; llvmpipe and real GPUs run it.
    if(gpu->adapter().find("Microsoft Basic Render Driver")!=std::string::npos){std::printf("skipped on %s (no waves on CPU adapters)\n",gpu->adapter().c_str());return 77;}
    albert::gpu::Ocean ocean(*gpu);std::printf("Ocean on %s:\n",gpu->adapter().c_str());
    for(double wind:{10.,15.}){
        WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);ocean.update(e,12.3,static_cast<float>(wind),0);
        WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu->queue(),1,&c);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);
        const auto shape=read(*gpu,ocean.displacementTexture());const auto slopes=read(*gpu,ocean.derivativesTexture());double eta=0,slope=0;
        for(int l=0;l<3;++l){eta+=variance(shape,l,1);slope+=variance(slopes,l,0)+variance(slopes,l,1);}
        std::printf(" %.0f m/s:\n",wind);
        near(4*std::sqrt(eta),albert::gpu::Ocean::significantHeight(wind),albert::gpu::Ocean::significantHeight(wind)*.08,"significant wave height (m), Pierson-Moskowitz");
        near(slope,albert::gpu::Ocean::resolvedSlope(wind),albert::gpu::Ocean::resolvedSlope(wind)*.15,"resolved mean square slope, the spectrum's");
        near(slope,0,.003+.00512*wind,"  within Cox-Munk's total (0.003 + 0.00512 U)");
    }
    if(failures)throw std::runtime_error(std::to_string(failures)+" values outside");
    std::printf("ocean_test: ok\n");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
