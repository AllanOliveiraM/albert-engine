#include "gpu/scene.hpp"
#include "gpu/programs.hpp"
#include "gpu/atmosphere.hpp"
#include <webgpu/wgpu.h>
#include <threepp/threepp.hpp>
#include <threepp/materials/ShaderMaterial.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string_view>
#include <unordered_map>
namespace albert::gpu {
namespace {
using namespace threepp;
constexpr std::uint32_t objectStride=256;   // Object uniforms (224 bytes) at the dynamic-offset alignment.
constexpr unsigned evictAfter=240;          // Frames without use before GPU copies are released.
WGPUStringView sv(std::string_view s){return{s.data(),s.size()};}
WGPUBlendFactor factor(BlendFactor f){
    switch(f){case BlendFactor::Zero:return WGPUBlendFactor_Zero;case BlendFactor::One:return WGPUBlendFactor_One;case BlendFactor::SrcColor:return WGPUBlendFactor_Src;
    case BlendFactor::OneMinusSrcColor:return WGPUBlendFactor_OneMinusSrc;case BlendFactor::SrcAlpha:return WGPUBlendFactor_SrcAlpha;case BlendFactor::OneMinusSrcAlpha:return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::DstAlpha:return WGPUBlendFactor_DstAlpha;case BlendFactor::OneMinusDstAlpha:return WGPUBlendFactor_OneMinusDstAlpha;case BlendFactor::DstColor:return WGPUBlendFactor_Dst;
    case BlendFactor::OneMinusDstColor:return WGPUBlendFactor_OneMinusDst;case BlendFactor::SrcAlphaSaturate:return WGPUBlendFactor_SrcAlphaSaturated;}
    return WGPUBlendFactor_One;
}
WGPUBlendOperation operation(BlendEquation e){return e==BlendEquation::Subtract?WGPUBlendOperation_Subtract:e==BlendEquation::ReverseSubtract?WGPUBlendOperation_ReverseSubtract:e==BlendEquation::Min?WGPUBlendOperation_Min:e==BlendEquation::Max?WGPUBlendOperation_Max:WGPUBlendOperation_Add;}
// threepp's blending (premultipliedAlpha false), as GLRenderer sets it.
bool blend(const Material& m,WGPUBlendState& b){
    if(!m.transparent&&m.blending!=Blending::Multiply&&m.blending!=Blending::Additive&&m.blending!=Blending::Custom)return false;
    auto set=[&](WGPUBlendFactor cs,WGPUBlendFactor cd,WGPUBlendFactor as,WGPUBlendFactor ad,WGPUBlendOperation op=WGPUBlendOperation_Add){b.color={op,cs,cd};b.alpha={op,as,ad};};
    switch(m.blending){
    case Blending::None:return false;
    case Blending::Normal:set(WGPUBlendFactor_SrcAlpha,WGPUBlendFactor_OneMinusSrcAlpha,WGPUBlendFactor_One,WGPUBlendFactor_OneMinusSrcAlpha);break;
    case Blending::Additive:set(WGPUBlendFactor_SrcAlpha,WGPUBlendFactor_One,WGPUBlendFactor_SrcAlpha,WGPUBlendFactor_One);break;
    case Blending::Subtractive:set(WGPUBlendFactor_Zero,WGPUBlendFactor_OneMinusSrc,WGPUBlendFactor_Zero,WGPUBlendFactor_One);break;
    case Blending::Multiply:set(WGPUBlendFactor_Zero,WGPUBlendFactor_Src,WGPUBlendFactor_Zero,WGPUBlendFactor_SrcAlpha);break;
    case Blending::Custom:b.color={operation(m.blendEquation),factor(m.blendSrc),factor(m.blendDst)};
        b.alpha={operation(m.blendEquationAlpha.value_or(m.blendEquation)),factor(m.blendSrcAlpha.value_or(m.blendSrc)),factor(m.blendDstAlpha.value_or(m.blendDst))};break;
    }
    return true;
}
std::size_t align(std::size_t v,std::size_t a){return (v+a-1)/a*a;}
std::uint16_t toHalf(float f){ // Round to nearest; overflow to infinity, tiny values to subnormals or zero.
    std::uint32_t x;std::memcpy(&x,&f,4);const std::uint32_t sign=(x>>16)&0x8000u;const int e=static_cast<int>((x>>23)&255)-112;std::uint32_t m=x&0x7fffffu;
    if(((x>>23)&255)==255)return static_cast<std::uint16_t>(sign|0x7c00u|(m?0x200u:0u));
    if(e>=31)return static_cast<std::uint16_t>(sign|0x7c00u);
    if(e<=0){if(e<-10)return static_cast<std::uint16_t>(sign);m|=0x800000u;const int shift=14-e;return static_cast<std::uint16_t>(sign|((m+(1u<<(shift-1)))>>shift));}
    const std::uint32_t h=(static_cast<std::uint32_t>(e)<<10)|(m>>13);return static_cast<std::uint16_t>(sign|(h+((m>>12)&1u)));
}
// WGSL uniform layout: f32 4, vec2 8, vec3 16 (size 12), vec4 16, mat3 16 (48), mat4 16 (64).
std::pair<std::size_t,std::size_t> layout(Uniform u){
    switch(u){case Uniform::f32:return{4,4};case Uniform::vec2:return{8,8};case Uniform::vec3:return{16,12};case Uniform::vec4:return{16,16};case Uniform::mat3:return{16,48};case Uniform::mat4:return{16,64};}
    return{4,4};
}
void put(std::vector<std::byte>& out,std::size_t at,const float* v,std::size_t n){std::memcpy(out.data()+at,v,n*sizeof(float));}
void pack(std::vector<std::byte>& out,std::size_t at,Uniform u,const UniformValue* value){
    if(!value)return;
    std::visit([&](const auto& x){
        using T=std::decay_t<decltype(x)>;
        if constexpr(std::is_same_v<T,float>){put(out,at,&x,1);}
        else if constexpr(std::is_same_v<T,int>||std::is_same_v<T,bool>){const float f=static_cast<float>(x);put(out,at,&f,1);}
        else if constexpr(std::is_same_v<T,Vector2>){const float f[]{x.x,x.y};put(out,at,f,2);}
        else if constexpr(std::is_same_v<T,Vector3>){const float f[]{x.x,x.y,x.z};put(out,at,f,3);}
        else if constexpr(std::is_same_v<T,Vector3*>){if(x){const float f[]{x->x,x->y,x->z};put(out,at,f,3);}}
        else if constexpr(std::is_same_v<T,Color>){const float f[]{x.r,x.g,x.b,1};put(out,at,f,u==Uniform::vec4?4:3);}
        else if constexpr(std::is_same_v<T,Vector4>){const float f[]{x.x,x.y,x.z,x.w};put(out,at,f,4);}
        else if constexpr(std::is_same_v<T,Matrix4>){put(out,at,x.elements.data(),16);}
        else if constexpr(std::is_same_v<T,Matrix4*>){if(x)put(out,at,x->elements.data(),16);}
        else if constexpr(std::is_same_v<T,Matrix3>){for(int c=0;c<3;++c)put(out,at+16*c,x.elements.data()+3*c,3);}
    },*value);
}
// Built-in materials as WGSL: Lambert/Blinn-Phong (Phong) or a GGX microfacet (Standard) under one
// directional and one ambient light, with map (sRGB decoded), alphaMap + alphaTest and a light
// map on uv2 added as irradiance (threepp's lightMap). Basic is unlit.
const char* const builtinWgsl=R"(
struct Material { colour: vec4f, emissive: vec4f, params: vec4f, flags: vec4f, specular: vec4f, more: vec4f };
// params: roughness, metalness or shininess, alphaTest, lightMapIntensity
// flags: map sRGB, kind (0 basic, 1 phong, 2 standard, 3 lambert), has alphaMap, has lightMap
// specular: Phong specular colour, bumpScale; more: has bumpMap, has roughnessMap, double sided, flat shading; emissive.w: bumpMap wraps
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var map: texture_2d<f32>;
@group(2) @binding(2) var mapSampler: sampler;
@group(2) @binding(3) var alphaMap: texture_2d<f32>;
@group(2) @binding(4) var alphaSampler: sampler;
@group(2) @binding(5) var lightMap: texture_2d<f32>;
@group(2) @binding(6) var lightSampler: sampler;
@group(2) @binding(7) var bumpMap: texture_2d<f32>;
@group(2) @binding(8) var bumpSampler: sampler;
@group(2) @binding(9) var roughnessMap: texture_2d<f32>;
@group(2) @binding(10) var roughnessSampler: sampler;
struct Varyings { @builtin(position) clip: vec4f, @location(0) position: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f, @location(3) uv2: vec2f, @location(4) world: vec3f, @location(5) worldNormal: vec3f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f, @location(3) uv2: vec2f) -> Varyings {
    var o: Varyings; let view = object.modelView * vec4f(position, 1.0);
    o.clip = frame.projection * view; o.position = view.xyz; o.normal = object.normalView * normal; o.uv = uv; o.uv2 = uv2;
    o.world = (object.model * vec4f(position, 1.0)).xyz; o.worldNormal = normalize(object.normalWorld * normal);
    return o;
}
@fragment fn fs_main(v: Varyings, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    let face = select(-1.0, 1.0, front);
    var texel = textureSample(map, mapSampler, v.uv);
    if (material.flags.x > 0.5) { texel = vec4f(srgbToLinear(texel.rgb), texel.a); }
    var base = material.colour * texel;
    let mask = textureSample(alphaMap, alphaSampler, v.uv).g;
    let lightTexel = textureSample(lightMap, lightSampler, v.uv2).rgb;
    let roughTexel = textureSample(roughnessMap, roughnessSampler, v.uv);
    let dx = dpdx(v.uv); let dy = dpdy(v.uv); let scale = material.specular.w;
    let wraps = material.emissive.w > 0.5; let h = scale * heightAt(bumpMap, v.uv, wraps);
    let dBx = scale * heightAt(bumpMap, v.uv + dx, wraps) - h; let dBy = scale * heightAt(bumpMap, v.uv + dy, wraps) - h;
    var geometryNormal = normalize(v.normal); if (material.more.z > 0.5) { geometryNormal = geometryNormal * face; }
    if (material.more.w > 0.5) { geometryNormal = flatNormal(v.position); }
    var n = geometryNormal; if (material.more.x > 0.5) { n = bumpNormal(v.position, geometryNormal, dBx, dBy, face); }
    if (material.flags.z > 0.5) { base.a = base.a * mask; }
    if (base.a < material.params.z) { discard; }
    let light = airLight(v.world, v.worldNormal);
    let mapped = select(vec3f(0.0), lightTexel * material.params.w, material.flags.w > 0.5); let extra = mapped + light.sky * frame.sunColor.rgb;
    let shadow = shadowFactor(v.world, v.worldNormal) * light.sun;
    var colour: vec3f;
    let kind = material.flags.y;
    if (kind < 0.5) { colour = base.rgb * select(vec3f(1.0), mapped / PI_L, material.flags.w > 0.5); return vec4f(finish(colour + material.emissive.rgb), base.a); } // Unlit, no air.
    else if (kind > 1.5 && kind < 2.5) {
        var roughness = material.params.x * select(1.0, roughTexel.g, material.more.y > 0.5);
        if (material.more.y > 0.5 && frame.weather.z > 0.0 && frame.air.w > 0.0) { // Water (roughness texels under 0.5): Cox-Munk slopes for the surface wind.
            roughness = mix(roughness, sqrt(sqrt(0.003 + 0.00512 * frame.weather.z)), 1.0 - smoothstep(0.35, 0.5, roughTexel.g)); }
        colour = standardLight(base.rgb, roughness, material.params.y, n, geometryNormal, v.position, extra, shadow)
            + airReflection(v.world, n, v.position, roughness, mix(vec3f(0.04), base.rgb, material.params.y)); // The sky on smooth water.
    } else if (kind > 2.5) { colour = lambertLight(base.rgb, n, mapped * PI_L + light.sky * frame.sunColor.rgb, shadow); } // threepp keeps π on Lambert light maps.
    else { colour = phongLight(base.rgb, material.specular.rgb, material.params.y, n, v.position, extra, shadow); }
    return vec4f(finish(airApply(v.world, colour + material.emissive.rgb)), base.a);
}
)";
}
struct SceneRenderer::Impl {
    Context& gpu;WGPUDevice device;WGPUQueue queue;
    WGPUTexture depth=nullptr,colour=nullptr;WGPUTextureView depthView=nullptr,colourView=nullptr;unsigned depthWidth=0,depthHeight=0,depthSamples=0; // colour: multisampled, resolved into the target.
    WGPUBuffer frameBuffer=nullptr,objectBuffer=nullptr;std::size_t objectCapacity=0;
    WGPUBindGroupLayout frameLayout=nullptr,objectLayout=nullptr;WGPUBindGroup frameGroup=nullptr,objectGroup=nullptr;
    WGPUBuffer zeros=nullptr;WGPUTexture whiteTexture=nullptr,blackTexture=nullptr;WGPUTextureView white=nullptr,black=nullptr;WGPUSampler linear=nullptr;
    struct Buffer {WGPUBuffer buffer=nullptr;std::size_t bytes=0;unsigned version=~0u;};
    struct Geometry {Buffer index;unsigned used=0;}; // Index buffers belong to one geometry; attributes may be shared.
    struct Attribute {std::weak_ptr<BufferAttribute> owner;Buffer buffer;unsigned used=0;};
    // A texture's GPU copy. Large images stream in row slices (~8 MB a frame) into `pending` while
    // the previous copy (or nothing) keeps drawing; mipmaps are rendered on the GPU at the end.
    struct Texture {WGPUTexture texture=nullptr;WGPUTextureView view=nullptr;WGPUSampler sampler=nullptr;unsigned version=~0u,used=0;
        WGPUTexture pending=nullptr;unsigned pendingVersion=~0u,row=0,width=0,height=0,levels=1;std::size_t texel=0;std::vector<std::byte> converted;std::uint64_t serial=0;unsigned shownWidth=0,shownHeight=0;};
    // Bind groups are keyed by view serials: a released view's address may come back for a new one.
    struct MaterialState {Buffer uniforms;WGPUBindGroup group=nullptr;std::vector<std::uint64_t> serials;std::string program;unsigned used=0;};
    struct Compiled {WGPUShaderModule module=nullptr;WGPUBindGroupLayout layout=nullptr;WGPUPipelineLayout pipelineLayout=nullptr;Program program;};
    std::unordered_map<unsigned,Geometry> geometries;std::unordered_map<const BufferAttribute*,Attribute> attributes;std::unordered_map<unsigned,Texture> textures;std::uint64_t serials=2;std::unordered_map<unsigned,MaterialState> materials; // Serials 1, 2: white, black.
    std::unordered_map<std::string,Compiled> compiled;std::map<std::array<std::int64_t,24>,WGPURenderPipeline> pipelines;std::set<std::string> reported;
    unsigned frame=0,drawn=0;bool floatFilterable=false;float tickNanoseconds=1;
    // Sun shadow map: depth from the light's orthographic camera, compared with PCF in the receivers.
    WGPUTexture shadowTexture=nullptr,dummyShadow=nullptr;WGPUTextureView shadowView=nullptr,dummyShadowView=nullptr;unsigned shadowSize=0;std::uint64_t shadowSerial=0,boundFrame=~0ull; // boundFrame: the shadow map serial in frameGroup (0: the dummy).
    const Atmosphere* boundAir=nullptr;std::uint64_t boundClouds=0;WGPUSampler airSampler=nullptr,cloudSampler=nullptr;WGPUTexture noVolume=nullptr;WGPUTextureView noVolumeView=nullptr;WGPUBuffer noAir=nullptr; // Placeholders without air.
    WGPUSampler shadowSampler=nullptr;WGPUBindGroup casterFrame=nullptr;WGPURenderPipeline shadowPipeline=nullptr;WGPUPipelineLayout shadowLayout=nullptr;WGPUShaderModule shadowModule=nullptr;
    // Scene-pass GPU time: two timestamps resolved into a buffer, read back a few frames later.
    WGPUQuerySet timestamps=nullptr;WGPUBuffer resolved=nullptr,readback=nullptr;bool mapping=false,mapPending=false;double gpuMs=-1;WGPUBufferMapCallbackInfo pendingMap{};
    explicit Impl(Context& c):gpu(c),device(c.device()),queue(c.queue()){
        auto uniformLayout=[&](bool dynamic){
            WGPUBindGroupLayoutEntry e{};e.binding=0;e.visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;e.buffer.type=WGPUBufferBindingType_Uniform;e.buffer.hasDynamicOffset=dynamic;
            WGPUBindGroupLayoutDescriptor d{};d.entryCount=1;d.entries=&e;return wgpuDeviceCreateBindGroupLayout(device,&d);};
        objectLayout=uniformLayout(true);
        { // Frame: uniforms, the shadow depth map and its comparison sampler; the air's parameters, tables and sampler.
            WGPUBindGroupLayoutEntry e[11]{};const auto both=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;
            e[0].binding=0;e[0].visibility=both;e[0].buffer.type=WGPUBufferBindingType_Uniform;
            e[1].binding=1;e[1].visibility=WGPUShaderStage_Fragment;e[1].texture.sampleType=WGPUTextureSampleType_Depth;e[1].texture.viewDimension=WGPUTextureViewDimension_2D;
            e[2].binding=2;e[2].visibility=WGPUShaderStage_Fragment;e[2].sampler.type=WGPUSamplerBindingType_Comparison;
            e[3].binding=3;e[3].visibility=both;e[3].buffer.type=WGPUBufferBindingType_Uniform;
            for(std::uint32_t i=4;i<8;++i){e[i].binding=i;e[i].visibility=both;e[i].texture.sampleType=WGPUTextureSampleType_Float;e[i].texture.viewDimension=i==5||i==6?WGPUTextureViewDimension_3D:WGPUTextureViewDimension_2D;}
            e[8].binding=8;e[8].visibility=both;e[8].sampler.type=WGPUSamplerBindingType_Filtering;
            e[9].binding=9;e[9].visibility=WGPUShaderStage_Fragment;e[9].texture.sampleType=WGPUTextureSampleType_Float;e[9].texture.viewDimension=WGPUTextureViewDimension_2D; // The cloud map (shadows).
            e[10].binding=10;e[10].visibility=WGPUShaderStage_Fragment;e[10].sampler.type=WGPUSamplerBindingType_Filtering;
            WGPUBindGroupLayoutDescriptor d{};d.entryCount=11;d.entries=e;frameLayout=wgpuDeviceCreateBindGroupLayout(device,&d);
        }
        frameBuffer=buffer(512,WGPUBufferUsage_Uniform);
        {WGPUTextureDescriptor d{};d.size={1,1,1};d.format=WGPUTextureFormat_Depth32Float;d.usage=WGPUTextureUsage_TextureBinding;d.mipLevelCount=1;d.sampleCount=1;d.dimension=WGPUTextureDimension_2D;
         dummyShadow=wgpuDeviceCreateTexture(device,&d);dummyShadowView=wgpuTextureCreateView(dummyShadow,nullptr);
         WGPUSamplerDescriptor sd{};sd.addressModeU=sd.addressModeV=sd.addressModeW=WGPUAddressMode_ClampToEdge;sd.magFilter=sd.minFilter=WGPUFilterMode_Linear;sd.mipmapFilter=WGPUMipmapFilterMode_Nearest;
         sd.compare=WGPUCompareFunction_LessEqual;sd.lodMaxClamp=1;sd.maxAnisotropy=1;shadowSampler=wgpuDeviceCreateSampler(device,&sd);
         sd.compare=WGPUCompareFunction_Undefined;airSampler=wgpuDeviceCreateSampler(device,&sd);
         sd.addressModeU=WGPUAddressMode_Repeat;cloudSampler=wgpuDeviceCreateSampler(device,&sd); // Longitude wraps.
         d.size={1,1,1};d.format=WGPUTextureFormat_RGBA8Unorm;d.dimension=WGPUTextureDimension_3D;noVolume=wgpuDeviceCreateTexture(device,&d);noVolumeView=wgpuTextureCreateView(noVolume,nullptr);}
        noAir=buffer(Atmosphere::parameterBytes,WGPUBufferUsage_Uniform);
        zeros=buffer(64,WGPUBufferUsage_Vertex);
        WGPUTextureDescriptor t{};t.size={1,1,1};t.format=WGPUTextureFormat_RGBA8Unorm;t.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;t.mipLevelCount=1;t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;
        auto solid=[&](std::uint32_t rgba,WGPUTexture& texture,WGPUTextureView& view){texture=wgpuDeviceCreateTexture(device,&t);view=wgpuTextureCreateView(texture,nullptr);
            WGPUTexelCopyTextureInfo dst{};dst.texture=texture;WGPUTexelCopyBufferLayout l{};l.bytesPerRow=4;l.rowsPerImage=1;wgpuQueueWriteTexture(queue,&dst,&rgba,4,&l,&t.size);};
        solid(0xffffffffu,whiteTexture,white);solid(0xff000000u,blackTexture,black); // Black: GL's empty texture for ShaderMaterials.
        WGPUSamplerDescriptor s{};s.addressModeU=s.addressModeV=s.addressModeW=WGPUAddressMode_ClampToEdge;s.magFilter=s.minFilter=WGPUFilterMode_Linear;s.mipmapFilter=WGPUMipmapFilterMode_Linear;s.lodMaxClamp=32;s.maxAnisotropy=1;
        linear=wgpuDeviceCreateSampler(device,&s);
        floatFilterable=wgpuDeviceHasFeature(device,WGPUFeatureName_Float32Filterable);tickNanoseconds=wgpuQueueGetTimestampPeriod(queue);
        if(wgpuDeviceHasFeature(device,WGPUFeatureName_TimestampQuery)){
            WGPUQuerySetDescriptor q{};q.type=WGPUQueryType_Timestamp;q.count=2;timestamps=wgpuDeviceCreateQuerySet(device,&q);
            WGPUBufferDescriptor b{};b.size=16;b.usage=WGPUBufferUsage_QueryResolve|WGPUBufferUsage_CopySrc;resolved=wgpuDeviceCreateBuffer(device,&b);
            b.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;readback=wgpuDeviceCreateBuffer(device,&b);
        }
        Program b;b.wgsl=builtinWgsl;b.attributes={{"normal",3},{"uv",2},{"uv2",2}};b.textures={"map","alphaMap","lightMap","bumpMap","roughnessMap"};define("albert:builtin",b);
    }
    ~Impl(){
        for(auto& [k,p]:pipelines)wgpuRenderPipelineRelease(p);for(auto& [k,p]:mipPipelines)wgpuRenderPipelineRelease(p);
        if(mipModule){wgpuShaderModuleRelease(mipModule);wgpuBindGroupLayoutRelease(mipLayout);wgpuPipelineLayoutRelease(mipPipelineLayout);}
        for(auto& [k,c]:compiled){wgpuShaderModuleRelease(c.module);wgpuBindGroupLayoutRelease(c.layout);wgpuPipelineLayoutRelease(c.pipelineLayout);}
        for(auto& [k,g]:geometries)release(g);for(auto& [k,a]:attributes)release(a);for(auto& [k,t]:textures)release(t);for(auto& [k,m]:materials)release(m);
        for(auto b:{frameBuffer,objectBuffer,zeros,resolved,readback})if(b)wgpuBufferRelease(b);if(timestamps)wgpuQuerySetRelease(timestamps);
        if(frameGroup)wgpuBindGroupRelease(frameGroup);if(casterFrame)wgpuBindGroupRelease(casterFrame);if(objectGroup)wgpuBindGroupRelease(objectGroup);
        if(shadowView)wgpuTextureViewRelease(shadowView);if(shadowTexture)wgpuTextureRelease(shadowTexture);wgpuTextureViewRelease(dummyShadowView);wgpuTextureRelease(dummyShadow);wgpuSamplerRelease(shadowSampler);
        wgpuSamplerRelease(airSampler);wgpuSamplerRelease(cloudSampler);wgpuTextureViewRelease(noVolumeView);wgpuTextureRelease(noVolume);wgpuBufferRelease(noAir);
        if(shadowPipeline)wgpuRenderPipelineRelease(shadowPipeline);if(shadowLayout)wgpuPipelineLayoutRelease(shadowLayout);if(shadowModule)wgpuShaderModuleRelease(shadowModule);
        wgpuBindGroupLayoutRelease(frameLayout);wgpuBindGroupLayoutRelease(objectLayout);
        wgpuSamplerRelease(linear);wgpuTextureViewRelease(white);wgpuTextureRelease(whiteTexture);wgpuTextureViewRelease(black);wgpuTextureRelease(blackTexture);
        if(depthView)wgpuTextureViewRelease(depthView);if(depth)wgpuTextureRelease(depth);if(colourView)wgpuTextureViewRelease(colourView);if(colour)wgpuTextureRelease(colour);
    }
    WGPUBindGroup frameWith(WGPUTextureView shadow,const Atmosphere* a,WGPUTextureView clouds){ // a: ready tables, or null for placeholders.
        WGPUBindGroupEntry e[11]{};e[0].binding=0;e[0].buffer=frameBuffer;e[0].size=512;e[1].binding=1;e[1].textureView=shadow;e[2].binding=2;e[2].sampler=shadowSampler;
        e[3].binding=3;e[3].buffer=a?a->parameters():noAir;e[3].size=Atmosphere::parameterBytes;
        const WGPUTextureView tables[4]{a?a->transmittance():white,a?a->scattering():noVolumeView,a?a->mie():noVolumeView,a?a->irradiance():white};
        for(std::uint32_t i=0;i<4;++i){e[4+i].binding=4+i;e[4+i].textureView=tables[i];}
        e[8].binding=8;e[8].sampler=airSampler;e[9].binding=9;e[9].textureView=clouds;e[10].binding=10;e[10].sampler=cloudSampler;
        WGPUBindGroupDescriptor d{};d.layout=frameLayout;d.entryCount=11;d.entries=e;return wgpuDeviceCreateBindGroup(device,&d);
    }
    void bindFrame(WGPUTextureView shadow,const Atmosphere* a,WGPUTextureView clouds,std::uint64_t cloudSerial){
        if(!casterFrame)casterFrame=frameWith(dummyShadowView,nullptr,black); // The shadow pass cannot sample the map it writes.
        const std::uint64_t key=shadow==dummyShadowView?0:shadowSerial;if(frameGroup&&boundFrame==key&&boundAir==a&&boundClouds==cloudSerial)return;
        if(frameGroup)wgpuBindGroupRelease(frameGroup);boundFrame=key;boundAir=a;boundClouds=cloudSerial;frameGroup=frameWith(shadow,a,clouds);
    }
    WGPURenderPipeline shadowCaster(){ // Depth only, positions through the sun's view-projection.
        if(shadowPipeline)return shadowPipeline;
        const std::string code=std::string(prelude)+"@vertex fn vs_main(@location(0) position: vec3f) -> @builtin(position) vec4f { return frame.shadowViewProj * object.model * vec4f(position, 1.0); }\n";
        WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code=sv(code);WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;shadowModule=wgpuDeviceCreateShaderModule(device,&md);
        WGPUBindGroupLayout layouts[]{frameLayout,objectLayout};WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=2;pd.bindGroupLayouts=layouts;shadowLayout=wgpuDeviceCreatePipelineLayout(device,&pd);
        WGPUVertexAttribute a{};a.format=WGPUVertexFormat_Float32x3;WGPUVertexBufferLayout b{};b.arrayStride=12;b.stepMode=WGPUVertexStepMode_Vertex;b.attributeCount=1;b.attributes=&a;
        WGPUDepthStencilState ds{};ds.format=WGPUTextureFormat_Depth32Float;ds.depthWriteEnabled=WGPUOptionalBool_True;ds.depthCompare=WGPUCompareFunction_Less;
        ds.stencilFront.compare=ds.stencilBack.compare=WGPUCompareFunction_Always;ds.stencilFront.failOp=ds.stencilFront.depthFailOp=ds.stencilFront.passOp=WGPUStencilOperation_Keep;ds.stencilBack=ds.stencilFront;
        WGPURenderPipelineDescriptor d{};d.label=sv("albert:shadow");d.layout=shadowLayout;d.vertex.module=shadowModule;d.vertex.entryPoint=sv("vs_main");d.vertex.bufferCount=1;d.vertex.buffers=&b;
        d.primitive.topology=WGPUPrimitiveTopology_TriangleList;d.primitive.cullMode=WGPUCullMode_None;d.depthStencil=&ds;d.multisample.count=1;d.multisample.mask=~0u;
        return shadowPipeline=wgpuDeviceCreateRenderPipeline(device,&d);
    }
    WGPUBuffer buffer(std::size_t bytes,WGPUBufferUsage usage){WGPUBufferDescriptor d{};d.size=align(std::max<std::size_t>(bytes,16),4);d.usage=usage|WGPUBufferUsage_CopyDst;return wgpuDeviceCreateBuffer(device,&d);}
    void release(Geometry& g){if(g.index.buffer)wgpuBufferRelease(g.index.buffer);}
    void release(Attribute& a){if(a.buffer.buffer)wgpuBufferRelease(a.buffer.buffer);}
    // The GPU copy of a float attribute, one per threepp attribute however many geometries share it
    // (the border chunks share theirs and differ in draw range); nullptr when absent or of another size.
    WGPUBuffer vertices(BufferGeometry& geometry,const std::string& name,int components){
        const auto& all=geometry.getAttributes();const auto it=all.find(name);if(it==all.end())return nullptr;
        const auto* a=dynamic_cast<const TypedBufferAttribute<float>*>(it->second.get());if(!a||a->itemSize()!=components)return nullptr;
        auto& slot=attributes[it->second.get()];
        if(slot.owner.lock()!=it->second){release(slot);slot=Attribute{};slot.owner=it->second;} // New, or another attribute at a reused address.
        slot.used=frame;upload(slot.buffer,a->array().data(),a->array().size()*4,WGPUBufferUsage_Vertex,a->version);return slot.buffer.buffer;
    }
    void release(Texture& t){if(t.view)wgpuTextureViewRelease(t.view);if(t.texture)wgpuTextureRelease(t.texture);if(t.sampler)wgpuSamplerRelease(t.sampler);if(t.pending)wgpuTextureRelease(t.pending);}
    void release(MaterialState& m){if(m.uniforms.buffer)wgpuBufferRelease(m.uniforms.buffer);if(m.group)wgpuBindGroupRelease(m.group);}
    void report(const std::string& what){if(reported.insert(what).second)std::fprintf(stderr,"WebGPU renderer: %s\n",what.c_str());}
    void upload(Buffer& b,const void* data,std::size_t bytes,WGPUBufferUsage usage,unsigned version){
        if(b.version==version&&b.buffer)return;
        if(!b.buffer||b.bytes<bytes){if(b.buffer)wgpuBufferRelease(b.buffer);b.buffer=buffer(bytes,usage);b.bytes=align(std::max<std::size_t>(bytes,16),4);++reallocations;}
        if(bytes%4==0)wgpuQueueWriteBuffer(queue,b.buffer,0,data,bytes);
        else{std::vector<std::byte> padded(align(bytes,4));std::memcpy(padded.data(),data,bytes);wgpuQueueWriteBuffer(queue,b.buffer,0,padded.data(),padded.size());}
        b.version=version;
    }
    const Compiled* program(const std::string& name){
        if(const auto it=compiled.find(name);it!=compiled.end())return &it->second;
        const Program* p=gpu::program(name);if(!p){report("no WebGPU program for material '"+name+"'");return nullptr;}
        Compiled c;c.program=*p;const std::string source=std::string(prelude)+atmosphereWgsl+gpu::air+lighting+p->wgsl;
        WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code=sv(source);WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;md.label=sv(name);
        c.module=wgpuDeviceCreateShaderModule(device,&md);
        std::vector<WGPUBindGroupLayoutEntry> entries(1+2*p->textures.size());
        entries[0].binding=0;entries[0].visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;entries[0].buffer.type=WGPUBufferBindingType_Uniform;
        for(std::size_t i=0;i<p->textures.size();++i){
            auto& t=entries[1+2*i];t.binding=static_cast<std::uint32_t>(1+2*i);t.visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;
            t.texture.sampleType=WGPUTextureSampleType_Float;t.texture.viewDimension=WGPUTextureViewDimension_2D;
            auto& s=entries[2+2*i];s.binding=static_cast<std::uint32_t>(2+2*i);s.visibility=t.visibility;s.sampler.type=WGPUSamplerBindingType_Filtering;
        }
        WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=entries.size();ld.entries=entries.data();c.layout=wgpuDeviceCreateBindGroupLayout(device,&ld);
        WGPUBindGroupLayout layouts[]{frameLayout,objectLayout,c.layout};WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=3;pd.bindGroupLayouts=layouts;
        c.pipelineLayout=wgpuDeviceCreatePipelineLayout(device,&pd);
        return &compiled.emplace(name,std::move(c)).first->second;
    }
    std::unordered_map<int,WGPURenderPipeline> mipPipelines;WGPUBindGroupLayout mipLayout=nullptr;WGPUPipelineLayout mipPipelineLayout=nullptr;WGPUShaderModule mipModule=nullptr;
    unsigned reallocations=0; // Buffers created by upload(): a new one may reuse a released one's address.
    static constexpr std::size_t streamLimit=8u<<20;std::size_t streamBudget=0; // Bytes a frame for streamed updates; still allowed this frame.
    // Box-filtered mip chain rendered level by level (a full-screen triangle sampling the level above).
    void mipmaps(WGPUTexture texture,WGPUTextureFormat format,unsigned levels){
        if(levels<2)return;
        if(!mipModule){
            const char* code=R"(
@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
struct V { @builtin(position) p: vec4f, @location(0) uv: vec2f };
@vertex fn vs_main(@builtin(vertex_index) i: u32) -> V { let c = vec2f(f32((i << 1u) & 2u), f32(i & 2u)); var v: V; v.p = vec4f(c * 2.0 - 1.0, 0.0, 1.0); v.uv = vec2f(c.x, 1.0 - c.y); return v; }
@fragment fn fs_main(v: V) -> @location(0) vec4f { return textureSampleLevel(source, smp, v.uv, 0.0); }
)";
            WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code=sv(code);WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;mipModule=wgpuDeviceCreateShaderModule(device,&md);
            WGPUBindGroupLayoutEntry e[2]{};e[0].binding=0;e[0].visibility=WGPUShaderStage_Fragment;e[0].texture.sampleType=WGPUTextureSampleType_Float;e[0].texture.viewDimension=WGPUTextureViewDimension_2D;
            e[1].binding=1;e[1].visibility=WGPUShaderStage_Fragment;e[1].sampler.type=WGPUSamplerBindingType_Filtering;
            WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=2;ld.entries=e;mipLayout=wgpuDeviceCreateBindGroupLayout(device,&ld);
            WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=1;pd.bindGroupLayouts=&mipLayout;mipPipelineLayout=wgpuDeviceCreatePipelineLayout(device,&pd);
        }
        auto& pipe=mipPipelines[static_cast<int>(format)];
        if(!pipe){
            WGPUColorTargetState t{};t.format=format;t.writeMask=WGPUColorWriteMask_All;WGPUFragmentState f{};f.module=mipModule;f.entryPoint=sv("fs_main");f.targetCount=1;f.targets=&t;
            WGPURenderPipelineDescriptor d{};d.layout=mipPipelineLayout;d.vertex.module=mipModule;d.vertex.entryPoint=sv("vs_main");d.primitive.topology=WGPUPrimitiveTopology_TriangleList;
            d.multisample.count=1;d.multisample.mask=~0u;d.fragment=&f;pipe=wgpuDeviceCreateRenderPipeline(device,&d);
        }
        WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(device,nullptr);
        for(unsigned level=1;level<levels;++level){
            WGPUTextureViewDescriptor vd{};vd.format=format;vd.dimension=WGPUTextureViewDimension_2D;vd.baseMipLevel=level-1;vd.mipLevelCount=1;vd.arrayLayerCount=1;vd.aspect=WGPUTextureAspect_All;
            WGPUTextureView src=wgpuTextureCreateView(texture,&vd);vd.baseMipLevel=level;WGPUTextureView dst=wgpuTextureCreateView(texture,&vd);
            WGPUBindGroupEntry be[2]{};be[0].binding=0;be[0].textureView=src;be[1].binding=1;be[1].sampler=linear;
            WGPUBindGroupDescriptor bd{};bd.layout=mipLayout;bd.entryCount=2;bd.entries=be;WGPUBindGroup group=wgpuDeviceCreateBindGroup(device,&bd);
            WGPURenderPassColorAttachment c{};c.view=dst;c.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;c.loadOp=WGPULoadOp_Clear;c.storeOp=WGPUStoreOp_Store;
            WGPURenderPassDescriptor rp{};rp.colorAttachmentCount=1;rp.colorAttachments=&c;WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&rp);
            wgpuRenderPassEncoderSetPipeline(pass,pipe);wgpuRenderPassEncoderSetBindGroup(pass,0,group,0,nullptr);wgpuRenderPassEncoderDraw(pass,3,1,0,0);wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);wgpuBindGroupRelease(group);wgpuTextureViewRelease(src);wgpuTextureViewRelease(dst);
        }
        WGPUCommandBuffer commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(queue,1,&commands);wgpuCommandBufferRelease(commands);wgpuCommandEncoderRelease(encoder);
    }
    // RGBA bytes, floats (or half floats without float32 filtering) of a threepp image: the
    // image's own buffer when it already has that layout (Albert's large textures), else a converted
    // copy kept in `out` for the rest of the upload.
    WGPUTextureFormat format(const Image& image)const{return image.isFloat()||image.isHalfFloat()?(floatFilterable?WGPUTextureFormat_RGBA32Float:WGPUTextureFormat_RGBA16Float):WGPUTextureFormat_RGBA8Unorm;}
    const std::byte* rgba(const threepp::Texture& source,std::vector<std::byte>& out){
        const Image& image=source.image();const std::size_t pixels=static_cast<std::size_t>(image.width())*std::max(1u,image.height());
        const int channels=std::max(1,image.channels());const auto f=format(image);
        if(channels==4&&f==WGPUTextureFormat_RGBA32Float&&image.isFloat())return reinterpret_cast<const std::byte*>(image.data<float>().data());
        if(channels==4&&f==WGPUTextureFormat_RGBA16Float&&image.isHalfFloat())return reinterpret_cast<const std::byte*>(image.data<std::uint16_t>().data());
        if(channels==4&&f==WGPUTextureFormat_RGBA8Unorm)return reinterpret_cast<const std::byte*>(image.data<unsigned char>().data());
        if(!out.empty())return out.data();
        auto expand=[&](auto read,auto one){using T=decltype(one);out.resize(pixels*4*sizeof(T));auto* d=reinterpret_cast<T*>(out.data());
            for(std::size_t i=0;i<pixels;++i)for(int c=0;c<4;++c)d[i*4+c]=c<channels?read(i*channels+c):c==3?one:(channels==1?read(i):T(0));};
        auto decoded=[&](std::size_t i)->float{
            if(image.isFloat()){const auto& v=image.data<float>();return i<v.size()?v[i]:0.f;}
            const auto& v=image.data<std::uint16_t>();if(i>=v.size())return 0.f;const unsigned h16=v[i],e=(h16>>10)&31,m=h16&1023;const float sgn=(h16&0x8000)?-1.f:1.f;
            return e==0?sgn*std::ldexp(static_cast<float>(m),-24):e==31?sgn*1e30f:sgn*std::ldexp(static_cast<float>(m|1024),static_cast<int>(e)-25);};
        if(f==WGPUTextureFormat_RGBA32Float)expand(decoded,1.f);
        else if(f==WGPUTextureFormat_RGBA16Float){const std::uint16_t one=toHalf(1.f);expand([&](std::size_t i){return image.isHalfFloat()?(i<image.data<std::uint16_t>().size()?image.data<std::uint16_t>()[i]:std::uint16_t{0}):toHalf(decoded(i));},one);}
        else{const auto& v=image.data<unsigned char>();expand([&](std::size_t i){return i<v.size()?v[i]:static_cast<unsigned char>(0);},static_cast<unsigned char>(255));}
        return out.data();
    }
    // The texture's GPU view and its serial. A first copy, a small image or one of another size (a
    // placeholder being replaced) uploads at once; a new version of a large one streams within the
    // frame budget while the previous copy, a valid stand-in, keeps drawing.
    WGPUTextureView view(threepp::Texture* source,bool blank,std::uint64_t& serial){ // blank: black instead of white when absent.
        serial=blank?2:1;if(!source)return blank?black:white;
        auto& t=textures[source->id];t.used=frame;if(t.view)serial=t.serial;
        if(t.version==source->version()&&t.view)return t.view;
        const Image& image=source->image();const unsigned w=image.width(),h=std::max(1u,image.height());
        if(!w)return t.view?t.view:blank?black:white;
        const WGPUTextureFormat f=format(image);const std::size_t texel=f==WGPUTextureFormat_RGBA32Float?16:f==WGPUTextureFormat_RGBA16Float?8:4;
        if(t.pendingVersion!=source->version()||t.width!=w||t.height!=h||t.texel!=texel){ // Start (or restart: new version or a replaced image) a copy.
            if(t.pending)wgpuTextureRelease(t.pending);t.converted.clear();t.row=0;t.width=w;t.height=h;t.texel=texel;t.pendingVersion=source->version();
            const bool mips=source->generateMipmaps&&source->minFilter!=Filter::Linear&&source->minFilter!=Filter::Nearest;
            t.levels=mips?1+static_cast<unsigned>(std::floor(std::log2(std::max(w,h)))):1;
            WGPUTextureDescriptor d{};d.size={w,h,1};d.format=f;
            d.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst|(t.levels>1?WGPUTextureUsage_RenderAttachment:WGPUTextureUsage_None);
            d.mipLevelCount=t.levels;d.sampleCount=1;d.dimension=WGPUTextureDimension_2D;t.pending=wgpuDeviceCreateTexture(device,&d);
        }
        const std::byte* data=rgba(*source,t.converted);const std::size_t rowBytes=static_cast<std::size_t>(t.width)*t.texel;
        const bool whole=!t.view||rowBytes*t.height<=(4u<<20)||t.shownWidth!=t.width||t.shownHeight!=t.height;
        while(t.row<t.height){
            std::size_t rows=whole?t.height-t.row:std::min<std::size_t>(t.height-t.row,streamBudget/rowBytes);
            if(!rows&&!whole&&streamBudget==streamLimit)rows=1; // A row larger than the whole budget.
            if(!rows)return t.view;
            WGPUTexelCopyTextureInfo dst{};dst.texture=t.pending;dst.origin={0,t.row,0};WGPUTexelCopyBufferLayout l{};l.bytesPerRow=static_cast<std::uint32_t>(rowBytes);l.rowsPerImage=static_cast<std::uint32_t>(rows);
            const WGPUExtent3D e{t.width,static_cast<std::uint32_t>(rows),1};wgpuQueueWriteTexture(queue,&dst,data+rowBytes*t.row,rowBytes*rows,&l,&e);
            t.row+=static_cast<unsigned>(rows);if(!whole)streamBudget-=std::min(streamBudget,rowBytes*rows);
        }
        mipmaps(t.pending,f,t.levels);
        if(t.view)wgpuTextureViewRelease(t.view);if(t.texture)wgpuTextureRelease(t.texture);if(t.sampler)wgpuSamplerRelease(t.sampler);
        t.texture=t.pending;t.pending=nullptr;t.version=t.pendingVersion;t.converted.clear();t.converted.shrink_to_fit();t.view=wgpuTextureCreateView(t.texture,nullptr);serial=t.serial=++serials;t.shownWidth=t.width;t.shownHeight=t.height;
        auto address=[](TextureWrapping m){return m==TextureWrapping::Repeat?WGPUAddressMode_Repeat:m==TextureWrapping::MirroredRepeat?WGPUAddressMode_MirrorRepeat:WGPUAddressMode_ClampToEdge;};
        WGPUSamplerDescriptor s{};s.addressModeU=address(source->wrapS);s.addressModeV=address(source->wrapT);s.addressModeW=WGPUAddressMode_ClampToEdge;
        s.magFilter=source->magFilter!=Filter::Nearest?WGPUFilterMode_Linear:WGPUFilterMode_Nearest;
        s.minFilter=source->minFilter!=Filter::Nearest&&source->minFilter!=Filter::NearestMipmapNearest?WGPUFilterMode_Linear:WGPUFilterMode_Nearest;
        s.mipmapFilter=source->minFilter==Filter::LinearMipmapLinear||source->minFilter==Filter::NearestMipmapLinear?WGPUMipmapFilterMode_Linear:WGPUMipmapFilterMode_Nearest;
        s.lodMaxClamp=static_cast<float>(t.levels);
        s.maxAnisotropy=static_cast<std::uint16_t>(s.magFilter==WGPUFilterMode_Linear&&s.minFilter==WGPUFilterMode_Linear&&s.mipmapFilter==WGPUMipmapFilterMode_Linear?std::clamp(source->anisotropy,1,16):1);
        t.sampler=wgpuDeviceCreateSampler(device,&s);
        return t.view;
    }
    WGPUSampler sampler(threepp::Texture* source){if(!source)return linear;const auto it=textures.find(source->id);return it!=textures.end()&&it->second.sampler?it->second.sampler:linear;}
    // Material uniforms and textures: from the Program's names for ShaderMaterials, or the
    // built-in layout for Basic, Phong and Standard.
    std::string programName(Material& m){
        if(dynamic_cast<ShaderMaterial*>(&m))return m.name;
        if(dynamic_cast<MeshBasicMaterial*>(&m)||dynamic_cast<MeshPhongMaterial*>(&m)||dynamic_cast<MeshStandardMaterial*>(&m)||dynamic_cast<MeshLambertMaterial*>(&m))return "albert:builtin";
        return {};
    }
    WGPUBindGroup materialGroup(Material& m,const Compiled& c){
        auto& state=materials[m.id];
        if(state.used==frame&&state.group&&state.program==m.name)return state.group; // Shared by many meshes (terrain nodes): once a frame.
        state.used=frame;
        std::vector<std::byte> bytes;std::vector<threepp::Texture*> maps;
        if(auto* shader=dynamic_cast<ShaderMaterial*>(&m)){
            std::size_t offset=0;std::vector<std::size_t> at;
            for(const auto& [n,u]:c.program.uniforms){const auto [a,size]=layout(u);offset=align(offset,a);at.push_back(offset);offset+=size;}
            bytes.assign(align(std::max<std::size_t>(offset,16),16),std::byte{});
            for(std::size_t i=0;i<c.program.uniforms.size();++i){const auto it=shader->uniforms.find(c.program.uniforms[i].first);if(it!=shader->uniforms.end())pack(bytes,at[i],c.program.uniforms[i].second,&it->second.value());}
            for(const auto& n:c.program.textures){const auto it=shader->uniforms.find(n);threepp::Texture* t=nullptr;if(it!=shader->uniforms.end())if(auto* p=std::get_if<threepp::Texture*>(&it->second.value()))t=*p;maps.push_back(t);}
        }else{
            float f[24]{1,1,1,m.opacity, 0,0,0,0, 1,0,m.alphaTest,1, 0,0,0,0, 0,0,0,0, 0,0,m.side==Side::Double?1.f:0.f,0};
            if(auto* c=dynamic_cast<MaterialWithColor*>(&m)){f[0]=c->color.r;f[1]=c->color.g;f[2]=c->color.b;}
            if(auto* e=dynamic_cast<MaterialWithEmissive*>(&m)){f[4]=e->emissive.r*e->emissiveIntensity;f[5]=e->emissive.g*e->emissiveIntensity;f[6]=e->emissive.b*e->emissiveIntensity;}
            if(auto* s=dynamic_cast<MeshStandardMaterial*>(&m)){f[8]=s->roughness;f[9]=s->metalness;f[13]=2;}
            else if(auto* p=dynamic_cast<MeshPhongMaterial*>(&m)){f[9]=p->shininess;f[13]=1;f[16]=p->specular.r;f[17]=p->specular.g;f[18]=p->specular.b;}
            else if(dynamic_cast<MeshLambertMaterial*>(&m)){f[13]=3;}
            if(auto* fl=dynamic_cast<MaterialWithFlatShading*>(&m))f[23]=fl->flatShading?1.f:0.f;
            threepp::Texture *map=nullptr,*alpha=nullptr,*light=nullptr,*bump=nullptr,*rough=nullptr;
            if(auto* w=dynamic_cast<MaterialWithMap*>(&m))map=w->map.get();
            if(auto* w=dynamic_cast<MaterialWithAlphaMap*>(&m))alpha=w->alphaMap.get();
            if(auto* w=dynamic_cast<MaterialWithLightMap*>(&m)){light=w->lightMap.get();f[11]=w->lightMapIntensity;}
            if(auto* w=dynamic_cast<MaterialWithBumpMap*>(&m)){bump=w->bumpMap.get();f[19]=w->bumpScale;f[7]=bump&&bump->wrapS==TextureWrapping::Repeat?1.f:0.f;}
            if(auto* w=dynamic_cast<MaterialWithRoughness*>(&m))rough=w->roughnessMap.get();
            f[12]=map&&map->colorSpace==ColorSpace::sRGB?1.f:0.f;f[14]=alpha?1.f:0.f;f[15]=light?1.f:0.f;f[20]=bump?1.f:0.f;f[21]=rough?1.f:0.f;
            bytes.resize(sizeof f);std::memcpy(bytes.data(),f,sizeof f);maps={map,alpha,light,bump,rough};
        }
        if(!state.uniforms.buffer||state.uniforms.bytes<bytes.size()){if(state.uniforms.buffer)wgpuBufferRelease(state.uniforms.buffer);state.uniforms.buffer=buffer(bytes.size(),WGPUBufferUsage_Uniform);state.uniforms.bytes=bytes.size();if(state.group){wgpuBindGroupRelease(state.group);state.group=nullptr;}}
        wgpuQueueWriteBuffer(queue,state.uniforms.buffer,0,bytes.data(),bytes.size());
        const bool shader=dynamic_cast<ShaderMaterial*>(&m)!=nullptr;std::vector<WGPUTextureView> views;std::vector<std::uint64_t> serials(maps.size());
        for(std::size_t i=0;i<maps.size();++i)views.push_back(view(maps[i],shader||i>0,serials[i]));
        if(state.group&&serials==state.serials&&state.program==m.name)return state.group;
        if(state.group)wgpuBindGroupRelease(state.group);state.serials=serials;state.program=m.name;
        std::vector<WGPUBindGroupEntry> e(1+2*maps.size());e[0].binding=0;e[0].buffer=state.uniforms.buffer;e[0].size=state.uniforms.bytes;
        for(std::size_t i=0;i<maps.size();++i){e[1+2*i].binding=static_cast<std::uint32_t>(1+2*i);e[1+2*i].textureView=views[i];e[2+2*i].binding=static_cast<std::uint32_t>(2+2*i);e[2+2*i].sampler=sampler(maps[i]);}
        WGPUBindGroupDescriptor d{};d.layout=c.layout;d.entryCount=e.size();d.entries=e.data();state.group=wgpuDeviceCreateBindGroup(device,&d);
        return state.group;
    }
    WGPURenderPipeline pipeline(const std::string& name,const Compiled& c,const Material& m,const std::vector<int>& strides,unsigned samples){
        WGPUBlendState b{};const bool blended=blend(m,b);
        const auto cull=m.side==Side::Double?WGPUCullMode_None:m.side==Side::Back?WGPUCullMode_Front:WGPUCullMode_Back;
        // Program, blend and depth state, polygon offsets, strides: numbers, not a string per draw.
        std::array<std::int64_t,24> key{};key.fill(-1);std::size_t at=0;key[at++]=reinterpret_cast<std::intptr_t>(&c);
        for(int v:{blended?1:0,static_cast<int>(b.color.srcFactor),static_cast<int>(b.color.dstFactor),static_cast<int>(b.color.operation),static_cast<int>(b.alpha.srcFactor),static_cast<int>(b.alpha.dstFactor),
            static_cast<int>(b.alpha.operation),m.depthTest?1:0,m.depthWrite?1:0,static_cast<int>(cull),m.colorWrite?1:0,static_cast<int>(samples)})key[at++]=v;
        const float offsets[2]{m.polygonOffset?m.polygonOffsetFactor:0.f,m.polygonOffset?m.polygonOffsetUnits:0.f};for(float o:offsets){std::int32_t bits;std::memcpy(&bits,&o,4);key[at++]=bits;}
        if(strides.size()>key.size()-at){report("too many vertex buffers for '"+name+"'");return nullptr;}
        for(int s:strides)key[at++]=s;
        if(const auto it=pipelines.find(key);it!=pipelines.end())return it->second;
        std::vector<WGPUVertexAttribute> attributes(1+c.program.attributes.size());std::vector<WGPUVertexBufferLayout> buffers(attributes.size());
        const auto step=c.program.points?WGPUVertexStepMode_Instance:WGPUVertexStepMode_Vertex;
        auto format=[](int n){return n==1?WGPUVertexFormat_Float32:n==2?WGPUVertexFormat_Float32x2:n==3?WGPUVertexFormat_Float32x3:WGPUVertexFormat_Float32x4;};
        for(std::size_t i=0;i<attributes.size();++i){
            const int n=i?c.program.attributes[i-1].second:3;attributes[i].format=format(n);attributes[i].offset=0;attributes[i].shaderLocation=static_cast<std::uint32_t>(i);
            buffers[i].arrayStride=static_cast<std::uint64_t>(strides[i]);buffers[i].stepMode=step;buffers[i].attributeCount=1;buffers[i].attributes=&attributes[i];
        }
        WGPUColorTargetState target{};target.format=gpu.format();target.blend=blended?&b:nullptr;target.writeMask=m.colorWrite?WGPUColorWriteMask_All:WGPUColorWriteMask_None;
        WGPUFragmentState f{};f.module=c.module;f.entryPoint=sv("fs_main");f.targetCount=1;f.targets=&target;
        WGPUDepthStencilState ds{};ds.format=WGPUTextureFormat_Depth32Float;ds.depthWriteEnabled=m.depthWrite?WGPUOptionalBool_True:WGPUOptionalBool_False;
        ds.depthCompare=m.depthTest&&!c.program.background?WGPUCompareFunction_GreaterEqual:WGPUCompareFunction_Always; // Reversed Z.
        if(c.program.background)ds.depthWriteEnabled=WGPUOptionalBool_False;
        ds.stencilFront.compare=ds.stencilBack.compare=WGPUCompareFunction_Always;ds.stencilFront.failOp=ds.stencilFront.depthFailOp=ds.stencilFront.passOp=WGPUStencilOperation_Keep;ds.stencilBack=ds.stencilFront;
        if(m.polygonOffset){ds.depthBias=static_cast<std::int32_t>(-m.polygonOffsetUnits);ds.depthBiasSlopeScale=-m.polygonOffsetFactor;} // Toward the camera, as GL's negative offsets.
        WGPURenderPipelineDescriptor d{};d.label=sv(name);d.layout=c.pipelineLayout;d.vertex.module=c.module;d.vertex.entryPoint=sv("vs_main");d.vertex.bufferCount=buffers.size();d.vertex.buffers=buffers.data();
        d.primitive.topology=WGPUPrimitiveTopology_TriangleList;d.primitive.frontFace=WGPUFrontFace_CCW;d.primitive.cullMode=c.program.points?WGPUCullMode_None:c.program.background?WGPUCullMode_Front:cull;
        d.depthStencil=&ds;d.multisample.count=samples;d.multisample.mask=~0u;d.fragment=&f;
        return pipelines.emplace(key,wgpuDeviceCreateRenderPipeline(device,&d)).first->second;
    }
    void ensureDepth(unsigned w,unsigned h,unsigned samples){
        if(depth&&depthWidth==w&&depthHeight==h&&depthSamples==samples)return;
        if(depthView)wgpuTextureViewRelease(depthView);if(depth)wgpuTextureRelease(depth);if(colourView)wgpuTextureViewRelease(colourView);if(colour)wgpuTextureRelease(colour);colour=nullptr;colourView=nullptr;
        WGPUTextureDescriptor d{};d.size={w,h,1};d.format=WGPUTextureFormat_Depth32Float;d.usage=WGPUTextureUsage_RenderAttachment;d.mipLevelCount=1;d.sampleCount=samples;d.dimension=WGPUTextureDimension_2D;
        depth=wgpuDeviceCreateTexture(device,&d);depthView=wgpuTextureCreateView(depth,nullptr);depthWidth=w;depthHeight=h;depthSamples=samples;
        if(samples>1){d.format=gpu.format();colour=wgpuDeviceCreateTexture(device,&d);colourView=wgpuTextureCreateView(colour,nullptr);}
    }
    void evict(){
        for(auto it=geometries.begin();it!=geometries.end();)if(frame-it->second.used>evictAfter){release(it->second);it=geometries.erase(it);}else ++it;
        for(auto it=attributes.begin();it!=attributes.end();)if(frame-it->second.used>evictAfter||it->second.owner.expired()){release(it->second);it=attributes.erase(it);}else ++it;
        for(auto it=textures.begin();it!=textures.end();)if(frame-it->second.used>evictAfter){release(it->second);it=textures.erase(it);}else ++it;
        for(auto it=materials.begin();it!=materials.end();)if(frame-it->second.used>evictAfter){release(it->second);it=materials.erase(it);}else ++it;
    }
};
SceneRenderer::SceneRenderer(Context& context):impl_(std::make_unique<Impl>(context)){}
SceneRenderer::~SceneRenderer()=default;
unsigned SceneRenderer::drawn()const{return impl_->drawn;}
std::size_t SceneRenderer::vertexBytes()const{std::size_t n=0;for(const auto& [k,a]:impl_->attributes)n+=a.buffer.bytes;return n;}
double SceneRenderer::gpuMilliseconds()const{return impl_->gpuMs;}
void SceneRenderer::submitted(){auto& g=*impl_;if(g.mapPending){wgpuBufferMapAsync(g.readback,WGPUMapMode_Read,0,16,g.pendingMap);g.mapPending=false;}}
void SceneRenderer::render(Scene& scene,Camera& camera,WGPUTextureView target,unsigned width,unsigned height,WGPUCommandEncoder encoder){
    auto& g=*impl_;++g.frame;g.drawn=0;const unsigned msaa=samples>1?4u:1u;g.ensureDepth(width,height,msaa);g.streamBudget=Impl::streamLimit;
    scene.updateMatrixWorld();if(!camera.parent)camera.updateMatrixWorld();
    // Reversed-Z infinite perspective: depth = near / -z, 1 at the near plane, 0 at infinity.
    // Orthographic: depth = (far + z) / (far - near), 1 at the near plane and 0 at the far one.
    Matrix4 projection;auto& pe=projection.elements;pe.fill(0);
    if(const auto* o=dynamic_cast<OrthographicCamera*>(&camera)){
        const float zoom=std::max(o->zoom,1e-12f),cx=(o->left+o->right)/2,cy=(o->top+o->bottom)/2,dx=(o->right-o->left)/(2*zoom),dy=(o->top-o->bottom)/(2*zoom);
        const float l=cx-dx,r=cx+dx,t=cy+dy,b=cy-dy,n=o->nearPlane,f=o->farPlane;
        pe[0]=2/(r-l);pe[5]=2/(t-b);pe[10]=1/(f-n);pe[12]=-(r+l)/(r-l);pe[13]=-(t+b)/(t-b);pe[14]=f/(f-n);pe[15]=1;
    }else{const auto* perspective=dynamic_cast<PerspectiveCamera*>(&camera);
        const float f=perspective?1.f/std::tan(perspective->fov*3.14159265358979f/360.f):1.f,aspect=perspective?perspective->aspect:1.f,n=camera.nearPlane;
        pe[0]=f/aspect;pe[5]=f;pe[11]=-1;pe[14]=n;}
    Frustum frustum;Matrix4 screen;screen.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse);frustum.setFromProjectionMatrix(screen);
    struct Item {Object3D* object;Material* material;BufferGeometry* geometry;const Impl::Compiled* program;float z;int order;bool points;};
    std::vector<Item> background,opaque,blended;Vector3 sun;float sunOn=0;Color sunColor(0,0,0),ambient(0,0,0);
    DirectionalLight* shadowLight=nullptr;std::vector<Mesh*> casters;
    scene.traverseVisible([&](Object3D& o){
        if(auto* light=dynamic_cast<DirectionalLight*>(&o)){Vector3 p,t;p.setFromMatrixPosition(*light->matrixWorld);t.setFromMatrixPosition(*light->target().matrixWorld);sun=p.sub(t).normalize();sunOn=1;sunColor=light->color;sunColor.multiplyScalar(light->intensity);
            if(shadows&&light->castShadow&&light->shadow)shadowLight=light;return;}
        if(auto* mesh=dynamic_cast<Mesh*>(&o);mesh&&o.castShadow&&mesh->material()&&mesh->material()->visible&&mesh->geometry())casters.push_back(mesh);
        if(auto* light=dynamic_cast<AmbientLight*>(&o)){Color c=light->color;c.multiplyScalar(light->intensity);ambient.add(c);return;}
        const bool points=dynamic_cast<Points*>(&o)!=nullptr;if(!points&&!dynamic_cast<Mesh*>(&o))return;
        auto geometry=o.geometry();auto material=o.material();if(!geometry||!material||!material->visible)return;
        if(o.frustumCulled){if(!geometry->boundingSphere)geometry->computeBoundingSphere();Sphere s=*geometry->boundingSphere;s.applyMatrix4(*o.matrixWorld);if(!frustum.intersectsSphere(s))return;}
        const std::string name=g.programName(*material);if(name.empty()){g.report("unsupported material type on '"+o.name+"'");return;}
        const auto* program=g.program(name);if(!program)return;
        Vector3 p;p.setFromMatrixPosition(*o.matrixWorld);p.applyMatrix4(screen);
        (program->program.background?background:material->transparent?blended:opaque).push_back({&o,material.get(),geometry.get(),program,p.z,o.renderOrder,points});
    });
    std::stable_sort(background.begin(),background.end(),[](const Item& a,const Item& b){return a.order<b.order;});
    std::stable_sort(opaque.begin(),opaque.end(),[](const Item& a,const Item& b){return a.order!=b.order?a.order<b.order:a.z<b.z;});
    std::stable_sort(blended.begin(),blended.end(),[](const Item& a,const Item& b){return a.order!=b.order?a.order<b.order:a.z>b.z;});
    // Frame uniforms.
    float f[128]{};std::memcpy(f,camera.matrixWorldInverse.elements.data(),64);std::memcpy(f+16,projection.elements.data(),64);
    const bool shadowing=shadowLight&&!casters.empty()&&dynamic_cast<OrthographicCamera*>(shadowLight->shadow->camera.get());
    if(shadowing){ // threepp places the light camera; the projection maps its depth to 0-1 for WebGPU.
        auto& sh=*shadowLight->shadow;sh.updateMatrices(*shadowLight);const auto& oc=static_cast<OrthographicCamera&>(*sh.camera);
        Matrix4 p;auto& e=p.elements;e.fill(0);e[0]=2/(oc.right-oc.left);e[5]=2/(oc.top-oc.bottom);e[10]=-1/(oc.farPlane-oc.nearPlane);
        e[12]=-(oc.right+oc.left)/(oc.right-oc.left);e[13]=-(oc.top+oc.bottom)/(oc.top-oc.bottom);e[14]=-oc.nearPlane/(oc.farPlane-oc.nearPlane);e[15]=1;
        Matrix4 vp;vp.multiplyMatrices(p,oc.matrixWorldInverse);std::memcpy(f+56,vp.elements.data(),64);
        const unsigned size=static_cast<unsigned>(std::clamp(sh.mapSize.x,256.f,4096.f));
        if(size!=g.shadowSize){if(g.shadowView)wgpuTextureViewRelease(g.shadowView);if(g.shadowTexture)wgpuTextureRelease(g.shadowTexture);
            WGPUTextureDescriptor d{};d.size={size,size,1};d.format=WGPUTextureFormat_Depth32Float;d.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_TextureBinding;d.mipLevelCount=1;d.sampleCount=1;d.dimension=WGPUTextureDimension_2D;
            g.shadowTexture=wgpuDeviceCreateTexture(g.device,&d);g.shadowView=wgpuTextureCreateView(g.shadowTexture,nullptr);g.shadowSize=size;g.shadowSerial=++g.serials;}
        f[72]=1;f[73]=sh.bias;f[74]=sh.normalBias;f[75]=1.f/static_cast<float>(size);
    }
    // Air: the precomputation first (its last commands precede this frame's pass), then the planet frame.
    const Atmosphere* airTables=nullptr;
    if(air.atmosphere&&air.body&&air.strength>0&&air.atmosphere->advance(encoder,airSteps)){
        airTables=air.atmosphere;Matrix4 planet;planet.copy(*air.body->matrixWorld).invert();Matrix4 scale;scale.makeScale(air.kmPerUnit,air.kmPerUnit,air.kmPerUnit);planet.premultiply(scale);
        std::memcpy(f+76,planet.elements.data(),64);Vector3 eye;eye.setFromMatrixPosition(*camera.matrixWorld);eye.applyMatrix4(planet);
        const float ground=std::min(air.groundKm,0.f);eye.multiplyScalar(1-ground/std::max(eye.length(),1.f));f[99]=ground; // Radii shifted by -ground (airPoint too).
        Vector3 toward=sun;toward.transformDirection(planet); // Linear part, normalised: the ellipsoid maps to the model's sphere.
        f[92]=eye.x;f[93]=eye.y;f[94]=eye.z;f[95]=std::min(air.strength,1.f);f[96]=toward.x;f[97]=toward.y;f[98]=toward.z;
    }
    // Cloud shadows: the layer's map (its GPU copy is the cloud material's), its height and opacity.
    std::uint64_t cloudSerial=2;WGPUTextureView cloudView=g.black;
    if(airTables&&air.clouds&&air.cloudShadow>0){cloudView=g.view(air.clouds,true,cloudSerial);f[100]=air.cloudKm;f[101]=std::min(air.cloudShadow,1.f);}
    if(airTables)f[102]=std::max(air.surfaceWind,0.f); // The sea's roughness (Cox-Munk).
    g.bindFrame(shadowing?g.shadowView:g.dummyShadowView,airTables,cloudView,cloudSerial);
    Vector3 eye;eye.setFromMatrixPosition(*camera.matrixWorld);f[32]=eye.x;f[33]=eye.y;f[34]=eye.z;f[35]=1;
    f[36]=static_cast<float>(width);f[37]=static_cast<float>(height);f[38]=camera.nearPlane;
    f[40]=sun.x;f[41]=sun.y;f[42]=sun.z;f[43]=sunOn;f[44]=sunColor.r;f[45]=sunColor.g;f[46]=sunColor.b;f[48]=ambient.r;f[49]=ambient.g;f[50]=ambient.b;f[52]=exposure;f[53]=toneMapping?1.f:0.f;
    wgpuQueueWriteBuffer(g.queue,g.frameBuffer,0,f,sizeof f);
    // Object uniforms, one 256-byte slot per draw, then one per shadow caster.
    const std::size_t count=background.size()+opaque.size()+blended.size()+(shadowing?casters.size():0);
    if(count*objectStride>g.objectCapacity){
        if(g.objectBuffer)wgpuBufferRelease(g.objectBuffer);if(g.objectGroup)wgpuBindGroupRelease(g.objectGroup);
        g.objectCapacity=std::max<std::size_t>(count*objectStride*2,objectStride*64);g.objectBuffer=g.buffer(g.objectCapacity,WGPUBufferUsage_Uniform);
        WGPUBindGroupEntry e{};e.binding=0;e.buffer=g.objectBuffer;e.size=240;WGPUBindGroupDescriptor d{};d.layout=g.objectLayout;d.entryCount=1;d.entries=&e;g.objectGroup=wgpuDeviceCreateBindGroup(g.device,&d);
    }
    std::vector<std::byte> objects(std::max<std::size_t>(count,1)*objectStride);std::size_t slot=0;
    auto writeObject=[&](Object3D& o){
        float* p=reinterpret_cast<float*>(objects.data()+slot*objectStride);std::memcpy(p,o.matrixWorld->elements.data(),64);
        Matrix4 mv;mv.multiplyMatrices(camera.matrixWorldInverse,*o.matrixWorld);std::memcpy(p+16,mv.elements.data(),64);
        Matrix3 nw;nw.getNormalMatrix(*o.matrixWorld);Matrix3 nv;nv.getNormalMatrix(mv);
        for(int c=0;c<3;++c){std::memcpy(p+32+4*c,nw.elements.data()+3*c,12);std::memcpy(p+44+4*c,nv.elements.data()+3*c,12);}
        p[56]=o.receiveShadow?1.f:0.f;
        return static_cast<std::uint32_t>(slot++*objectStride);
    };
    std::vector<std::uint32_t> offsets;for(auto* list:{&background,&opaque,&blended})for(auto& i:*list)offsets.push_back(writeObject(*i.object));
    std::vector<std::uint32_t> casterOffsets;if(shadowing)for(auto* m:casters)casterOffsets.push_back(writeObject(*m));
    if(count)wgpuQueueWriteBuffer(g.queue,g.objectBuffer,0,objects.data(),count*objectStride);
    if(shadowing){ // Sun depth from the casters' positions (geometry buffers shared with the main pass).
        WGPURenderPassDepthStencilAttachment sd{};sd.view=g.shadowView;sd.depthLoadOp=WGPULoadOp_Clear;sd.depthStoreOp=WGPUStoreOp_Store;sd.depthClearValue=1;
        WGPURenderPassDescriptor spd{};spd.depthStencilAttachment=&sd;WGPURenderPassEncoder sp=wgpuCommandEncoderBeginRenderPass(encoder,&spd);
        wgpuRenderPassEncoderSetPipeline(sp,g.shadowCaster());wgpuRenderPassEncoderSetBindGroup(sp,0,g.casterFrame,0,nullptr);
        for(std::size_t i=0;i<casters.size();++i){
            auto* geometry=casters[i]->geometry().get();auto* positions=geometry->getAttribute<float>("position");WGPUBuffer b=g.vertices(*geometry,"position",3);if(!b)continue;
            auto& geo=g.geometries[geometry->id];geo.used=g.frame;
            wgpuRenderPassEncoderSetBindGroup(sp,1,g.objectGroup,1,&casterOffsets[i]);wgpuRenderPassEncoderSetVertexBuffer(sp,0,b,0,WGPU_WHOLE_SIZE);
            if(auto* idx=geometry->getIndex()){g.upload(geo.index,idx->array().data(),idx->array().size()*4,WGPUBufferUsage_Index,idx->version);
                wgpuRenderPassEncoderSetIndexBuffer(sp,geo.index.buffer,WGPUIndexFormat_Uint32,0,WGPU_WHOLE_SIZE);wgpuRenderPassEncoderDrawIndexed(sp,static_cast<std::uint32_t>(idx->array().size()),1,0,0,0);}
            else wgpuRenderPassEncoderDraw(sp,static_cast<std::uint32_t>(positions->count()),1,0,0);
        }
        wgpuRenderPassEncoderEnd(sp);wgpuRenderPassEncoderRelease(sp);
    }
    // Pass.
    WGPURenderPassColorAttachment colour{};colour.view=msaa>1?g.colourView:target;colour.resolveTarget=msaa>1?target:nullptr;colour.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;colour.loadOp=WGPULoadOp_Clear;
    colour.storeOp=msaa>1?WGPUStoreOp_Discard:WGPUStoreOp_Store;
    if(scene.background.isColor()){const Color c=scene.background.color();colour.clearValue={c.r,c.g,c.b,1};}else colour.clearValue={0,0,0,1};
    WGPURenderPassDepthStencilAttachment depth{};depth.view=g.depthView;depth.depthLoadOp=WGPULoadOp_Clear;depth.depthStoreOp=WGPUStoreOp_Discard;depth.depthClearValue=0;
    WGPURenderPassDescriptor pd{};pd.colorAttachmentCount=1;pd.colorAttachments=&colour;pd.depthStencilAttachment=&depth;
    WGPUPassTimestampWrites times{};const bool timing=g.timestamps&&!g.mapping;
    if(timing){times.querySet=g.timestamps;times.beginningOfPassWriteIndex=0;times.endOfPassWriteIndex=1;pd.timestampWrites=&times;}
    WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&pd);
    wgpuRenderPassEncoderSetBindGroup(pass,0,g.frameGroup,0,nullptr);
    std::size_t index=0;
    // State already set by the previous draw is not set again (terrain nodes share almost all of it).
    WGPURenderPipeline lastPipe=nullptr;WGPUBindGroup lastGroup=nullptr;WGPUBuffer lastIndex=nullptr;std::array<WGPUBuffer,8> lastBuffers{};
    std::vector<WGPUBuffer> buffers;std::vector<int> strides;const std::string position="position";unsigned seenReallocations=g.reallocations;
    for(auto* list:{&background,&opaque,&blended})for(auto& item:*list){
        const std::uint32_t offset=offsets[index++];auto& geo=g.geometries[item.geometry->id];geo.used=g.frame;
        const auto* positions=item.geometry->getAttribute<float>("position");if(!positions)continue;
        const unsigned vertices=static_cast<unsigned>(positions->count());
        buffers.clear();strides.clear();
        auto bind=[&](const std::string& name,int components){
            if(WGPUBuffer b=g.vertices(*item.geometry,name,components)){buffers.push_back(b);strides.push_back(components*4);}else{buffers.push_back(g.zeros);strides.push_back(0);}};
        bind(position,3);for(const auto& [n,c]:item.program->program.attributes)bind(n,c);
        WGPURenderPipeline pipe=g.pipeline(g.programName(*item.material),*item.program,*item.material,strides,msaa);if(!pipe||buffers.size()>lastBuffers.size())continue;
        WGPUBindGroup group=g.materialGroup(*item.material,*item.program);
        if(g.reallocations!=seenReallocations){lastIndex=nullptr;lastBuffers.fill(nullptr);seenReallocations=g.reallocations;}
        if(pipe!=lastPipe){wgpuRenderPassEncoderSetPipeline(pass,pipe);lastPipe=pipe;}
        wgpuRenderPassEncoderSetBindGroup(pass,1,g.objectGroup,1,&offset);
        if(group!=lastGroup){wgpuRenderPassEncoderSetBindGroup(pass,2,group,0,nullptr);lastGroup=group;}
        for(std::size_t i=0;i<buffers.size();++i)if(buffers[i]!=lastBuffers[i]){wgpuRenderPassEncoderSetVertexBuffer(pass,static_cast<std::uint32_t>(i),buffers[i],0,WGPU_WHOLE_SIZE);lastBuffers[i]=buffers[i];}
        const auto range=item.geometry->drawRange;
        if(item.points){const unsigned first=static_cast<unsigned>(std::max(0,range.start)),n=std::min<unsigned>(vertices-std::min(first,vertices),static_cast<unsigned>(range.count));if(n)wgpuRenderPassEncoderDraw(pass,6,n,0,first);}
        else if(auto* idx=item.geometry->getIndex()){
            g.upload(geo.index,idx->array().data(),idx->array().size()*4,WGPUBufferUsage_Index,idx->version);
            const unsigned total=static_cast<unsigned>(idx->array().size()),first=std::min(total,static_cast<unsigned>(std::max(0,range.start))),n=std::min(total-first,static_cast<unsigned>(range.count));
            if(g.reallocations!=seenReallocations){lastIndex=nullptr;seenReallocations=g.reallocations;lastBuffers.fill(nullptr);}
            if(geo.index.buffer!=lastIndex){wgpuRenderPassEncoderSetIndexBuffer(pass,geo.index.buffer,WGPUIndexFormat_Uint32,0,WGPU_WHOLE_SIZE);lastIndex=geo.index.buffer;}
            if(n)wgpuRenderPassEncoderDrawIndexed(pass,n,1,first,0,0);
        }else{const unsigned first=std::min(vertices,static_cast<unsigned>(std::max(0,range.start))),n=std::min(vertices-first,static_cast<unsigned>(range.count));if(n)wgpuRenderPassEncoderDraw(pass,n,1,first,0);}
        ++g.drawn;
    }
    wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
    if(timing){ // Resolve now; map once this frame's commands have run (the callback fires on a later poll).
        wgpuCommandEncoderResolveQuerySet(encoder,g.timestamps,0,2,g.resolved,0);wgpuCommandEncoderCopyBufferToBuffer(encoder,g.resolved,0,g.readback,0,16);g.mapping=true;
        WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&g;
        cb.callback=[](WGPUMapAsyncStatus status,WGPUStringView,void* u,void*){auto& r=*static_cast<Impl*>(u);
            if(status==WGPUMapAsyncStatus_Success){const auto* t=static_cast<const std::uint64_t*>(wgpuBufferGetConstMappedRange(r.readback,0,16));if(t[1]>t[0])r.gpuMs=static_cast<double>(t[1]-t[0])*r.tickNanoseconds*1e-6;wgpuBufferUnmap(r.readback);}
            r.mapping=false;};
        g.pendingMap=cb;g.mapPending=true;
    }
    wgpuInstanceProcessEvents(g.gpu.instance());
    if(g.frame%60==0)g.evict();
}
}
