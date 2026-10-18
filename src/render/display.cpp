#include "display.hpp"
#include "window_icon.hpp"
#include "gpu/context.hpp"
#include "gpu/scene.hpp"
#include "gpu/window.hpp"
#include "threepp/threepp.hpp"
#include "threepp/canvas/Monitor.hpp"
#include "threepp/extras/imgui/ImguiContext.hpp"
#include <GLFW/glfw3.h>
#include <imgui_impl_wgpu.h>
using namespace threepp;
struct albert::Display::Impl {
    Backend backend;std::function<void()> draw;
    // OpenGL.
    std::unique_ptr<Canvas> canvas;std::unique_ptr<GLRenderer> renderer;std::unique_ptr<ImguiFunctionalContext> imgui;
    // WebGPU.
    std::unique_ptr<gpu::Window> window;std::unique_ptr<gpu::Context> context;std::unique_ptr<gpu::SceneRenderer> scene;
    WGPUCommandEncoder encoder=nullptr;WGPUTextureView target=nullptr;bool drawnTarget=false,styled=false;float dpi=1;
    void framebuffer(unsigned& w,unsigned& h)const{int x=0,y=0;glfwGetFramebufferSize(window->handle(),&x,&y);w=static_cast<unsigned>(std::max(x,1));h=static_cast<unsigned>(std::max(y,1));}
    void pass(WGPULoadOp load){ // A colour-only pass on the surface (clear, or ImGui over the scene).
        WGPURenderPassColorAttachment c{};c.view=target;c.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;c.loadOp=load;c.storeOp=WGPUStoreOp_Store;c.clearValue={0,0,0,1};
        WGPURenderPassDescriptor d{};d.colorAttachmentCount=1;d.colorAttachments=&c;WGPURenderPassEncoder p=wgpuCommandEncoderBeginRenderPass(encoder,&d);
        if(load==WGPULoadOp_Load)ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(),p);
        wgpuRenderPassEncoderEnd(p);wgpuRenderPassEncoderRelease(p);drawnTarget=true;
    }
};
albert::Display::Display(Backend backend,const std::string& title,WindowSize size,const std::filesystem::path& icon,std::function<void()> draw):impl_(std::make_unique<Impl>()){
    auto& d=*impl_;d.backend=backend;d.draw=std::move(draw);
    if(backend==Backend::opengl){
        Canvas::Parameters p;p.title(title).antialiasing(4).size(size).favicon(icon);
        d.canvas=std::make_unique<Canvas>(p);d.canvas->exitOnKeyEscape(false);d.renderer=std::make_unique<GLRenderer>(*d.canvas);
        setWindowIcon(d.canvas->windowPtr(),icon);
        d.renderer->toneMapping=ToneMapping::ACESFilmic;d.renderer->toneMappingExposure=1.05f;d.renderer->setPixelRatio(std::min(2.f,d.renderer->getTargetPixelRatio()));
        d.renderer->shadowMap().enabled=true;d.renderer->shadowMap().type=ShadowMap::PFC;d.renderer->shadowMap().autoUpdate=false;
        d.imgui=std::make_unique<ImguiFunctionalContext>(*d.canvas,[this]{if(impl_->draw)impl_->draw();});
        d.canvas->onWindowResize([this](WindowSize s){impl_->renderer->setSize(s);});
        return;
    }
    d.window=std::make_unique<gpu::Window>(title,size.width(),size.height());setWindowIcon(d.window->handle(),icon);
    d.context=std::make_unique<gpu::Context>(d.window->handle());d.scene=std::make_unique<gpu::SceneRenderer>(*d.context);d.scene->exposure=1.05f;
    std::printf("WebGPU: %s\n",d.context->adapter().c_str());
    d.window->onResize([this](WindowSize){unsigned w,h;impl_->framebuffer(w,h);impl_->context->resize(static_cast<int>(w),static_cast<int>(h));});
    ImGui::CreateContext();ImGui_ImplGlfw_InitForOther(d.window->handle(),true);
    ImGui_ImplWGPU_InitInfo info;info.Device=d.context->device();info.RenderTargetFormat=d.context->format();info.DepthStencilFormat=WGPUTextureFormat_Undefined;ImGui_ImplWGPU_Init(&info);
    d.dpi=monitor::contentScale().first;
}
albert::Display::~Display(){
    auto& d=*impl_;
    if(d.backend==Backend::webgpu){ImGui_ImplWGPU_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();d.scene.reset();d.context.reset();d.window.reset();}
}
albert::Display::Backend albert::Display::backend()const{return impl_->backend;}
PeripheralsEventSource& albert::Display::events(){return impl_->canvas?static_cast<PeripheralsEventSource&>(*impl_->canvas):*impl_->window;}
GLRenderer* albert::Display::gl(){return impl_->renderer.get();}
bool albert::Display::isOpen()const{return impl_->canvas?impl_->canvas->isOpen():impl_->window->isOpen();}
void albert::Display::close(){if(impl_->canvas)impl_->canvas->close();else impl_->window->close();}
float albert::Display::aspect()const{return impl_->canvas?impl_->canvas->aspect():impl_->window->aspect();}
void albert::Display::onResize(std::function<void(WindowSize)> f){if(impl_->canvas)impl_->canvas->onWindowResize(std::move(f));else impl_->window->onResize(std::move(f));}
void albert::Display::setExposure(float value){if(impl_->renderer)impl_->renderer->toneMappingExposure=value;else impl_->scene->exposure=value;}
float albert::Display::exposure()const{return impl_->renderer?impl_->renderer->toneMappingExposure:impl_->scene->exposure;}
bool albert::Display::frame(const std::function<void()>& work){
    auto& d=*impl_;
    if(d.canvas)return d.canvas->animateOnce(work);
    if(!d.window->isOpen())return false;
    d.window->poll();
    if(!(d.target=d.context->acquire())){unsigned w,h;d.framebuffer(w,h);d.context->resize(static_cast<int>(w),static_cast<int>(h));return d.window->isOpen();}
    d.encoder=wgpuDeviceCreateCommandEncoder(d.context->device(),nullptr);d.drawnTarget=false;
    work();
    if(!d.drawnTarget)d.pass(WGPULoadOp_Clear);
    WGPUCommandBuffer commands=wgpuCommandEncoderFinish(d.encoder,nullptr);wgpuQueueSubmit(d.context->queue(),1,&commands);
    wgpuCommandBufferRelease(commands);wgpuCommandEncoderRelease(d.encoder);d.encoder=nullptr;d.context->present();d.target=nullptr;
    return d.window->isOpen();
}
void albert::Display::clear(){auto& d=*impl_;if(d.renderer)d.renderer->clear();else if(d.encoder)d.pass(WGPULoadOp_Clear);}
void albert::Display::render(Scene& scene,Camera& camera){
    auto& d=*impl_;if(d.renderer){d.renderer->render(scene,camera);return;}
    if(!d.encoder)return;
    unsigned w,h;d.framebuffer(w,h);d.scene->render(scene,camera,d.target,w,h,d.encoder);d.drawnTarget=true;
}
void albert::Display::ui(){
    auto& d=*impl_;if(d.imgui){d.imgui->render();return;}
    if(!d.encoder)return;
    if(!d.styled){ImGuiStyle& style=ImGui::GetStyle();style=ImGuiStyle();style.FontScaleDpi=d.dpi;style.ScaleAllSizes(d.dpi);d.styled=true;} // As threepp's ImGui context.
    ImGui_ImplWGPU_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();if(d.draw)d.draw();ImGui::Render();
    if(!d.drawnTarget)d.pass(WGPULoadOp_Clear);
    d.pass(WGPULoadOp_Load);
}
