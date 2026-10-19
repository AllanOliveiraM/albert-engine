// WebGPU scene renderer, offscreen with readback: background, depth, blending, textures and
// instanced points. Exits 77 (skipped) without a WebGPU adapter.
#include "gpu/context.hpp"
#include "gpu/programs.hpp"
#include "gpu/scene.hpp"
#include <threepp/threepp.hpp>
#include <threepp/materials/ShaderMaterial.hpp>
#include <threepp/textures/DataTexture.hpp>
#include <array>
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
        albert::gpu::SceneRenderer renderer(*gpu);renderer.samples=1; // Exact pixel checks.
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
            // A new map size recreates the map: the frame bind group must sample the new one (the
            // moved blocker now shades x in [-2.2, -0.2], seen at u 0.29-0.36 beside its own image).
            sunlight->shadow->mapSize.set(1024,1024);blocker->position.set(0,2,0);
            WGPUCommandEncoder e3=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(world,eye,view,w,h,e3);
            WGPUCommandBuffer c3=wgpuCommandEncoderFinish(e3,nullptr);wgpuQueueSubmit(gpu->queue(),1,&c3);wgpuCommandBufferRelease(c3);wgpuCommandEncoderRelease(e3);
            const auto moved=read(*gpu,target,w,h);auto lum2=[&](float u,float v){const auto* p=&moved[(static_cast<std::size_t>(v*(h-1))*w+static_cast<unsigned>(u*(w-1)))*4];return p[0]+p[1]+p[2];};
            std::printf("resized shadow map: lit %d, shaded %d\n",lum2(.20f,.5f),lum2(.32f,.5f));
            require(lum2(.20f,.5f)>200&&lum2(.32f,.5f)<lum2(.20f,.5f)/3&&albert::gpu::errors()==0,"Shadow after the map is recreated");
            renderer.shadows=false;
        }
        auto frameOf=[&](Scene& sc,Camera& cam){WGPUCommandEncoder en=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(sc,cam,view,w,h,en);
            WGPUCommandBuffer cb=wgpuCommandEncoderFinish(en,nullptr);wgpuQueueSubmit(gpu->queue(),1,&cb);wgpuCommandBufferRelease(cb);wgpuCommandEncoderRelease(en);
            const auto shot=read(*gpu,target,w,h);const auto* p=&shot[(static_cast<std::size_t>(h/2)*w+w/2)*4];return std::array<int,3>{p[0],p[1],p[2]};};
        { // Textures: a small one replaced every frame (bind groups follow each new view), then a 16 MB
          // one whose update streams over two frames while the old copy keeps drawing.
            Scene sc;OrthographicCamera cam(-1,1,1,-1,.1f,10);cam.position.set(0,0,5);
            auto small=DataTexture::create(ImageData(std::vector<unsigned char>(16,255)),2,2);auto mat=MeshBasicMaterial::create();mat->map=small;
            sc.add(Mesh::create(PlaneGeometry::create(4,4),mat));
            for(int i=0;i<4;++i){auto& d=small->image().data();for(std::size_t k=0;k<d.size();k+=4){d[k]=i%2?255:0;d[k+1]=i%2?0:255;d[k+2]=0;d[k+3]=255;}small->needsUpdate();
                const auto p=frameOf(sc,cam);std::printf("replaced texture %d: %d %d %d\n",i,p[0],p[1],p[2]);require(i%2?p[0]>p[1]+80:p[1]>p[0]+50,"Replaced texture drawn in its frame");}
            auto big=DataTexture::create(ImageData(std::vector<unsigned char>(2048*2048*4,0)),2048,2048);
            auto paint=[&](int c){auto& d=big->image().data();for(std::size_t k=0;k<d.size();k+=4){d[k]=c==0?255:0;d[k+1]=c==1?255:0;d[k+2]=0;d[k+3]=255;}big->needsUpdate();};
            paint(0);mat->map=big;mat->needsUpdate();
            const auto first=frameOf(sc,cam);paint(1);const auto during=frameOf(sc,cam),after=frameOf(sc,cam);
            std::printf("streamed texture: first %d %d, during %d %d, after %d %d\n",first[0],first[1],during[0],during[1],after[0],after[1]);
            require(first[0]>first[1]+80,"A first copy uploads at once");
            require(during[0]>during[1]+80,"An update larger than the frame budget keeps the old copy while it streams");
            require(after[1]>after[0]+50&&albert::gpu::errors()==0,"The streamed copy replaces the old one");
        }
        { // A 1x1 placeholder (TextureUploads) replaced by a large image: the image shows at once, not the placeholder.
            Scene sc;OrthographicCamera cam(-1,1,1,-1,.1f,10);cam.position.set(0,0,5);
            auto tex=DataTexture::create(ImageData(std::vector<unsigned char>{0,0,0,255}),1,1);auto mat=MeshBasicMaterial::create();mat->map=tex;sc.add(Mesh::create(PlaneGeometry::create(4,4),mat));
            frameOf(sc,cam);tex->image()=Image(std::vector<unsigned char>(2048*2048*4,255),2048,2048);tex->needsUpdate();
            const auto p=frameOf(sc,cam);std::printf("placeholder replaced: %d %d %d\n",p[0],p[1],p[2]);require(p[0]>200&&p[1]>200,"A replaced placeholder shows the new image at once");
        }
        { // Chunks sharing one attribute (as the border lines do) share one GPU copy.
            Scene sc;OrthographicCamera cam(-1,1,1,-1,.1f,10);cam.position.set(0,0,5);
            std::vector<float> triangles;for(int i=0;i<20000;++i)for(float v:{-.5f,-.5f,0.f,.5f,-.5f,0.f,0.f,.5f,0.f})triangles.push_back(v);
            const std::shared_ptr<BufferAttribute> shared=FloatBufferAttribute::create(std::move(triangles),3);const std::size_t bytes=20000*9*4;
            for(int i=0;i<8;++i){auto g=BufferGeometry::create();g->setAttribute("position",shared);g->setDrawRange(i*3000,3000);sc.add(Mesh::create(g,MeshBasicMaterial::create()));}
            const std::size_t before=renderer.vertexBytes();frameOf(sc,cam);const std::size_t added=renderer.vertexBytes()-before;
            std::printf("shared attribute: %zu bytes resident for %zu\n",added,bytes);
            require(added>bytes/2&&added<bytes*3/2&&renderer.drawn()==8,"One GPU copy for a shared attribute"); // Not eight; other buffers may go meanwhile.
        }
        { // Lunar photometry by material name ("albert:moon"): a sphere lit from the camera's side is as bright at 0.9 of its
          // radius as at the centre (no limb darkening at full phase), where Phong falls off as cos i; earthshine (an Earth
          // 60.27 radii behind the camera, the sun behind the sphere) lights its night side, which stays black without it.
            auto linear=[](int c){const double v=c/255.;return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};
            auto disc=[&](const char* name,Vector3 sunAt,float intensity,const Object3D* earth){
                Scene sc;sc.background=Color(0,0,0);OrthographicCamera cam(-1.2f,1.2f,.8f,-.8f,.1f,100);cam.position.set(0,0,5);
                auto mat=MeshPhongMaterial::create();mat->color=Color(.5f,.5f,.5f);mat->specular=Color(0,0,0);mat->name=name;
                sc.add(Mesh::create(SphereGeometry::create(.75f,128,64),mat));auto sun=DirectionalLight::create(Color(1,1,1),intensity);sun->position.copy(sunAt);sc.add(sun);
                renderer.toneMapping=false;renderer.earthshine={earth,earth?1.f:0.f};
                WGPUCommandEncoder en=wgpuDeviceCreateCommandEncoder(gpu->device(),nullptr);renderer.render(sc,cam,view,w,h,en);
                WGPUCommandBuffer cb=wgpuCommandEncoderFinish(en,nullptr);wgpuQueueSubmit(gpu->queue(),1,&cb);wgpuCommandBufferRelease(cb);wgpuCommandEncoderRelease(en);
                renderer.toneMapping=true;renderer.earthshine={};
                const auto shot=read(*gpu,target,w,h);auto at=[&](double x){const auto* q=&shot[(static_cast<std::size_t>(h/2)*w+static_cast<unsigned>(std::lround(w/2.+x*.75/1.2*w/2.)))*4];return linear(q[1]);};
                return std::array<double,2>{at(0),at(.9)};};
            const auto moon=disc("albert:moon",Vector3(0,0,10),.5f,nullptr),phong=disc("",Vector3(0,0,10),.5f,nullptr);
            std::printf("full phase at 0.9 of the radius over the centre: lunar %.3f, Phong %.3f\n",moon[1]/moon[0],phong[1]/phong[0]);
            require(std::abs(moon[1]/moon[0]-1)<.08&&phong[1]/phong[0]<.6,"Lunar photometry: no limb darkening at full phase");
            Object3D earth;earth.position.set(0,0,60.27f);earth.updateMatrixWorld();
            const auto lit=disc("albert:moon",Vector3(0,0,-10),6000,&earth),dark=disc("albert:moon",Vector3(0,0,-10),6000,nullptr);
            // At full Earth from the near face (59.52 radii): 6000 × 0.434 / 59.52² × R30 / π × F(0) / (X(30°,0,30°) F(30°)), R30 = 0.866 × 0.5.
            const double r30=.8660254*.5,pi=3.14159265358979;
            auto lobes=[&](double alpha){const double a=alpha*pi/180,c=std::cos(a),g1=.14*r30-.262,g2=.44;
                return (1+1.78/(1+std::tan(a/2)/.048))*(.45*(1-g1*g1)/std::pow(1+g1*g1+2*g1*c,1.5)+.55*(1-g2*g2)/std::pow(1+g2*g2+2*g2*c,1.5));};
            const double expected=6000*.434/(59.52*59.52)*r30/pi/(.90383*lobes(30)*(1-3*.07716)/lobes(3));
            std::printf("night side: %.3f with earthshine (expected %.3f), %.3f without\n",lit[0],expected,dark[0]);
            require(std::abs(lit[0]/expected-1)<.04&&dark[0]<.002,"Earthshine lights the night side");
        }
        { // Multisampling resolves into the target: the first scene's flat colours survive it.
            renderer.samples=4;const auto p=frameOf(scene,camera);renderer.samples=1;
            std::printf("msaa centre %d %d %d\n",p[0],p[1],p[2]);require(albert::gpu::errors()==0,"MSAA pass without errors");
        }
        wgpuTextureViewRelease(view);wgpuTextureRelease(target);
        std::printf("gpu_scene_test: ok\n");return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
