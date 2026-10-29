#include "fisheye.hpp"
#include <string_view>
namespace albert::gpu {
namespace {
WGPUStringView sv(std::string_view s){return {s.data(),s.size()};}
constexpr std::string_view shader=R"(
@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var linear: sampler;
struct Vertex { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32> };
@vertex fn vs(@builtin(vertex_index) i:u32)->Vertex {
    let p=array<vec2<f32>,3>(vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    var out:Vertex;out.position=vec4(p[i],0,1);out.uv=p[i]*vec2(0.5,-0.5)+vec2(0.5);return out;
}
@fragment fn fs(in:Vertex)->@location(0) vec4<f32> {
    let size=vec2<f32>(textureDimensions(image));let aspect=size.x/size.y;
    let p=(in.uv*2-1)*vec2(aspect,1);let r=length(p);
    let theta=r/sqrt(1+aspect*aspect)*1.30899694;
    // r=f*theta at the output; tan(theta) at the perspective source. The centre has a finite limit.
    let gain=select(tan(theta)/max(r,0.000001),1.30899694/sqrt(1+aspect*aspect),r<0.000001)/3.1715948;
    let uv=vec2(0.5)+p*gain/vec2(aspect,1)*0.5;
    return textureSample(image,linear,uv);
}
)";
constexpr std::string_view plain=R"(
@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var linear: sampler;
struct Vertex { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32> };
@vertex fn vs(@builtin(vertex_index) i:u32)->Vertex {
    let p=array<vec2<f32>,3>(vec2(-1,-1),vec2(3,-1),vec2(-1,3));
    var out:Vertex;out.position=vec4(p[i],0,1);out.uv=p[i]*vec2(0.5,-0.5)+vec2(0.5);return out;
}
@fragment fn fs(in:Vertex)->@location(0) vec4<f32> { return textureSample(image,linear,in.uv); }
)";
}
Fisheye::Fisheye(Context& context,bool lens):context_(context){
    WGPUShaderSourceWGSL code{};code.chain.sType=WGPUSType_ShaderSourceWGSL;code.code=sv(lens?shader:plain);
    WGPUShaderModuleDescriptor sm{};sm.nextInChain=&code.chain;auto module=wgpuDeviceCreateShaderModule(context.device(),&sm);
    WGPUBindGroupLayoutEntry entries[2]{};entries[0].binding=0;entries[0].visibility=WGPUShaderStage_Fragment;entries[0].texture.sampleType=WGPUTextureSampleType_Float;entries[0].texture.viewDimension=WGPUTextureViewDimension_2D;
    entries[1].binding=1;entries[1].visibility=WGPUShaderStage_Fragment;entries[1].sampler.type=WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=2;ld.entries=entries;layout_=wgpuDeviceCreateBindGroupLayout(context.device(),&ld);
    WGPUPipelineLayoutDescriptor pl{};pl.bindGroupLayoutCount=1;pl.bindGroupLayouts=&layout_;auto pipelineLayout=wgpuDeviceCreatePipelineLayout(context.device(),&pl);
    WGPUColorTargetState colour{};colour.format=context.format();colour.writeMask=WGPUColorWriteMask_All;
    WGPUFragmentState fragment{};fragment.module=module;fragment.entryPoint=sv("fs");fragment.targetCount=1;fragment.targets=&colour;
    WGPURenderPipelineDescriptor pd{};pd.layout=pipelineLayout;pd.vertex.module=module;pd.vertex.entryPoint=sv("vs");pd.fragment=&fragment;pd.primitive.topology=WGPUPrimitiveTopology_TriangleList;pd.multisample.count=1;pd.multisample.mask=~0u;
    pipeline_=wgpuDeviceCreateRenderPipeline(context.device(),&pd);wgpuPipelineLayoutRelease(pipelineLayout);wgpuShaderModuleRelease(module);
    WGPUSamplerDescriptor sampler{};sampler.minFilter=sampler.magFilter=WGPUFilterMode_Linear;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=WGPUAddressMode_ClampToEdge;sampler.maxAnisotropy=1;sampler.lodMaxClamp=32;
    sampler_=wgpuDeviceCreateSampler(context.device(),&sampler);
}
Fisheye::~Fisheye(){
    if(group_)wgpuBindGroupRelease(group_);if(view_)wgpuTextureViewRelease(view_);if(texture_)wgpuTextureRelease(texture_);
    wgpuSamplerRelease(sampler_);wgpuBindGroupLayoutRelease(layout_);wgpuRenderPipelineRelease(pipeline_);
}
WGPUTextureView Fisheye::source(unsigned width,unsigned height){
    if(view_&&width==width_&&height==height_)return view_;
    if(group_)wgpuBindGroupRelease(group_);if(view_)wgpuTextureViewRelease(view_);if(texture_)wgpuTextureRelease(texture_);
    width_=width;height_=height;
    WGPUTextureDescriptor t{};t.size={width,height,1};t.format=context_.format();t.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding;t.mipLevelCount=1;t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;
    texture_=wgpuDeviceCreateTexture(context_.device(),&t);view_=wgpuTextureCreateView(texture_,nullptr);
    WGPUBindGroupEntry entries[2]{};entries[0].binding=0;entries[0].textureView=view_;entries[1].binding=1;entries[1].sampler=sampler_;
    WGPUBindGroupDescriptor d{};d.layout=layout_;d.entryCount=2;d.entries=entries;group_=wgpuDeviceCreateBindGroup(context_.device(),&d);return view_;
}
void Fisheye::draw(WGPUCommandEncoder encoder,WGPUTextureView target){
    WGPURenderPassColorAttachment colour{};colour.view=target;colour.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;colour.loadOp=WGPULoadOp_Clear;colour.storeOp=WGPUStoreOp_Store;colour.clearValue={0,0,0,1};
    WGPURenderPassDescriptor d{};d.colorAttachmentCount=1;d.colorAttachments=&colour;
    auto pass=wgpuCommandEncoderBeginRenderPass(encoder,&d);wgpuRenderPassEncoderSetPipeline(pass,pipeline_);wgpuRenderPassEncoderSetBindGroup(pass,0,group_,0,nullptr);wgpuRenderPassEncoderDraw(pass,3,1,0,0);
    wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
}
}
