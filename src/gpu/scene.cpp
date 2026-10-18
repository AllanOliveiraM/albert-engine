#include "gpu/scene.hpp"
#include "gpu/programs.hpp"
#include <threepp/threepp.hpp>
#include <threepp/materials/ShaderMaterial.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
struct Material { colour: vec4f, emissive: vec4f, params: vec4f, flags: vec4f };
// params: roughness, metalness or shininess, alphaTest, lightMapIntensity
// flags: map sRGB, kind (0 basic, 1 phong, 2 standard), has alphaMap, has lightMap
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var map: texture_2d<f32>;
@group(2) @binding(2) var mapSampler: sampler;
@group(2) @binding(3) var alphaMap: texture_2d<f32>;
@group(2) @binding(4) var alphaSampler: sampler;
@group(2) @binding(5) var lightMap: texture_2d<f32>;
@group(2) @binding(6) var lightSampler: sampler;
struct Varyings { @builtin(position) clip: vec4f, @location(0) world: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f, @location(3) uv2: vec2f };
@vertex fn vs_main(@location(0) position: vec3f, @location(1) normal: vec3f, @location(2) uv: vec2f, @location(3) uv2: vec2f) -> Varyings {
    var o: Varyings;
    let world = object.model * vec4f(position, 1.0);
    o.clip = frame.projection * object.modelView * vec4f(position, 1.0);
    o.world = world.xyz; o.normal = object.normalWorld * normal; o.uv = uv; o.uv2 = uv2;
    return o;
}
@fragment fn fs_main(v: Varyings, @builtin(front_facing) front: bool) -> @location(0) vec4f {
    var albedo = textureSample(map, mapSampler, v.uv);
    if (material.flags.x > 0.5) { albedo = vec4f(srgbToLinear(albedo.rgb), albedo.a); }
    var base = material.colour * albedo;
    let mask = textureSample(alphaMap, alphaSampler, v.uv).g;
    let light = textureSample(lightMap, lightSampler, v.uv2).rgb;
    if (material.flags.z > 0.5) { base.a = base.a * mask; }
    if (base.a < material.params.z) { discard; }
    if (material.flags.y < 0.5) { return vec4f(finish(base.rgb), base.a); }
    var n = normalize(v.normal); if (!front) { n = -n; }
    let l = normalize(frame.sun.xyz); let e = normalize(frame.camera.xyz - v.world); let h = normalize(l + e);
    let nl = max(dot(n, l), 0.0) * frame.sun.w;
    var irradiance = frame.ambient.rgb;
    if (material.flags.w > 0.5) { irradiance = irradiance + light * material.params.w; }
    var colour: vec3f;
    if (material.flags.y < 1.5) {
        let specular = pow(max(dot(n, h), 0.0), max(material.params.y, 1.0)) * nl;
        colour = base.rgb * (irradiance + frame.sunColor.rgb * nl) + frame.sunColor.rgb * specular * 0.02;
    } else {
        let a = max(material.params.x * material.params.x, 0.002); let nh = max(dot(n, h), 0.0); let nv = max(dot(n, e), 1e-4);
        let d = a * a / (3.14159265 * pow(nh * nh * (a * a - 1.0) + 1.0, 2.0));
        let f0 = mix(vec3f(0.04), base.rgb, material.params.y);
        let f = f0 + (1.0 - f0) * pow(1.0 - max(dot(h, e), 0.0), 5.0);
        let k = a * 0.5; let g = nl / max(nl * (1.0 - k) + k, 1e-4) / (nv * (1.0 - k) + k);
        let diffuse = base.rgb * (1.0 - material.params.y);
        colour = diffuse * irradiance + (diffuse * (1.0 - f) + 3.14159265 * d * f * g / 4.0) * frame.sunColor.rgb * nl;
    }
    return vec4f(finish(colour + material.emissive.rgb), base.a);
}
)";
}
struct SceneRenderer::Impl {
    Context& gpu;WGPUDevice device;WGPUQueue queue;
    WGPUTexture depth=nullptr;WGPUTextureView depthView=nullptr;unsigned depthWidth=0,depthHeight=0;
    WGPUBuffer frameBuffer=nullptr,objectBuffer=nullptr;std::size_t objectCapacity=0;
    WGPUBindGroupLayout frameLayout=nullptr,objectLayout=nullptr;WGPUBindGroup frameGroup=nullptr,objectGroup=nullptr;
    WGPUBuffer zeros=nullptr;WGPUTexture whiteTexture=nullptr;WGPUTextureView white=nullptr;WGPUSampler linear=nullptr;
    struct Buffer {WGPUBuffer buffer=nullptr;std::size_t bytes=0;unsigned version=~0u;};
    struct Geometry {std::unordered_map<std::string,Buffer> attributes;Buffer index;unsigned used=0;};
    struct Texture {WGPUTexture texture=nullptr;WGPUTextureView view=nullptr;WGPUSampler sampler=nullptr;unsigned version=~0u,used=0;};
    struct MaterialState {Buffer uniforms;WGPUBindGroup group=nullptr;std::vector<WGPUTextureView> views;std::string program;unsigned used=0;};
    struct Compiled {WGPUShaderModule module=nullptr;WGPUBindGroupLayout layout=nullptr;WGPUPipelineLayout pipelineLayout=nullptr;Program program;};
    std::unordered_map<unsigned,Geometry> geometries;std::unordered_map<unsigned,Texture> textures;std::unordered_map<unsigned,MaterialState> materials;
    std::unordered_map<std::string,Compiled> compiled;std::unordered_map<std::string,WGPURenderPipeline> pipelines;std::set<std::string> reported;
    unsigned frame=0,drawn=0;bool floatFilterable=false;
    explicit Impl(Context& c):gpu(c),device(c.device()),queue(c.queue()){
        auto uniformLayout=[&](bool dynamic){
            WGPUBindGroupLayoutEntry e{};e.binding=0;e.visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;e.buffer.type=WGPUBufferBindingType_Uniform;e.buffer.hasDynamicOffset=dynamic;
            WGPUBindGroupLayoutDescriptor d{};d.entryCount=1;d.entries=&e;return wgpuDeviceCreateBindGroupLayout(device,&d);};
        frameLayout=uniformLayout(false);objectLayout=uniformLayout(true);
        frameBuffer=buffer(256,WGPUBufferUsage_Uniform);
        {WGPUBindGroupEntry e{};e.binding=0;e.buffer=frameBuffer;e.size=256;WGPUBindGroupDescriptor d{};d.layout=frameLayout;d.entryCount=1;d.entries=&e;frameGroup=wgpuDeviceCreateBindGroup(device,&d);}
        zeros=buffer(64,WGPUBufferUsage_Vertex);
        WGPUTextureDescriptor t{};t.size={1,1,1};t.format=WGPUTextureFormat_RGBA8Unorm;t.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;t.mipLevelCount=1;t.sampleCount=1;t.dimension=WGPUTextureDimension_2D;
        whiteTexture=wgpuDeviceCreateTexture(device,&t);white=wgpuTextureCreateView(whiteTexture,nullptr);const std::uint32_t one=0xffffffffu;
        WGPUTexelCopyTextureInfo dst{};dst.texture=whiteTexture;WGPUTexelCopyBufferLayout l{};l.bytesPerRow=4;l.rowsPerImage=1;wgpuQueueWriteTexture(queue,&dst,&one,4,&l,&t.size);
        WGPUSamplerDescriptor s{};s.addressModeU=s.addressModeV=s.addressModeW=WGPUAddressMode_ClampToEdge;s.magFilter=s.minFilter=WGPUFilterMode_Linear;s.mipmapFilter=WGPUMipmapFilterMode_Linear;s.lodMaxClamp=32;s.maxAnisotropy=1;
        linear=wgpuDeviceCreateSampler(device,&s);
        floatFilterable=wgpuDeviceHasFeature(device,WGPUFeatureName_Float32Filterable);
        Program b;b.wgsl=builtinWgsl;b.attributes={{"normal",3},{"uv",2},{"uv2",2}};b.textures={"map","alphaMap","lightMap"};define("albert:builtin",b);
    }
    ~Impl(){
        for(auto& [k,p]:pipelines)wgpuRenderPipelineRelease(p);
        for(auto& [k,c]:compiled){wgpuShaderModuleRelease(c.module);wgpuBindGroupLayoutRelease(c.layout);wgpuPipelineLayoutRelease(c.pipelineLayout);}
        for(auto& [k,g]:geometries)release(g);for(auto& [k,t]:textures)release(t);for(auto& [k,m]:materials)release(m);
        for(auto b:{frameBuffer,objectBuffer,zeros})if(b)wgpuBufferRelease(b);
        if(frameGroup)wgpuBindGroupRelease(frameGroup);if(objectGroup)wgpuBindGroupRelease(objectGroup);
        wgpuBindGroupLayoutRelease(frameLayout);wgpuBindGroupLayoutRelease(objectLayout);
        wgpuSamplerRelease(linear);wgpuTextureViewRelease(white);wgpuTextureRelease(whiteTexture);
        if(depthView)wgpuTextureViewRelease(depthView);if(depth)wgpuTextureRelease(depth);
    }
    WGPUBuffer buffer(std::size_t bytes,WGPUBufferUsage usage){WGPUBufferDescriptor d{};d.size=align(std::max<std::size_t>(bytes,16),4);d.usage=usage|WGPUBufferUsage_CopyDst;return wgpuDeviceCreateBuffer(device,&d);}
    void release(Geometry& g){for(auto& [k,b]:g.attributes)if(b.buffer)wgpuBufferRelease(b.buffer);if(g.index.buffer)wgpuBufferRelease(g.index.buffer);}
    void release(Texture& t){if(t.view)wgpuTextureViewRelease(t.view);if(t.texture)wgpuTextureRelease(t.texture);if(t.sampler)wgpuSamplerRelease(t.sampler);}
    void release(MaterialState& m){if(m.uniforms.buffer)wgpuBufferRelease(m.uniforms.buffer);if(m.group)wgpuBindGroupRelease(m.group);}
    void report(const std::string& what){if(reported.insert(what).second)std::fprintf(stderr,"WebGPU renderer: %s\n",what.c_str());}
    void upload(Buffer& b,const void* data,std::size_t bytes,WGPUBufferUsage usage,unsigned version){
        if(b.version==version&&b.buffer)return;
        if(!b.buffer||b.bytes<bytes){if(b.buffer)wgpuBufferRelease(b.buffer);b.buffer=buffer(bytes,usage);b.bytes=align(std::max<std::size_t>(bytes,16),4);}
        if(bytes%4==0)wgpuQueueWriteBuffer(queue,b.buffer,0,data,bytes);
        else{std::vector<std::byte> padded(align(bytes,4));std::memcpy(padded.data(),data,bytes);wgpuQueueWriteBuffer(queue,b.buffer,0,padded.data(),padded.size());}
        b.version=version;
    }
    const Compiled* program(const std::string& name){
        if(const auto it=compiled.find(name);it!=compiled.end())return &it->second;
        const Program* p=gpu::program(name);if(!p){report("no WebGPU program for material '"+name+"'");return nullptr;}
        Compiled c;c.program=*p;const std::string source=std::string(prelude)+p->wgsl;
        WGPUShaderSourceWGSL w{};w.chain.sType=WGPUSType_ShaderSourceWGSL;w.code=sv(source);WGPUShaderModuleDescriptor md{};md.nextInChain=&w.chain;md.label=sv(name);
        c.module=wgpuDeviceCreateShaderModule(device,&md);
        std::vector<WGPUBindGroupLayoutEntry> entries(1+2*p->textures.size());
        entries[0].binding=0;entries[0].visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;entries[0].buffer.type=WGPUBufferBindingType_Uniform;
        for(std::size_t i=0;i<p->textures.size();++i){
            auto& t=entries[1+2*i];t.binding=static_cast<std::uint32_t>(1+2*i);t.visibility=WGPUShaderStage_Vertex|WGPUShaderStage_Fragment;
            t.texture.sampleType=floatFilterable?WGPUTextureSampleType_Float:WGPUTextureSampleType_UnfilterableFloat;t.texture.viewDimension=WGPUTextureViewDimension_2D;
            auto& s=entries[2+2*i];s.binding=static_cast<std::uint32_t>(2+2*i);s.visibility=t.visibility;s.sampler.type=floatFilterable?WGPUSamplerBindingType_Filtering:WGPUSamplerBindingType_NonFiltering;
        }
        WGPUBindGroupLayoutDescriptor ld{};ld.entryCount=entries.size();ld.entries=entries.data();c.layout=wgpuDeviceCreateBindGroupLayout(device,&ld);
        WGPUBindGroupLayout layouts[]{frameLayout,objectLayout,c.layout};WGPUPipelineLayoutDescriptor pd{};pd.bindGroupLayoutCount=3;pd.bindGroupLayouts=layouts;
        c.pipelineLayout=wgpuDeviceCreatePipelineLayout(device,&pd);
        return &compiled.emplace(name,std::move(c)).first->second;
    }
    // A texture's GPU copy: 8-bit RGBA (rows flipped like GL's UNPACK_FLIP_Y), or float data as
    // rgba32float (filterable when the adapter allows it); mipmaps by box filtering on the CPU.
    WGPUTextureView view(threepp::Texture* source){
        if(!source)return white;
        auto& t=textures[source->id];t.used=frame;
        if(t.view&&t.version==source->version())return t.view;
        release(t);t={};t.used=frame;t.version=source->version();
        const Image& image=source->image();const unsigned w=image.width(),h=std::max(1u,image.height());
        if(!w)return white;
        const int channels=source->format==Format::Red?1:source->format==Format::RG?2:source->format==Format::RGB?3:4;
        const bool floats=image.isFloat()||image.isHalfFloat(); // Half floats (unused by Albert) are widened.
        const bool mips=source->generateMipmaps&&source->minFilter!=Filter::Linear&&source->minFilter!=Filter::Nearest;
        const unsigned levels=mips?1+static_cast<unsigned>(std::floor(std::log2(std::max(w,h)))):1;
        // Format: threepp's Format describes the channels of the CPU data.
        WGPUTextureDescriptor d{};d.size={w,h,1};d.format=floats?WGPUTextureFormat_RGBA32Float:WGPUTextureFormat_RGBA8Unorm;d.usage=WGPUTextureUsage_TextureBinding|WGPUTextureUsage_CopyDst;
        d.mipLevelCount=levels;d.sampleCount=1;d.dimension=WGPUTextureDimension_2D;t.texture=wgpuDeviceCreateTexture(device,&d);
        auto writeLevel=[&](const void* data,unsigned lw,unsigned lh,unsigned level,std::size_t texel){
            WGPUTexelCopyTextureInfo dst{};dst.texture=t.texture;dst.mipLevel=level;WGPUTexelCopyBufferLayout l{};l.bytesPerRow=static_cast<std::uint32_t>(lw*texel);l.rowsPerImage=lh;
            const WGPUExtent3D e{lw,lh,1};wgpuQueueWriteTexture(queue,&dst,data,lw*lh*texel,&l,&e);};
        auto build=[&](auto zero,auto read){ // Expand to RGBA (rows as given: v = 0 is the first row, as in GL), then box-filter the mip chain.
            using T=decltype(zero);std::vector<T> rgba(static_cast<std::size_t>(w)*h*4,zero);
            for(unsigned y=0;y<h;++y){const unsigned sy=y;
                for(unsigned x=0;x<w;++x)for(int c=0;c<4;++c){const std::size_t i=(static_cast<std::size_t>(sy)*w+x);
                    rgba[(static_cast<std::size_t>(y)*w+x)*4+c]=c<channels?read(i*channels+c):c==3?(sizeof(T)==1?T(255):T(1)):(channels==1?read(i):zero);}}
            unsigned lw=w,lh=h;writeLevel(rgba.data(),lw,lh,0,4*sizeof(T));
            for(unsigned level=1;level<levels;++level){
                const unsigned nw=std::max(1u,lw/2),nh=std::max(1u,lh/2);std::vector<T> next(static_cast<std::size_t>(nw)*nh*4);
                for(unsigned y=0;y<nh;++y)for(unsigned x=0;x<nw;++x)for(int c=0;c<4;++c){
                    float s=0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){const unsigned sx=std::min(lw-1,2*x+dx),sy=std::min(lh-1,2*y+dy);s+=static_cast<float>(rgba[(static_cast<std::size_t>(sy)*lw+sx)*4+c]);}
                    next[(static_cast<std::size_t>(y)*nw+x)*4+c]=static_cast<T>(sizeof(T)==1?s/4+.5f:s/4);}
                rgba.swap(next);lw=nw;lh=nh;writeLevel(rgba.data(),lw,lh,level,4*sizeof(T));
            }
        };
        if(image.isFloat()){const auto& v=image.data<float>();build(0.f,[&](std::size_t i){return i<v.size()?v[i]:0.f;});}
        else if(image.isHalfFloat()){const auto& v=image.data<std::uint16_t>();build(0.f,[&](std::size_t i){
            if(i>=v.size())return 0.f;const unsigned h16=v[i],e=(h16>>10)&31,m=h16&1023;const float s=(h16&0x8000)?-1.f:1.f;
            return e==0?s*std::ldexp(static_cast<float>(m),-24):e==31?s*1e30f:s*std::ldexp(static_cast<float>(m|1024),static_cast<int>(e)-25);});}
        else{const auto& v=image.data<unsigned char>();build(static_cast<unsigned char>(0),[&](std::size_t i){return i<v.size()?v[i]:static_cast<unsigned char>(0);});}
        t.view=wgpuTextureCreateView(t.texture,nullptr);
        auto address=[](TextureWrapping m){return m==TextureWrapping::Repeat?WGPUAddressMode_Repeat:m==TextureWrapping::MirroredRepeat?WGPUAddressMode_MirrorRepeat:WGPUAddressMode_ClampToEdge;};
        const bool filtered=!floats||floatFilterable;
        WGPUSamplerDescriptor s{};s.addressModeU=address(source->wrapS);s.addressModeV=address(source->wrapT);s.addressModeW=WGPUAddressMode_ClampToEdge;
        s.magFilter=filtered&&source->magFilter!=Filter::Nearest?WGPUFilterMode_Linear:WGPUFilterMode_Nearest;
        s.minFilter=filtered&&source->minFilter!=Filter::Nearest&&source->minFilter!=Filter::NearestMipmapNearest?WGPUFilterMode_Linear:WGPUFilterMode_Nearest;
        s.mipmapFilter=filtered&&(source->minFilter==Filter::LinearMipmapLinear||source->minFilter==Filter::NearestMipmapLinear)?WGPUMipmapFilterMode_Linear:WGPUMipmapFilterMode_Nearest;
        s.lodMaxClamp=static_cast<float>(levels);
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
        auto& state=materials[m.id];state.used=frame;
        std::vector<std::byte> bytes;std::vector<threepp::Texture*> maps;
        if(auto* shader=dynamic_cast<ShaderMaterial*>(&m)){
            std::size_t offset=0;std::vector<std::size_t> at;
            for(const auto& [n,u]:c.program.uniforms){const auto [a,size]=layout(u);offset=align(offset,a);at.push_back(offset);offset+=size;}
            bytes.assign(align(std::max<std::size_t>(offset,16),16),std::byte{});
            for(std::size_t i=0;i<c.program.uniforms.size();++i){const auto it=shader->uniforms.find(c.program.uniforms[i].first);if(it!=shader->uniforms.end())pack(bytes,at[i],c.program.uniforms[i].second,&it->second.value());}
            for(const auto& n:c.program.textures){const auto it=shader->uniforms.find(n);threepp::Texture* t=nullptr;if(it!=shader->uniforms.end())if(auto* p=std::get_if<threepp::Texture*>(&it->second.value()))t=*p;maps.push_back(t);}
        }else{
            float f[16]{1,1,1,m.opacity, 0,0,0,0, 1,0,m.alphaTest,1, 0,0,0,0};
            if(auto* c=dynamic_cast<MaterialWithColor*>(&m)){f[0]=c->color.r;f[1]=c->color.g;f[2]=c->color.b;}
            if(auto* e=dynamic_cast<MaterialWithEmissive*>(&m)){f[4]=e->emissive.r*e->emissiveIntensity;f[5]=e->emissive.g*e->emissiveIntensity;f[6]=e->emissive.b*e->emissiveIntensity;}
            if(auto* s=dynamic_cast<MeshStandardMaterial*>(&m)){f[8]=s->roughness;f[9]=s->metalness;f[13]=2;}
            else if(auto* p=dynamic_cast<MeshPhongMaterial*>(&m)){f[9]=p->shininess;f[13]=1;}
            else if(dynamic_cast<MeshLambertMaterial*>(&m)){f[9]=1;f[13]=1;}
            threepp::Texture *map=nullptr,*alpha=nullptr,*light=nullptr;
            if(auto* w=dynamic_cast<MaterialWithMap*>(&m))map=w->map.get();
            if(auto* w=dynamic_cast<MaterialWithAlphaMap*>(&m))alpha=w->alphaMap.get();
            if(auto* w=dynamic_cast<MaterialWithLightMap*>(&m)){light=w->lightMap.get();f[11]=w->lightMapIntensity;}
            f[12]=map&&map->colorSpace==ColorSpace::sRGB?1.f:0.f;f[14]=alpha?1.f:0.f;f[15]=light?1.f:0.f;
            bytes.resize(sizeof f);std::memcpy(bytes.data(),f,sizeof f);maps={map,alpha,light};
        }
        if(!state.uniforms.buffer||state.uniforms.bytes<bytes.size()){if(state.uniforms.buffer)wgpuBufferRelease(state.uniforms.buffer);state.uniforms.buffer=buffer(bytes.size(),WGPUBufferUsage_Uniform);state.uniforms.bytes=bytes.size();if(state.group){wgpuBindGroupRelease(state.group);state.group=nullptr;}}
        wgpuQueueWriteBuffer(queue,state.uniforms.buffer,0,bytes.data(),bytes.size());
        std::vector<WGPUTextureView> views;for(auto* t:maps)views.push_back(view(t));
        if(state.group&&views==state.views&&state.program==m.name)return state.group;
        if(state.group)wgpuBindGroupRelease(state.group);state.views=views;state.program=m.name;
        std::vector<WGPUBindGroupEntry> e(1+2*maps.size());e[0].binding=0;e[0].buffer=state.uniforms.buffer;e[0].size=state.uniforms.bytes;
        for(std::size_t i=0;i<maps.size();++i){e[1+2*i].binding=static_cast<std::uint32_t>(1+2*i);e[1+2*i].textureView=views[i];e[2+2*i].binding=static_cast<std::uint32_t>(2+2*i);e[2+2*i].sampler=sampler(maps[i]);}
        WGPUBindGroupDescriptor d{};d.layout=c.layout;d.entryCount=e.size();d.entries=e.data();state.group=wgpuDeviceCreateBindGroup(device,&d);
        return state.group;
    }
    WGPURenderPipeline pipeline(const std::string& name,const Compiled& c,const Material& m,const std::vector<int>& strides){
        WGPUBlendState b{};const bool blended=blend(m,b);
        const auto cull=m.side==Side::Double?WGPUCullMode_None:m.side==Side::Back?WGPUCullMode_Front:WGPUCullMode_Back;
        std::string key=name+'|'+std::to_string(blended?static_cast<int>(b.color.srcFactor)*1000+static_cast<int>(b.color.dstFactor)*10+static_cast<int>(b.color.operation):0)+'|'
            +std::to_string(static_cast<int>(b.alpha.srcFactor))+std::to_string(static_cast<int>(b.alpha.dstFactor))+'|'+std::to_string(m.depthTest)+std::to_string(m.depthWrite)+std::to_string(static_cast<int>(cull))
            +'|'+std::to_string(m.polygonOffset?m.polygonOffsetUnits:0.f)+'|'+std::to_string(m.colorWrite);
        for(int s:strides)key+=','+std::to_string(s);
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
        ds.depthCompare=m.depthTest?WGPUCompareFunction_GreaterEqual:WGPUCompareFunction_Always; // Reversed Z.
        ds.stencilFront.compare=ds.stencilBack.compare=WGPUCompareFunction_Always;ds.stencilFront.failOp=ds.stencilFront.depthFailOp=ds.stencilFront.passOp=WGPUStencilOperation_Keep;ds.stencilBack=ds.stencilFront;
        if(m.polygonOffset){ds.depthBias=static_cast<std::int32_t>(-m.polygonOffsetUnits);ds.depthBiasSlopeScale=-m.polygonOffsetFactor;} // Toward the camera, as GL's negative offsets.
        WGPURenderPipelineDescriptor d{};d.label=sv(name);d.layout=c.pipelineLayout;d.vertex.module=c.module;d.vertex.entryPoint=sv("vs_main");d.vertex.bufferCount=buffers.size();d.vertex.buffers=buffers.data();
        d.primitive.topology=WGPUPrimitiveTopology_TriangleList;d.primitive.frontFace=WGPUFrontFace_CCW;d.primitive.cullMode=c.program.points?WGPUCullMode_None:cull;
        d.depthStencil=&ds;d.multisample.count=1;d.multisample.mask=~0u;d.fragment=&f;
        return pipelines.emplace(key,wgpuDeviceCreateRenderPipeline(device,&d)).first->second;
    }
    void ensureDepth(unsigned w,unsigned h){
        if(depth&&depthWidth==w&&depthHeight==h)return;
        if(depthView)wgpuTextureViewRelease(depthView);if(depth)wgpuTextureRelease(depth);
        WGPUTextureDescriptor d{};d.size={w,h,1};d.format=WGPUTextureFormat_Depth32Float;d.usage=WGPUTextureUsage_RenderAttachment;d.mipLevelCount=1;d.sampleCount=1;d.dimension=WGPUTextureDimension_2D;
        depth=wgpuDeviceCreateTexture(device,&d);depthView=wgpuTextureCreateView(depth,nullptr);depthWidth=w;depthHeight=h;
    }
    void evict(){
        for(auto it=geometries.begin();it!=geometries.end();)if(frame-it->second.used>evictAfter){release(it->second);it=geometries.erase(it);}else ++it;
        for(auto it=textures.begin();it!=textures.end();)if(frame-it->second.used>evictAfter){release(it->second);it=textures.erase(it);}else ++it;
        for(auto it=materials.begin();it!=materials.end();)if(frame-it->second.used>evictAfter){release(it->second);it=materials.erase(it);}else ++it;
    }
};
SceneRenderer::SceneRenderer(Context& context):impl_(std::make_unique<Impl>(context)){}
SceneRenderer::~SceneRenderer()=default;
unsigned SceneRenderer::drawn()const{return impl_->drawn;}
void SceneRenderer::render(Scene& scene,Camera& camera,WGPUTextureView target,unsigned width,unsigned height,WGPUCommandEncoder encoder){
    auto& g=*impl_;++g.frame;g.drawn=0;g.ensureDepth(width,height);
    scene.updateMatrixWorld();if(!camera.parent)camera.updateMatrixWorld();
    // Reversed-Z infinite perspective: depth = near / -z, 1 at the near plane, 0 at infinity.
    Matrix4 projection;const auto* perspective=dynamic_cast<PerspectiveCamera*>(&camera);
    {const float f=perspective?1.f/std::tan(perspective->fov*3.14159265358979f/360.f):1.f,aspect=perspective?perspective->aspect:1.f,n=camera.nearPlane;
     auto& e=projection.elements;e.fill(0);e[0]=f/aspect;e[5]=f;e[11]=-1;e[14]=n;}
    Frustum frustum;Matrix4 screen;screen.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse);frustum.setFromProjectionMatrix(screen);
    struct Item {Object3D* object;Material* material;BufferGeometry* geometry;const Impl::Compiled* program;float z;int order;bool points;};
    std::vector<Item> opaque,blended;Vector3 sun;float sunOn=0;Color sunColor(0,0,0),ambient(0,0,0);
    scene.traverseVisible([&](Object3D& o){
        if(auto* light=dynamic_cast<DirectionalLight*>(&o)){Vector3 p,t;p.setFromMatrixPosition(*light->matrixWorld);t.setFromMatrixPosition(*light->target().matrixWorld);sun=p.sub(t).normalize();sunOn=1;sunColor=light->color;sunColor.multiplyScalar(light->intensity);return;}
        if(auto* light=dynamic_cast<AmbientLight*>(&o)){Color c=light->color;c.multiplyScalar(light->intensity);ambient.add(c);return;}
        const bool points=dynamic_cast<Points*>(&o)!=nullptr;if(!points&&!dynamic_cast<Mesh*>(&o))return;
        auto geometry=o.geometry();auto material=o.material();if(!geometry||!material||!material->visible)return;
        if(o.frustumCulled){if(!geometry->boundingSphere)geometry->computeBoundingSphere();Sphere s=*geometry->boundingSphere;s.applyMatrix4(*o.matrixWorld);if(!frustum.intersectsSphere(s))return;}
        const std::string name=g.programName(*material);if(name.empty()){g.report("unsupported material type on '"+o.name+"'");return;}
        const auto* program=g.program(name);if(!program)return;
        Vector3 p;p.setFromMatrixPosition(*o.matrixWorld);p.applyMatrix4(screen);
        (material->transparent?blended:opaque).push_back({&o,material.get(),geometry.get(),program,p.z,o.renderOrder,points});
    });
    std::stable_sort(opaque.begin(),opaque.end(),[](const Item& a,const Item& b){return a.order!=b.order?a.order<b.order:a.z<b.z;});
    std::stable_sort(blended.begin(),blended.end(),[](const Item& a,const Item& b){return a.order!=b.order?a.order<b.order:a.z>b.z;});
    // Frame uniforms.
    float f[64]{};std::memcpy(f,camera.matrixWorldInverse.elements.data(),64);std::memcpy(f+16,projection.elements.data(),64);
    Vector3 eye;eye.setFromMatrixPosition(*camera.matrixWorld);f[32]=eye.x;f[33]=eye.y;f[34]=eye.z;f[35]=1;
    f[36]=static_cast<float>(width);f[37]=static_cast<float>(height);f[38]=camera.nearPlane;
    f[40]=sun.x;f[41]=sun.y;f[42]=sun.z;f[43]=sunOn;f[44]=sunColor.r;f[45]=sunColor.g;f[46]=sunColor.b;f[48]=ambient.r;f[49]=ambient.g;f[50]=ambient.b;f[52]=exposure;
    wgpuQueueWriteBuffer(g.queue,g.frameBuffer,0,f,sizeof f);
    // Object uniforms, one 256-byte slot per draw.
    const std::size_t count=opaque.size()+blended.size();
    if(count*objectStride>g.objectCapacity){
        if(g.objectBuffer)wgpuBufferRelease(g.objectBuffer);if(g.objectGroup)wgpuBindGroupRelease(g.objectGroup);
        g.objectCapacity=std::max<std::size_t>(count*objectStride*2,objectStride*64);g.objectBuffer=g.buffer(g.objectCapacity,WGPUBufferUsage_Uniform);
        WGPUBindGroupEntry e{};e.binding=0;e.buffer=g.objectBuffer;e.size=224;WGPUBindGroupDescriptor d{};d.layout=g.objectLayout;d.entryCount=1;d.entries=&e;g.objectGroup=wgpuDeviceCreateBindGroup(g.device,&d);
    }
    std::vector<std::byte> objects(std::max<std::size_t>(count,1)*objectStride);std::size_t slot=0;
    auto writeObject=[&](Object3D& o){
        float* p=reinterpret_cast<float*>(objects.data()+slot*objectStride);std::memcpy(p,o.matrixWorld->elements.data(),64);
        Matrix4 mv;mv.multiplyMatrices(camera.matrixWorldInverse,*o.matrixWorld);std::memcpy(p+16,mv.elements.data(),64);
        Matrix3 nw;nw.getNormalMatrix(*o.matrixWorld);Matrix3 nv;nv.getNormalMatrix(mv);
        for(int c=0;c<3;++c){std::memcpy(p+32+4*c,nw.elements.data()+3*c,12);std::memcpy(p+44+4*c,nv.elements.data()+3*c,12);}
        return static_cast<std::uint32_t>(slot++*objectStride);
    };
    std::vector<std::uint32_t> offsets;for(auto* list:{&opaque,&blended})for(auto& i:*list)offsets.push_back(writeObject(*i.object));
    if(count)wgpuQueueWriteBuffer(g.queue,g.objectBuffer,0,objects.data(),count*objectStride);
    // Pass.
    WGPURenderPassColorAttachment colour{};colour.view=target;colour.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;colour.loadOp=WGPULoadOp_Clear;colour.storeOp=WGPUStoreOp_Store;
    if(scene.background.isColor()){const Color c=scene.background.color();colour.clearValue={c.r,c.g,c.b,1};}else colour.clearValue={0,0,0,1};
    WGPURenderPassDepthStencilAttachment depth{};depth.view=g.depthView;depth.depthLoadOp=WGPULoadOp_Clear;depth.depthStoreOp=WGPUStoreOp_Store;depth.depthClearValue=0;
    WGPURenderPassDescriptor pd{};pd.colorAttachmentCount=1;pd.colorAttachments=&colour;pd.depthStencilAttachment=&depth;
    WGPURenderPassEncoder pass=wgpuCommandEncoderBeginRenderPass(encoder,&pd);
    wgpuRenderPassEncoderSetBindGroup(pass,0,g.frameGroup,0,nullptr);
    std::size_t index=0;
    for(auto* list:{&opaque,&blended})for(auto& item:*list){
        const std::uint32_t offset=offsets[index++];auto& geo=g.geometries[item.geometry->id];geo.used=g.frame;
        const auto* positions=item.geometry->getAttribute<float>("position");if(!positions)continue;
        const unsigned vertices=static_cast<unsigned>(positions->count());
        std::vector<WGPUBuffer> buffers;std::vector<int> strides;
        auto bind=[&](const std::string& name,int components){
            if(auto* a=item.geometry->getAttribute<float>(name);a&&a->itemSize()==components){auto& b=geo.attributes[name];g.upload(b,a->array().data(),a->array().size()*4,WGPUBufferUsage_Vertex,a->version);buffers.push_back(b.buffer);strides.push_back(components*4);}
            else{buffers.push_back(g.zeros);strides.push_back(0);}};
        bind("position",3);for(const auto& [n,c]:item.program->program.attributes)bind(n,c);
        WGPURenderPipeline pipe=g.pipeline(g.programName(*item.material),*item.program,*item.material,strides);
        WGPUBindGroup group=g.materialGroup(*item.material,*item.program);
        wgpuRenderPassEncoderSetPipeline(pass,pipe);wgpuRenderPassEncoderSetBindGroup(pass,1,g.objectGroup,1,&offset);wgpuRenderPassEncoderSetBindGroup(pass,2,group,0,nullptr);
        for(std::size_t i=0;i<buffers.size();++i)wgpuRenderPassEncoderSetVertexBuffer(pass,static_cast<std::uint32_t>(i),buffers[i],0,WGPU_WHOLE_SIZE);
        const auto range=item.geometry->drawRange;
        if(item.points){const unsigned first=static_cast<unsigned>(std::max(0,range.start)),n=std::min<unsigned>(vertices-std::min(first,vertices),static_cast<unsigned>(range.count));if(n)wgpuRenderPassEncoderDraw(pass,6,n,0,first);}
        else if(auto* idx=item.geometry->getIndex()){
            g.upload(geo.index,idx->array().data(),idx->array().size()*4,WGPUBufferUsage_Index,idx->version);
            const unsigned total=static_cast<unsigned>(idx->array().size()),first=std::min(total,static_cast<unsigned>(std::max(0,range.start))),n=std::min(total-first,static_cast<unsigned>(range.count));
            wgpuRenderPassEncoderSetIndexBuffer(pass,geo.index.buffer,WGPUIndexFormat_Uint32,0,WGPU_WHOLE_SIZE);if(n)wgpuRenderPassEncoderDrawIndexed(pass,n,1,first,0,0);
        }else{const unsigned first=std::min(vertices,static_cast<unsigned>(std::max(0,range.start))),n=std::min(vertices-first,static_cast<unsigned>(range.count));if(n)wgpuRenderPassEncoderDraw(pass,n,1,first,0);}
        ++g.drawn;
    }
    wgpuRenderPassEncoderEnd(pass);wgpuRenderPassEncoderRelease(pass);
    if(g.frame%60==0)g.evict();
}
}
