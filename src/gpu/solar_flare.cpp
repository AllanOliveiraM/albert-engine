#include "solar_flare.hpp"
#include "gpu/atmosphere.hpp"
#include "gpu/programs.hpp"
#include <string>
#include <string_view>

namespace albert::gpu {
namespace {
WGPUStringView sv(std::string_view s){return {s.data(),s.size()};}
constexpr const char* parameters=R"(
struct Parameters { source: vec4f, screen: vec4f, disc: vec4f };
)";
constexpr const char* measureShader=R"(
@group(1) @binding(0) var<uniform> flare: Parameters;
@group(1) @binding(1) var<storage, read_write> signal: vec4f;
@group(1) @binding(3) var cloudImage: texture_2d<f32>;
@group(1) @binding(4) var cloudLinear: sampler;
fn shellTransmission(c: vec3f, ray: vec3f) -> f32 {
    let r = bottomRadius() + frame.weather.x;
    let b = dot(c, ray); let d = b * b - dot(c,c) + r * r;
    if (frame.weather.y <= 0.0 || d < 0.0) { return 1.0; }
    var t = 1.0;
    for (var i = 0u; i < 2u; i++) {
        let distance = -b + select(-sqrt(d), sqrt(d), i == 1u);
        if (distance > 0.0) {
            let at = c + ray * distance; let mu = max(abs(dot(normalize(at),ray)),0.05);
            t *= exp(-cloudOpticalDepth(cloudCover(at)) * frame.weather.y / mu);
        }
    }
    return t;
}
// One independent disc sample per lane. Keep texture reads and atmospheric
// functions out of nested serial loops (especially for the D3D12 FXC path).
fn discSample(i: u32) -> vec3f {
    let angle = f32(i) * 2.39996323; let radius = sqrt((f32(i)+0.5)/32.0);
    let uv = flare.source.xy + vec2f(cos(angle),sin(angle)) * radius * flare.disc.xy;
    if (any(uv < vec2f(0.0)) || any(uv >= vec2f(1.0))) { return vec3f(0.0); }
    let pixel = vec2i(uv * flare.screen.xy);
    // Reversed-Z needs a relative tolerance, including at tiny near planes.
    let visible = DEPTH_VISIBILITY;
    if (visible == 0.0) { return vec3f(0.0); }
    let ndc = (uv * 2.0 - 1.0) * vec2f(1.0,-1.0);
    let rayView = normalize(vec3f(ndc.x/frame.projection[0][0], ndc.y/frame.projection[1][1],-1.0));
    let ray = transpose(mat3x3f(frame.view[0].xyz,frame.view[1].xyz,frame.view[2].xyz)) * rayView;
    var transmission = vec3f(1.0);
    if (frame.air.w > 0.0) {
        let airRay = normalize((frame.planet * vec4f(ray,0.0)).xyz);
        if (skyHitsGround(frame.air.xyz,airRay)) { return vec3f(0.0); }
        transmission = airFilter(frame.camera.xyz + ray * flare.source.w);
        if (flare.screen.w == 0.0) { transmission *= shellTransmission(frame.air.xyz,airRay); }
    }
    if (flare.screen.w > 0.0) {
        transmission *= mix(1.0,textureSampleLevel(cloudImage,cloudLinear,uv,0.0).a,flare.screen.w);
    }
    return visible * transmission * vec3f(1.0,0.96,0.88) * frame.exposure.x / 32.0;
}
var<workgroup> discColours: array<vec3f,32>;
@compute @workgroup_size(32) fn measure(@builtin(local_invocation_index) i: u32) {
    discColours[i] = discSample(i);
    // Every lane reaches the barrier, including samples rejected by discSample.
    workgroupBarrier();
    if (i == 0u) {
        var colour = vec3f(0.0);
        // Texture-free reduction in the original deterministic sample order.
        for (var j = 0u; j < 32u; j++) { colour += discColours[j]; }
        signal = vec4f(min(colour * flare.disc.z,vec3f(1.0)),1.0);
    }
}
)";
constexpr const char* drawShader=R"(
@group(0) @binding(0) var<uniform> flare: Parameters;
@group(0) @binding(1) var<storage, read> signal: vec4f;
struct Vertex { @builtin(position) position: vec4f, @location(0) uv: vec2f };
@vertex fn vs(@builtin(vertex_index) i:u32) -> Vertex {
    let p = array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));
    var v: Vertex; v.position=vec4f(p[i],0,1); v.uv=p[i]*vec2f(0.5,-0.5)+0.5; return v;
}
fn ghost(p: vec2f, centre: vec2f, radius: f32) -> f32 {
    let r=length(p-centre)/radius;
    return exp(-r*r*3.0) * (1.0-smoothstep(0.7,1.0,r));
}
@fragment fn fs(v: Vertex) -> @location(0) vec4f {
    let aspect=vec2f(flare.screen.x/flare.screen.y,1.0);
    let p=(v.uv-0.5)*aspect; let sun=(flare.source.xy-0.5)*aspect;
    let halo=exp(-length(p-sun)*35.0);
    let offAxis=0.25+0.75*smoothstep(0.0,0.35,length(sun));
    let ghosts=offAxis*(vec3f(0.75,0.9,1.0)*ghost(p,-sun*0.45,0.032)*0.32
        + vec3f(1.0,0.82,0.6)*ghost(p,sun*0.3,0.018)*0.22
        + vec3f(0.7,1.0,0.86)*ghost(p,-sun*0.9,0.065)*0.16);
    // A bounded display-space optical overlay. The scene is already tone-mapped;
    // the effect is deliberately weak and preserves the target's alpha.
    return vec4f(signal.rgb * flare.screen.z * (halo + ghosts),0.0);
}
)";
}
SolarFlare::SolarFlare(Context& gpu,WGPUBindGroupLayout frameLayout):gpu_(gpu){
    auto device=gpu.device();
    auto buffer=[&](unsigned bytes,WGPUBufferUsage usage){WGPUBufferDescriptor d{};d.size=bytes;d.usage=usage;return wgpuDeviceCreateBuffer(device,&d);};
    parameters_=buffer(48,WGPUBufferUsage_Uniform|WGPUBufferUsage_CopyDst);signal_=buffer(16,WGPUBufferUsage_Storage);
    for(unsigned variant=0;variant<2;++variant){
        WGPUBindGroupLayoutEntry e[5]{};for(unsigned i=0;i<5;++i){e[i].binding=i;e[i].visibility=WGPUShaderStage_Compute;}
        e[0].buffer.type=WGPUBufferBindingType_Uniform;e[1].buffer.type=WGPUBufferBindingType_Storage;
        e[2].texture.sampleType=WGPUTextureSampleType_Depth;e[2].texture.viewDimension=WGPUTextureViewDimension_2D;e[2].texture.multisampled=variant!=0;
        e[3].texture.sampleType=WGPUTextureSampleType_Float;e[3].texture.viewDimension=WGPUTextureViewDimension_2D;e[4].sampler.type=WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=5;ld.entries=e;measureLayouts_[variant]=wgpuDeviceCreateBindGroupLayout(device,&ld);
        std::string code=std::string(prelude)+atmosphereWgsl+air+parameters;
        code+=variant?"@group(1) @binding(2) var depth: texture_depth_multisampled_2d;\n":"@group(1) @binding(2) var depth: texture_depth_2d;\n";
        std::string visibility;
        const unsigned samples=variant?4:1;
        for(unsigned sample=0;sample<samples;++sample){
            if(sample)visibility+=" + ";
            visibility+="select(0.0,"+std::string(variant?"0.25":"1.0")+",textureLoad(depth,pixel,"+std::to_string(sample)+") <= flare.source.z * 1.02)";
        }
        std::string body=measureShader;const std::string token="DEPTH_VISIBILITY";body.replace(body.find(token),token.size(),visibility);code+=body;
        WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code=sv(code);WGPUShaderModuleDescriptor md{};md.nextInChain=&wgsl.chain;
        auto module=wgpuDeviceCreateShaderModule(device,&md);WGPUBindGroupLayout layouts[]{frameLayout,measureLayouts_[variant]};WGPUPipelineLayoutDescriptor pl{};pl.bindGroupLayoutCount=2;pl.bindGroupLayouts=layouts;
        auto layout=wgpuDeviceCreatePipelineLayout(device,&pl);WGPUComputePipelineDescriptor pd{};pd.layout=layout;pd.compute.module=module;pd.compute.entryPoint=sv("measure");measurePipelines_[variant]=wgpuDeviceCreateComputePipeline(device,&pd);
        wgpuPipelineLayoutRelease(layout);wgpuShaderModuleRelease(module);
    }
    WGPUBindGroupLayoutEntry e[2]{};for(unsigned i=0;i<2;++i){e[i].binding=i;e[i].visibility=WGPUShaderStage_Fragment;}
    e[0].buffer.type=WGPUBufferBindingType_Uniform;e[1].buffer.type=WGPUBufferBindingType_ReadOnlyStorage;
    WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=2;ld.entries=e;drawLayout_=wgpuDeviceCreateBindGroupLayout(device,&ld);
    const std::string code=std::string(parameters)+drawShader;WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code=sv(code);WGPUShaderModuleDescriptor md{};md.nextInChain=&wgsl.chain;
    auto module=wgpuDeviceCreateShaderModule(device,&md);WGPUPipelineLayoutDescriptor pl{};pl.bindGroupLayoutCount=1;pl.bindGroupLayouts=&drawLayout_;auto layout=wgpuDeviceCreatePipelineLayout(device,&pl);
    WGPUBlendState blend{};blend.color={WGPUBlendOperation_Add,WGPUBlendFactor_One,WGPUBlendFactor_One};blend.alpha={WGPUBlendOperation_Add,WGPUBlendFactor_Zero,WGPUBlendFactor_One};
    WGPUColorTargetState colour{};colour.format=gpu.format();colour.blend=&blend;colour.writeMask=WGPUColorWriteMask_All;WGPUFragmentState fs{};fs.module=module;fs.entryPoint=sv("fs");fs.targetCount=1;fs.targets=&colour;
    WGPURenderPipelineDescriptor pd{};pd.layout=layout;pd.vertex.module=module;pd.vertex.entryPoint=sv("vs");pd.fragment=&fs;pd.primitive.topology=WGPUPrimitiveTopology_TriangleList;pd.multisample.count=1;pd.multisample.mask=~0u;
    drawPipeline_=wgpuDeviceCreateRenderPipeline(device,&pd);wgpuPipelineLayoutRelease(layout);wgpuShaderModuleRelease(module);
    WGPUBindGroupEntry b[2]{};b[0].binding=0;b[0].buffer=parameters_;b[0].size=48;b[1].binding=1;b[1].buffer=signal_;b[1].size=16;
    WGPUBindGroupDescriptor bd{};bd.layout=drawLayout_;bd.entryCount=2;bd.entries=b;drawGroup_=wgpuDeviceCreateBindGroup(device,&bd);
}
SolarFlare::~SolarFlare(){
    wgpuBindGroupRelease(drawGroup_);wgpuRenderPipelineRelease(drawPipeline_);wgpuBindGroupLayoutRelease(drawLayout_);
    for(unsigned i=0;i<2;++i){wgpuComputePipelineRelease(measurePipelines_[i]);wgpuBindGroupLayoutRelease(measureLayouts_[i]);}
    wgpuBufferRelease(parameters_);wgpuBufferRelease(signal_);
}
void SolarFlare::measure(WGPUCommandEncoder encoder,WGPUBindGroup frame,WGPUTextureView depth,unsigned samples,WGPUTextureView clouds,WGPUSampler sampler,const std::array<float,12>& parameters,const WGPUPassTimestampWrites* timing){
    wgpuQueueWriteBuffer(gpu_.queue(),parameters_,0,parameters.data(),48);const unsigned variant=samples>1?1:0;
    WGPUBindGroupEntry e[5]{};for(unsigned i=0;i<5;++i)e[i].binding=i;
    e[0].buffer=parameters_;e[0].size=48;e[1].buffer=signal_;e[1].size=16;e[2].textureView=depth;e[3].textureView=clouds;e[4].sampler=sampler;
    WGPUBindGroupDescriptor d{};d.layout=measureLayouts_[variant];d.entryCount=5;d.entries=e;auto group=wgpuDeviceCreateBindGroup(gpu_.device(),&d);
    WGPUComputePassDescriptor cd{};cd.timestampWrites=timing;
    auto pass=wgpuCommandEncoderBeginComputePass(encoder,&cd);wgpuComputePassEncoderSetPipeline(pass,measurePipelines_[variant]);wgpuComputePassEncoderSetBindGroup(pass,0,frame,0,nullptr);wgpuComputePassEncoderSetBindGroup(pass,1,group,0,nullptr);wgpuComputePassEncoderDispatchWorkgroups(pass,1,1,1);wgpuComputePassEncoderEnd(pass);wgpuComputePassEncoderRelease(pass);wgpuBindGroupRelease(group);
}
void SolarFlare::draw(WGPUCommandEncoder encoder,WGPUTextureView target,const WGPUPassTimestampWrites* timing){
    WGPURenderPassColorAttachment c{};c.view=target;c.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;c.loadOp=WGPULoadOp_Load;c.storeOp=WGPUStoreOp_Store;
    WGPURenderPassDescriptor d{};d.colorAttachmentCount=1;d.colorAttachments=&c;d.timestampWrites=timing;auto pass=wgpuCommandEncoderBeginRenderPass(encoder,&d);
    wgpuRenderPassEncoderSetPipeline(pass,drawPipeline_);wgpuRenderPassEncoderSetBindGroup(pass,0,drawGroup_,0,nullptr);wgpuRenderPassEncoderDraw(pass,3,1,0,0);wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
}
}
