#pragma once
#include "gpu/context.hpp"
#include "gpu/pass_timing.hpp"
#include <webgpu/wgpu.h>
#include <string>
namespace albert::gpu {
// A reusable scratch layer. Flush at transparent sort boundaries; colours are premultiplied,
// in the same display space as the scene. RGBA16F retains emission above one until composite.
class LocalMedia {
    Context& gpu_;WGPUTexture colour_=nullptr,depth_=nullptr,sceneDepth_=nullptr;
    WGPUTextureView colourView_=nullptr,depthView_=nullptr,sceneDepthView_=nullptr;
    WGPUBindGroupLayout copyLayout_=nullptr,downLayout_=nullptr,upLayout_=nullptr;
    WGPUPipelineLayout copyPipelineLayout_=nullptr,downPipelineLayout_=nullptr,upPipelineLayout_=nullptr;
    WGPURenderPipeline copy_=nullptr,down_=nullptr,up_=nullptr;WGPUShaderModule module_=nullptr;
    WGPUBindGroup copyGroup_=nullptr,downGroup_=nullptr,upGroup_=nullptr;WGPUTextureView boundDepth_=nullptr;
    unsigned width_=0,height_=0,divisor_=0,samples_=0;
    static WGPUStringView sv(const std::string& s){return{s.data(),s.size()};}
    void releaseTargets(){
        for(auto g:{copyGroup_,downGroup_,upGroup_})if(g)wgpuBindGroupRelease(g);
        for(auto v:{colourView_,depthView_,sceneDepthView_})if(v)wgpuTextureViewRelease(v);
        for(auto t:{colour_,depth_,sceneDepth_})if(t)wgpuTextureRelease(t);
        boundDepth_=nullptr;copyGroup_=downGroup_=upGroup_=nullptr;colourView_=depthView_=sceneDepthView_=nullptr;colour_=depth_=sceneDepth_=nullptr;
    }
    void releasePipelines(){
        for(auto p:{copy_,down_,up_})if(p)wgpuRenderPipelineRelease(p);
        for(auto p:{copyPipelineLayout_,downPipelineLayout_,upPipelineLayout_})if(p)wgpuPipelineLayoutRelease(p);
        for(auto l:{copyLayout_,downLayout_,upLayout_})if(l)wgpuBindGroupLayoutRelease(l);
        if(module_)wgpuShaderModuleRelease(module_);
        copy_=down_=up_=nullptr;copyPipelineLayout_=downPipelineLayout_=upPipelineLayout_=nullptr;copyLayout_=downLayout_=upLayout_=nullptr;module_=nullptr;
    }
    void pipelines(unsigned samples){
        const std::string source=std::string(R"(
@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {
    let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[i],0,1);
}
@group(0) @binding(0) var original: )")+(samples>1?"texture_depth_multisampled_2d;":"texture_depth_2d;")+R"(
@fragment fn snapshot(@builtin(position) p:vec4f)->@location(0) f32 {
    var z=0.0;
)"+(samples>1?"for(var i=0;i<4;i++){z=max(z,textureLoad(original,vec2i(p.xy),i));}":"z=textureLoad(original,vec2i(p.xy),0);")+R"(
    return z;
}
@group(0) @binding(0) var lowColour:texture_2d<f32>;
@group(0) @binding(1) var lowDepth:texture_depth_2d;
@group(0) @binding(2) var referenceDepth:texture_2d<f32>;
@fragment fn composite(@builtin(position) p:vec4f)->@location(0) vec4f {
    let size=vec2i(textureDimensions(lowColour));let fullSize=vec2f(textureDimensions(referenceDepth));
    let coord=p.xy/fullSize*vec2f(size)-0.5;let base=vec2i(floor(coord));let f=fract(coord);
    let z=textureLoad(referenceDepth,vec2i(p.xy),0).r;
    var colour=vec4f(0);var weight=0.0;var closest=vec4f(0);var best=1e30;
    for(var y=0;y<2;y++){for(var x=0;x<2;x++){
        let q=clamp(base+vec2i(x,y),vec2i(0),size-1);let d=textureLoad(lowDepth,q,0);
        // Relative reversed-Z difference also works at Earth-radius scene scales.
        let difference=abs(d-z)/max(max(d,z),1e-12);
        let bilinear=select(1.0-f.x,f.x,x==1)*select(1.0-f.y,f.y,y==1);
        let w=bilinear*max(0.0,1.0-difference*40.0);let c=textureLoad(lowColour,q,0);
        colour+=c*w;weight+=w;if(difference<best){best=difference;closest=c;}
    }}
    return select(closest,colour/max(weight,1e-8),weight>1e-6);
}
)";
        // Entry points use disjoint bindings; reduction has its own small module below.
        WGPUShaderSourceWGSL wg{};wg.chain.sType=WGPUSType_ShaderSourceWGSL;wg.code=sv(source);WGPUShaderModuleDescriptor md{};md.nextInChain=&wg.chain;module_=wgpuDeviceCreateShaderModule(gpu_.device(),&md);
        auto layout=[&](bool copy,bool up){WGPUBindGroupLayoutEntry e[3]{};const unsigned n=up?3:1;
            for(unsigned i=0;i<n;++i){e[i].binding=i;e[i].visibility=WGPUShaderStage_Fragment;e[i].texture.viewDimension=WGPUTextureViewDimension_2D;e[i].texture.sampleType=(copy||i==1)?WGPUTextureSampleType_Depth:WGPUTextureSampleType_UnfilterableFloat;e[i].texture.multisampled=copy&&samples>1;}
            WGPUBindGroupLayoutDescriptor d{};d.entryCount=n;d.entries=e;return wgpuDeviceCreateBindGroupLayout(gpu_.device(),&d);};
        copyLayout_=layout(true,false);downLayout_=layout(false,false);upLayout_=layout(false,true);
        auto pl=[&](WGPUBindGroupLayout l){WGPUPipelineLayoutDescriptor d{};d.bindGroupLayoutCount=1;d.bindGroupLayouts=&l;return wgpuDeviceCreatePipelineLayout(gpu_.device(),&d);};
        copyPipelineLayout_=pl(copyLayout_);downPipelineLayout_=pl(downLayout_);upPipelineLayout_=pl(upLayout_);
        copy_=pipeline(module_,"snapshot",copyPipelineLayout_,WGPUTextureFormat_R32Float,1,false,false);
        up_=pipeline(module_,"composite",upPipelineLayout_,gpu_.format(),samples,true,true);
    }
    WGPURenderPipeline pipeline(WGPUShaderModule module,const std::string& entry,WGPUPipelineLayout layout,WGPUTextureFormat format,unsigned samples,bool blend,bool depth){
        WGPUBlendState b{};b.color={WGPUBlendOperation_Add,WGPUBlendFactor_One,WGPUBlendFactor_OneMinusSrcAlpha};b.alpha=b.color;
        WGPUColorTargetState c{};c.format=format;c.writeMask=WGPUColorWriteMask_All;c.blend=blend?&b:nullptr;
        WGPUFragmentState f{};f.module=module;f.entryPoint=sv(entry);f.targetCount=format==WGPUTextureFormat_Undefined?0:1;f.targets=&c;
        WGPUDepthStencilState ds{};ds.format=WGPUTextureFormat_Depth32Float;ds.depthWriteEnabled=blend?WGPUOptionalBool_False:WGPUOptionalBool_True;ds.depthCompare=WGPUCompareFunction_Always;
        WGPURenderPipelineDescriptor d{};d.label=sv(entry);d.layout=layout;d.vertex.module=module;d.vertex.entryPoint={"vs",2};d.primitive.topology=WGPUPrimitiveTopology_TriangleList;d.multisample.count=samples;d.multisample.mask=~0u;d.fragment=&f;d.depthStencil=depth?&ds:nullptr;
        return wgpuDeviceCreateRenderPipeline(gpu_.device(),&d);
    }
public:
    explicit LocalMedia(Context& gpu):gpu_(gpu){}
    ~LocalMedia(){releaseTargets();releasePipelines();}
    void ensure(unsigned w,unsigned h,unsigned divisor,unsigned samples,WGPUTextureView sceneDepth){
        if(width_==w&&height_==h&&divisor_==divisor&&samples_==samples)return;
        releaseTargets();if(samples_!=samples||!module_){releasePipelines();pipelines(samples);}
        width_=w;height_=h;divisor_=divisor;samples_=samples;
        auto texture=[&](unsigned x,unsigned y,WGPUTextureFormat format,WGPUTexture& t,WGPUTextureView& v){WGPUTextureDescriptor d{};d.size={x,y,1};d.dimension=WGPUTextureDimension_2D;d.format=format;d.mipLevelCount=1;d.sampleCount=1;d.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding;t=wgpuDeviceCreateTexture(gpu_.device(),&d);v=wgpuTextureCreateView(t,nullptr);};
        texture(w,h,WGPUTextureFormat_R32Float,sceneDepth_,sceneDepthView_);texture((w+divisor-1)/divisor,(h+divisor-1)/divisor,WGPUTextureFormat_RGBA16Float,colour_,colourView_);texture((w+divisor-1)/divisor,(h+divisor-1)/divisor,WGPUTextureFormat_Depth32Float,depth_,depthView_);
        auto group=[&](WGPUBindGroupLayout l,std::initializer_list<WGPUTextureView> views){WGPUBindGroupEntry e[3]{};unsigned i=0;for(auto v:views){e[i].binding=i;e[i].textureView=v;++i;}WGPUBindGroupDescriptor d{};d.layout=l;d.entryCount=i;d.entries=e;return wgpuDeviceCreateBindGroup(gpu_.device(),&d);};
        copyGroup_=group(copyLayout_,{sceneDepth});boundDepth_=sceneDepth;downGroup_=group(downLayout_,{sceneDepthView_});upGroup_=group(upLayout_,{colourView_,depthView_,sceneDepthView_});
        if(down_)wgpuRenderPipelineRelease(down_);
        std::string code=R"(@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f {let p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));return vec4f(p[i],0,1);}
@group(0) @binding(0) var fullDepth:texture_2d<f32>;
@fragment fn reduce(@builtin(position) p:vec4f)->@builtin(frag_depth) f32 {return textureLoad(fullDepth,min(vec2i(p.xy*vec2f()"+std::to_string(float(w)/((w+divisor-1)/divisor))+","+std::to_string(float(h)/((h+divisor-1)/divisor))+R"()),vec2i(textureDimensions(fullDepth))-1),0).r;})";
        WGPUShaderSourceWGSL wg{};wg.chain.sType=WGPUSType_ShaderSourceWGSL;wg.code=sv(code);WGPUShaderModuleDescriptor md{};md.nextInChain=&wg.chain;auto module=wgpuDeviceCreateShaderModule(gpu_.device(),&md);
        down_=pipeline(module,"reduce",downPipelineLayout_,WGPUTextureFormat_Undefined,1,false,true);wgpuShaderModuleRelease(module);
    }
    WGPUTextureView colour()const{return colourView_;}WGPUTextureView depth()const{return depthView_;}
    // Snapshot the opaque depth once. MSAA chooses nearest covered sample (reversed Z).
    void prepare(WGPUCommandEncoder encoder,WGPUTextureView sceneDepth,PassTiming& timing){
        // Refresh the source binding: the renderer keeps two targets and can return to an older one.
        if(boundDepth_!=sceneDepth){if(copyGroup_)wgpuBindGroupRelease(copyGroup_);WGPUBindGroupEntry e{};e.textureView=sceneDepth;WGPUBindGroupDescriptor gd{};gd.layout=copyLayout_;gd.entryCount=1;gd.entries=&e;copyGroup_=wgpuDeviceCreateBindGroup(gpu_.device(),&gd);boundDepth_=sceneDepth;}
        WGPURenderPassColorAttachment c{};c.view=sceneDepthView_;c.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;c.loadOp=WGPULoadOp_Clear;c.storeOp=WGPUStoreOp_Store;
        WGPURenderPassDescriptor d{};d.colorAttachmentCount=1;d.colorAttachments=&c;WGPUPassTimestampWrites t{};d.timestampWrites=timing.pass("media-depth",t);
        auto p=wgpuCommandEncoderBeginRenderPass(encoder,&d);wgpuRenderPassEncoderSetPipeline(p,copy_);wgpuRenderPassEncoderSetBindGroup(p,0,copyGroup_,0,nullptr);wgpuRenderPassEncoderDraw(p,3,1,0,0);wgpuRenderPassEncoderEnd(p);wgpuRenderPassEncoderRelease(p);
        WGPURenderPassDepthStencilAttachment z{};z.view=depthView_;z.depthLoadOp=WGPULoadOp_Clear;z.depthStoreOp=WGPUStoreOp_Store;z.depthClearValue=0;d.colorAttachmentCount=0;d.depthStencilAttachment=&z;d.timestampWrites=timing.pass("media-depth",t);
        p=wgpuCommandEncoderBeginRenderPass(encoder,&d);wgpuRenderPassEncoderSetPipeline(p,down_);wgpuRenderPassEncoderSetBindGroup(p,0,downGroup_,0,nullptr);wgpuRenderPassEncoderDraw(p,3,1,0,0);wgpuRenderPassEncoderEnd(p);wgpuRenderPassEncoderRelease(p);
    }
    void composite(WGPURenderPassEncoder pass){wgpuRenderPassEncoderSetPipeline(pass,up_);wgpuRenderPassEncoderSetBindGroup(pass,0,upGroup_,0,nullptr);wgpuRenderPassEncoderDraw(pass,3,1,0,0);}
};
}
