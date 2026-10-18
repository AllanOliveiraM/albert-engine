// WebGPU scene renderer, offscreen with readback: background, depth, blending, textures and
// instanced points. Exits 77 (skipped) without a WebGPU adapter.
#include "gpu/context.hpp"
#include "gpu/programs.hpp"
#include "gpu/scene.hpp"
#include <threepp/threepp.hpp>
#include <threepp/materials/ShaderMaterial.hpp>
#include <threepp/textures/DataTexture.hpp>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
using namespace threepp;
namespace {
void require(bool ok,const char* m){if(!ok)throw std::runtime_error(m);}
struct Target {WGPUTexture texture;WGPUTextureView view;};
std::vector<unsigned char> read(albert::gpu::Context& gpu,WGPUTexture texture,unsigned w,unsigned h){
    const unsigned row=(w*4+255)/256*256;WGPUBufferDescriptor bd{};bd.size=row*h;bd.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;WGPUBuffer b=wgpuDeviceCreateBuffer(gpu.device(),&bd);
    WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);WGPUTexelCopyTextureInfo src{};src.texture=texture;WGPUTexelCopyBufferInfo dst{};dst.buffer=b;dst.layout.bytesPerRow=row;dst.layout.rowsPerImage=h;
    const WGPUExtent3D size{w,h,1};wgpuCommandEncoderCopyTextureToBuffer(e,&src,&dst,&size);WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu.queue(),1,&c);
    bool done=false;WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&done;cb.callback=[](WGPUMapAsyncStatus,WGPUStringView,void* u,void*){*static_cast<bool*>(u)=true;};
    wgpuBufferMapAsync(b,WGPUMapMode_Read,0,bd.size,cb);while(!done)wgpuInstanceProcessEvents(gpu.instance());
    const auto* p=static_cast<const unsigned char*>(wgpuBufferGetConstMappedRange(b,0,bd.size));std::vector<unsigned char> out(static_cast<std::size_t>(w)*h*4);
    const bool bgra=gpu.format()==WGPUTextureFormat_BGRA8Unorm;
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const auto* s=p+y*row+x*4;auto* d=&out[(static_cast<std::size_t>(y)*w+x)*4];d[0]=bgra?s[2]:s[0];d[1]=s[1];d[2]=bgra?s[0]:s[2];d[3]=s[3];}
    wgpuBufferUnmap(b);wgpuBufferRelease(b);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);return out;
}
}
int main(){
    try{
        std::unique_ptr<albert::gpu::Context> gpu;
        try{gpu=std::make_unique<albert::gpu::Context>();}catch(const std::exception& e){std::printf("skipped: %s\n",e.what());return 77;}
        std::printf("adapter: %s\n",gpu->adapter().c_str());
        constexpr unsigned w=96,h=64;
        WGPUTextureDescriptor td{};td.size={w,h,1};td.format=gpu->format();td.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;td.mipLevelCount=1;td.sampleCount=1;td.dimension=WGPUTextureDimension_2D;
        WGPUTexture target=wgpuDeviceCreateTexture(gpu->device(),&td);WGPUTextureView view=wgpuTextureCreateView(target,nullptr);
        albert::gpu::SceneRenderer renderer(*gpu);
        Scene scene;scene.background=Color(.1f,.2f,.3f);PerspectiveCamera camera(60,static_cast<float>(w)/h,.1f,100);camera.position.set(0,0,5);
        // Opaque red quad at x<0, a blue quad further back, a half-transparent green quad in front at x>0.
        auto quad=[&](const Color& c,float x,float z,bool transparent){auto m=MeshBasicMaterial::create();m->color=c;m->transparent=transparent;m->opacity=transparent?.5f:1;
            auto mesh=Mesh::create(PlaneGeometry::create(2,2),m);mesh->position.set(x,0,z);scene.add(mesh);return mesh;};
        quad(Color(1,0,0),-1,0,false);quad(Color(0,0,1),-.5f,-1,false);quad(Color(0,1,0),1.2f,1,true);
        // A textured quad (2x2 RGBA: top row white, bottom black) below.
        auto tex=DataTexture::create(ImageData(std::vector<unsigned char>{0,0,0,255, 0,0,0,255, 255,255,255,255, 255,255,255,255}),2,2);tex->minFilter=tex->magFilter=Filter::Nearest;tex->needsUpdate();
        auto tm=MeshBasicMaterial::create();tm->map=tex;auto textured=Mesh::create(PlaneGeometry::create(1,1),tm);textured->position.set(3.3f,-2.f,0);scene.add(textured);
        // Instanced points: a white 6-pixel square per vertex.
        albert::gpu::Program points;points.points=true;points.uniforms={{"size",albert::gpu::Uniform::f32}};
        points.wgsl=R"(
struct Material { size: f32 };
@group(2) @binding(0) var<uniform> material: Material;
@vertex fn vs_main(@location(0) position: vec3f, @builtin(vertex_index) corner: u32) -> @builtin(position) vec4f {
    let c = array<vec2f, 6>(vec2f(-1, -1), vec2f(1, -1), vec2f(1, 1), vec2f(-1, -1), vec2f(1, 1), vec2f(-1, 1))[corner];
    let clip = frame.projection * object.modelView * vec4f(position, 1.0);
    return vec4f(clip.xy + c * material.size / frame.viewport.xy * clip.w, clip.z, clip.w);
}
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0); }
)";
        albert::gpu::define("test:points",points);
        auto pm=ShaderMaterial::create();pm->name="test:points";pm->uniforms["size"]=Uniform(3.f);
        auto pg=BufferGeometry::create();pg->setAttribute("position",FloatBufferAttribute::create(std::vector<float>{-2.6f,1.4f,0},3));
        auto cloud=Points::create(pg,pm);scene.add(cloud);
        WGPUCommandEncoder e=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(scene,camera,view,w,h,e);
        WGPUCommandBuffer c=wgpuCommandEncoderFinish(e,nullptr);wgpuQueueSubmit(gpu->queue(),1,&c);wgpuCommandBufferRelease(c);wgpuCommandEncoderRelease(e);
        const auto px=read(*gpu,target,w,h);
        auto at=[&](float u,float v){const unsigned x=static_cast<unsigned>(u*(w-1)),y=static_cast<unsigned>(v*(h-1));const auto* p=&px[(static_cast<std::size_t>(y)*w+x)*4];return std::array<int,3>{p[0],p[1],p[2]};};
        auto show=[&](const char* n,float u,float v){const auto p=at(u,v);std::printf("%-12s %3d %3d %3d\n",n,p[0],p[1],p[2]);return p;};
        // Screen positions from the camera (fov 60°, aspect 1.5, 5 units away): u = (x/(d·tan30·1.5)+1)/2, v from the top.
        const auto corner=show("background",.02f,.98f),red=show("red",.40f,.5f),blue=show("blue behind",.525f,.5f),green=show("green blend",.75f,.5f),top=show("tex top",.88f,.80f),bottom=show("tex bottom",.88f,.90f),point=show("point",.2f,.257f);
        require(std::abs(corner[0]-26)<=1&&std::abs(corner[1]-51)<=1&&std::abs(corner[2]-76)<=1,"Background clear colour (unorm rounding varies by GPU)");
        require(red[0]>200&&red[1]<30&&red[2]<30,"Opaque quad, tone mapped and encoded");
        require(blue[2]>150&&blue[0]<30,"Depth: the far quad shows where the near one is absent");
        require(green[1]>120&&green[1]>green[0]+30&&green[2]>30&&green[2]<120,"Blended green over the background");
        require(top[0]>200&&bottom[0]<20,"Texture rows: v = 1 at the top, as in GL");
        require(point[0]>240&&point[1]>240,"Instanced point quad");
        require(renderer.drawn()==5&&albert::gpu::errors()==0,"Draw count and no WebGPU errors");
        { // Sun shadow: a caster square above a lit Standard plane darkens what it covers.
            Scene world;PerspectiveCamera eye(60,static_cast<float>(w)/h,.1f,100);eye.position.set(0,6,.01f);eye.lookAt(Vector3(0,0,0));
            auto sunlight=DirectionalLight::create(0xffffff,3.f);sunlight->position.set(6,10,0);sunlight->castShadow=true;sunlight->shadow->mapSize.set(512,512);
            auto& oc=static_cast<OrthographicCamera&>(*sunlight->shadow->camera);oc.left=oc.bottom=-4;oc.right=oc.top=4;oc.nearPlane=.1f;oc.farPlane=30;oc.updateProjectionMatrix();
            world.add(sunlight); // The default target sits at the origin.
            auto ground=MeshStandardMaterial::create();ground->color=Color(.8f,.8f,.8f);ground->roughness=1;
            auto plane=Mesh::create(PlaneGeometry::create(8,8),ground);plane->rotation.x=-3.14159265f/2;plane->receiveShadow=true;world.add(plane);
            auto blocker=Mesh::create(PlaneGeometry::create(2,2),MeshBasicMaterial::create());blocker->rotation.x=-3.14159265f/2;blocker->position.set(2,2,0);blocker->castShadow=true;world.add(blocker);
            renderer.shadows=true;
            WGPUCommandEncoder e2=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(world,eye,view,w,h,e2);
            WGPUCommandBuffer c2=wgpuCommandEncoderFinish(e2,nullptr);wgpuQueueSubmit(gpu->queue(),1,&c2);wgpuCommandBufferRelease(c2);wgpuCommandEncoderRelease(e2);
            const auto shot=read(*gpu,target,w,h);auto lum=[&](float u,float v){const auto* p=&shot[(static_cast<std::size_t>(v*(h-1))*w+static_cast<unsigned>(u*(w-1)))*4];return p[0]+p[1]+p[2];};
            // Sun from +x (tan 0.6): the blocker over x in [1, 3] at height 2 shades the ground at x in [-0.2, 1.8],
            // seen at u 0.48-0.64 where the blocker itself (u 0.65-0.93) does not cover it.
            const int lit=lum(.20f,.5f),shaded=lum(.56f,.5f);std::printf("shadow: lit %d, shaded %d\n",lit,shaded);
            require(lit>200&&shaded<lit/3&&albert::gpu::errors()==0,"Sun shadow on the receiver");
        }
        wgpuTextureViewRelease(view);wgpuTextureRelease(target);
        std::printf("gpu_scene_test: ok\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
