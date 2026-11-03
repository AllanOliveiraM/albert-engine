#include "gpu/atmosphere.hpp"
#include "gpu/programs.hpp"
#include <webgpu/wgpu.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr float radius=6378.137f,scale=.8f,extinction=.04f;
struct Ray {std::array<float,3> at,direction;float distance;};
// Independent, double-precision Simpson integration of the spherical density.
// It does not share the production clipping or quadrature approximation.
double reference(const Ray& ray){
    constexpr int steps=40000;const double dx=ray.distance/steps;double sum=0;
    for(int i=0;i<=steps;++i){const double t=i*dx;
        const double x=ray.at[0]+ray.direction[0]*t,y=ray.at[1]+ray.direction[1]*t,z=ray.at[2]+ray.direction[2]*t;
        const double h=std::max(0.,std::sqrt(x*x+y*y+z*z)-double(radius));
        sum+=(i==0||i==steps?1:i%2?4:2)*std::exp(-h/scale);
    }
    return sum*dx/3;
}
double sunlightColumn(const Ray& ray){
    const auto& p=ray.at;const double r=std::sqrt(double(p[0])*p[0]+double(p[1])*p[1]+double(p[2])*p[2]);
    const double rc=std::clamp(r,double(radius),double(radius+80.f));
    const double mu=std::clamp(std::max((double(p[0])*ray.direction[0]+double(p[1])*ray.direction[1]+double(p[2])*ray.direction[2])/r,
        -std::sqrt(std::max(0.,1-double(radius)*radius/(rc*rc)))), -1.,1.);
    const double top=radius+80.f,d=-rc*mu+std::sqrt(rc*rc*mu*mu+(top-rc)*(top+rc));
    return reference({{{0,0,float(rc)}},{{float(std::sqrt(1-mu*mu)),0,float(mu)}},float(d)});
}
WGPUStringView sv(const std::string& s){return{s.data(),s.size()};}
}
int main(){try{
    std::unique_ptr<albert::gpu::Context> context;
    try{context=std::make_unique<albert::gpu::Context>();}catch(const std::exception& e){std::printf("Coastal optics skipped: %s\n",e.what());return 77;}
    auto& gpu=*context;std::printf("adapter: %s\n",gpu.adapter().c_str());
    const float low=radius+.01f,high=radius+80.f;
    const std::array<Ray,13> rays{{
        {{{0,0,low}},{{0,0,1}},high-low},
        {{{0,0,high}},{{0,0,-1}},high-low},
        {{{0,0,low}},{{1,0,0}},150},
        {{{150,0,low}},{{-1,0,0}},150},
        {{{-500,0,radius+.4f}},{{1,0,0}},1000},
        {{{500,0,radius+.4f}},{{-1,0,0}},1000},
        {{{0,0,radius+40.f}},{{0,0,1}},100},
        {{{0,0,low}},{{0,0,1}},0},
        {{{low*.6f,low*.8f,0}},{{.8f,-.6f,0}},150},
        {{{low*.6f+120,low*.8f-90,0}},{{-.8f,.6f,0}},150},
        // A partially visible solar limb must retain a grazing marine column,
        // even when its centre is below the geometric surface horizon.
        {{{0,0,radius}},{{1,0,0}},0},
        {{{0,0,radius}},{{std::cos(.001f),0,-std::sin(.001f)}},0},
        {{{0,0,radius}},{{std::cos(.001f),0,std::sin(.001f)}},0}
    }};
    std::string code=std::string(albert::gpu::prelude)+albert::gpu::atmosphereWgsl+albert::gpu::air+R"(
@group(0) @binding(16) var<storage, read_write> output: array<vec4f>;
@group(0) @binding(17) var<storage, read> inputs: array<vec4f>;
@compute @workgroup_size(1) fn main(@builtin(workgroup_id) id: vec3u) {
    let c=inputs[2u*id.x];let ray=inputs[2u*id.x+1u];
    output[id.x]=vec4f(coastalColumn(c.xyz,ray.xyz,c.w),coastalSun(c.xyz,ray.xyz));
}
)";
    std::array<float,232> frame{};frame[156]=extinction;frame[157]=scale;
    std::array<float,64> parameters{};parameters[15]=radius;parameters[19]=radius+80.f;
    std::vector<float> inputs;for(const auto& r:rays){for(float c:r.at)inputs.push_back(c);inputs.push_back(r.distance);for(float d:r.direction)inputs.push_back(d);inputs.push_back(0);}
    auto buffer=[&](const void* data,std::size_t bytes,WGPUBufferUsage usage){
        WGPUBufferDescriptor d{};d.size=bytes;d.usage=usage|(data?WGPUBufferUsage_CopyDst:0);
        auto b=wgpuDeviceCreateBuffer(gpu.device(),&d);if(data)wgpuQueueWriteBuffer(gpu.queue(),b,0,data,bytes);return b;
    };
    auto f=buffer(frame.data(),sizeof frame,WGPUBufferUsage_Uniform);
    auto a=buffer(parameters.data(),sizeof parameters,WGPUBufferUsage_Uniform);
    auto in=buffer(inputs.data(),inputs.size()*sizeof(float),WGPUBufferUsage_Storage);
    constexpr auto bytes=sizeof(float)*4*rays.size();
    auto out=buffer(nullptr,bytes,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc);
    auto back=buffer(nullptr,bytes,WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst);
    WGPUShaderSourceWGSL source{};source.chain.sType=WGPUSType_ShaderSourceWGSL;source.code=sv(code);
    WGPUShaderModuleDescriptor md{};md.nextInChain=&source.chain;auto module=wgpuDeviceCreateShaderModule(gpu.device(),&md);
    WGPUComputePipelineDescriptor pd{};pd.compute.module=module;pd.compute.entryPoint={"main",4};auto pipe=wgpuDeviceCreateComputePipeline(gpu.device(),&pd);
    auto layout=wgpuComputePipelineGetBindGroupLayout(pipe,0);
    WGPUBindGroupEntry entries[4]{};
    for(unsigned i=0;i<4;++i){entries[i].binding=std::array<unsigned,4>{0,3,16,17}[i];entries[i].buffer=std::array<WGPUBuffer,4>{f,a,out,in}[i];entries[i].size=std::array<std::size_t,4>{sizeof frame,sizeof parameters,bytes,inputs.size()*sizeof(float)}[i];}
    WGPUBindGroupDescriptor gd{};gd.layout=layout;gd.entryCount=4;gd.entries=entries;auto group=wgpuDeviceCreateBindGroup(gpu.device(),&gd);
    auto encoder=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);auto pass=wgpuCommandEncoderBeginComputePass(encoder,nullptr);
    wgpuComputePassEncoderSetPipeline(pass,pipe);wgpuComputePassEncoderSetBindGroup(pass,0,group,0,nullptr);wgpuComputePassEncoderDispatchWorkgroups(pass,rays.size(),1,1);wgpuComputePassEncoderEnd(pass);
    wgpuCommandEncoderCopyBufferToBuffer(encoder,out,0,back,0,bytes);auto commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(gpu.queue(),1,&commands);
    bool ready=false;WGPUMapAsyncStatus status{};WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&ready;cb.userdata2=&status;
    cb.callback=[](WGPUMapAsyncStatus s,WGPUStringView,void* done,void* result){*static_cast<WGPUMapAsyncStatus*>(result)=s;*static_cast<bool*>(done)=true;};
    wgpuBufferMapAsync(back,WGPUMapMode_Read,0,bytes,cb);while(!ready){wgpuDevicePoll(gpu.device(),true,nullptr);wgpuInstanceProcessEvents(gpu.instance());}
    if(status!=WGPUMapAsyncStatus_Success)throw std::runtime_error("Coastal optics readback failed");
    const auto* values=static_cast<const float*>(wgpuBufferGetConstMappedRange(back,0,bytes));int failures=0;
    for(unsigned i=0;i<rays.size();++i){const double expected=reference(rays[i]),got=values[4*i];const bool ok=std::isfinite(got)&&std::abs(got-expected)<.003+expected*.002;
        std::printf("%s column %u: %.6f km (reference %.6f)\n",ok?"ok  ":"FAIL",i,got,expected);failures+=!ok;
        const double sunColumn=sunlightColumn(rays[i]);
        for(unsigned channel=0;channel<3;++channel){const double transmission=values[4*i+1+channel],ref=std::exp(-std::array<double,3>{.88,1,1.13}[channel]*extinction*sunColumn);
            const bool sunOk=std::isfinite(transmission)&&std::abs(transmission-ref)<.002+ref*.002;
            std::printf("%s sunlight %u/%u: %.6f (reference %.6f)\n",sunOk?"ok  ":"FAIL",i,channel,transmission,ref);failures+=!sunOk;}
    }
    for(unsigned i:{0u,2u,4u,8u}){const bool ok=std::abs(values[4*i]-values[4*(i+1)])<.003+values[4*i]*.001;
        std::printf("%s reversed path %u/%u\n",ok?"ok  ":"FAIL",i,i+1);failures+=!ok;}
    wgpuBufferUnmap(back);wgpuBindGroupRelease(group);wgpuBindGroupLayoutRelease(layout);wgpuComputePipelineRelease(pipe);wgpuShaderModuleRelease(module);
    wgpuCommandBufferRelease(commands);wgpuComputePassEncoderRelease(pass);wgpuCommandEncoderRelease(encoder);
    for(auto b:{f,a,in,out,back})wgpuBufferRelease(b);
    return failures||albert::gpu::errors()?1:0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
