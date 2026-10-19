#include "display.hpp"
#include "window_icon.hpp"
#include "webgpu_programs.hpp"
#include "gpu/atmosphere.hpp"
#include "gpu/context.hpp"
#include "gpu/scene.hpp"
#include "gpu/window.hpp"
#include "threepp/threepp.hpp"
#include "threepp/canvas/Monitor.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <imgui_impl_wgpu.h>
using namespace threepp;
struct albert::Display::Impl {
    std::function<void()> draw;
    std::unique_ptr<gpu::Window> window;std::unique_ptr<gpu::Context> context;std::unique_ptr<gpu::SceneRenderer> scene;std::unique_ptr<gpu::Atmosphere> earthAir,marsAir;
    WGPUCommandEncoder encoder=nullptr;WGPUTextureView target=nullptr;bool drawnTarget=false,styled=false;float dpi=1;
    void framebuffer(unsigned& w,unsigned& h)const{int x=0,y=0;glfwGetFramebufferSize(window->handle(),&x,&y);w=static_cast<unsigned>(std::max(x,1));h=static_cast<unsigned>(std::max(y,1));}
    void pass(WGPULoadOp load){ // A colour-only pass on the surface (clear, or ImGui over the scene).
        WGPURenderPassColorAttachment c{};c.view=target;c.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;c.loadOp=load;c.storeOp=WGPUStoreOp_Store;c.clearValue={0,0,0,1};
        WGPURenderPassDescriptor d{};d.colorAttachmentCount=1;d.colorAttachments=&c;WGPURenderPassEncoder p=wgpuCommandEncoderBeginRenderPass(encoder,&d);
        if(load==WGPULoadOp_Load)ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(),p);
        wgpuRenderPassEncoderEnd(p);wgpuRenderPassEncoderRelease(p);drawnTarget=true;
    }
};
albert::Display::Display(const std::string& title,WindowSize size,const std::filesystem::path& icon,std::function<void()> draw):impl_(std::make_unique<Impl>()){
    auto& d=*impl_;d.draw=std::move(draw);
    d.window=std::make_unique<gpu::Window>(title,size.width(),size.height());setWindowIcon(d.window->handle(),icon);
    defineWebGpuPrograms();
    d.context=std::make_unique<gpu::Context>(d.window->handle());d.scene=std::make_unique<gpu::SceneRenderer>(*d.context);d.scene->exposure=1.05f;
    std::printf("WebGPU: %s\n",d.context->adapter().c_str());
    d.window->onResize([this](WindowSize){unsigned w,h;impl_->framebuffer(w,h);impl_->context->resize(static_cast<int>(w),static_cast<int>(h));});
    ImGui::CreateContext();ImGui_ImplGlfw_InitForOther(d.window->handle(),true);
    ImGui_ImplWGPU_InitInfo info;info.Device=d.context->device();info.RenderTargetFormat=d.context->format();info.DepthStencilFormat=WGPUTextureFormat_Undefined;ImGui_ImplWGPU_Init(&info);
    d.dpi=monitor::contentScale().first;
}
albert::Display::~Display(){
    auto& d=*impl_;ImGui_ImplWGPU_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();d.scene.reset();d.earthAir.reset();d.marsAir.reset();d.context.reset();d.window.reset();
}
PeripheralsEventSource& albert::Display::events(){return *impl_->window;}
bool albert::Display::isOpen()const{return impl_->window->isOpen();}
void albert::Display::close(){impl_->window->close();}
float albert::Display::aspect()const{return impl_->window->aspect();}
void albert::Display::onResize(std::function<void(WindowSize)> f){impl_->window->onResize(std::move(f));}
void albert::Display::setShadows(bool enabled){impl_->scene->shadows=enabled;}
void albert::Display::setExposure(float value){impl_->scene->exposure=value;}
void albert::Display::setWeather(threepp::Texture* clouds,float cloudKm,float cloudOpacity,float surfaceWind,float polarRatio){
    auto& d=*impl_;auto& a=d.scene->air;a.clouds=clouds;a.cloudKm=cloudKm;a.cloudShadow=clouds?cloudOpacity:0.f;a.surfaceWind=surfaceWind;a.polarRatio=polarRatio;}
void albert::Display::setAir(const Object3D* body,float kmPerUnit,float strength,float groundKm,bool mars){
    auto& d=*impl_;auto& air=mars?d.marsAir:d.earthAir; // Each precomputed once, when first needed.
    if(!air&&body&&strength>0){air=std::make_unique<gpu::Atmosphere>(*d.context,mars?gpu::marsAtmosphere():gpu::earthAtmosphere());d.scene->airSteps=d.context->software()?1:4;}
    auto weather=d.scene->air;d.scene->air={air.get(),body,kmPerUnit,strength,groundKm,weather.clouds,weather.cloudKm,weather.cloudShadow,weather.surfaceWind,weather.polarRatio};
}
void albert::Display::setEarthshine(const Object3D* earth,float radius){impl_->scene->earthshine={earth,earth?radius:0.f};}
float albert::Display::exposure()const{return impl_->scene->exposure;}
namespace {
// ALBERT_FRAME_STATS=1: mean, p99 and worst frame time every 300 frames (stdout).
double workMs=0,presentMs=0,gpuMs=-1; // Per-frame parts on WebGPU: app work and recording; acquire, submit and present; scene pass on the GPU.
void frameStats(){
    static const bool on=std::getenv("ALBERT_FRAME_STATS")!=nullptr;if(!on)return;
    static std::vector<double> times,gpu;static double work=0,present=0;static auto last=std::chrono::steady_clock::now();if(gpuMs>=0)gpu.push_back(gpuMs);
    const auto now=std::chrono::steady_clock::now();times.push_back(std::chrono::duration<double,std::milli>(now-last).count());last=now;work+=workMs;present+=presentMs;
    if(times.size()<300)return;
    std::sort(times.begin(),times.end());double sum=0;for(double t:times)sum+=t;const double n=static_cast<double>(times.size());
    std::sort(gpu.begin(),gpu.end());const double g50=gpu.empty()?-1:gpu[gpu.size()/2],g99=gpu.empty()?-1:gpu[gpu.size()*99/100];
    std::printf("frames: mean %.2f ms, p99 %.2f ms, worst %.2f ms (work %.2f, present %.2f; GPU scene median %.2f, p99 %.2f)\n",sum/n,times[times.size()*99/100],times.back(),work/n,present/n,g50,g99);std::fflush(stdout);times.clear();gpu.clear();work=present=0;
}
}
bool albert::Display::frame(const std::function<void()>& work){
    auto& d=*impl_;frameStats();
    if(!d.window->isOpen())return false;
    d.window->poll();
    const auto t0=std::chrono::steady_clock::now();
    if(!(d.target=d.context->acquire())){unsigned w,h;d.framebuffer(w,h);d.context->resize(static_cast<int>(w),static_cast<int>(h));return d.window->isOpen();}
    const auto t1=std::chrono::steady_clock::now();
    d.encoder=wgpuDeviceCreateCommandEncoder(d.context->device(),nullptr);d.drawnTarget=false;
    work();
    if(!d.drawnTarget)d.pass(WGPULoadOp_Clear);
    const auto t2=std::chrono::steady_clock::now();
    WGPUCommandBuffer commands=wgpuCommandEncoderFinish(d.encoder,nullptr);wgpuQueueSubmit(d.context->queue(),1,&commands);d.scene->submitted();
    wgpuCommandBufferRelease(commands);wgpuCommandEncoderRelease(d.encoder);d.encoder=nullptr;d.context->present();d.target=nullptr;gpuMs=d.scene->gpuMilliseconds();
    const auto t3=std::chrono::steady_clock::now();
    workMs=std::chrono::duration<double,std::milli>(t2-t1).count();presentMs=std::chrono::duration<double,std::milli>((t1-t0)+(t3-t2)).count();
    return d.window->isOpen();
}
void albert::Display::clear(){auto& d=*impl_;if(d.encoder)d.pass(WGPULoadOp_Clear);}
void albert::Display::render(Scene& scene,Camera& camera){
    auto& d=*impl_;if(!d.encoder)return;
    unsigned w,h;d.framebuffer(w,h);const auto t0=std::chrono::steady_clock::now();d.scene->render(scene,camera,d.target,w,h,d.encoder);d.drawnTarget=true;
    static const bool debug=std::getenv("ALBERT_TERRAIN_DEBUG")!=nullptr;
    if(debug){static int n=0;static double sum=0;sum+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();if(++n%60==0){std::fprintf(stderr,"scene render cpu %.2f ms, %u draws\n",sum/60,d.scene->drawn());sum=0;}}
}
void albert::Display::ui(){
    auto& d=*impl_;if(!d.encoder)return;
    if(!d.styled){ImGuiStyle& style=ImGui::GetStyle();style=ImGuiStyle();style.FontScaleDpi=d.dpi;style.ScaleAllSizes(d.dpi);d.styled=true;} // As threepp's ImGui context.
    ImGui_ImplWGPU_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();if(d.draw)d.draw();ImGui::Render();
    if(!d.drawnTarget)d.pass(WGPULoadOp_Clear);
    d.pass(WGPULoadOp_Load);
}
