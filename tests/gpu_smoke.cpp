// WebGPU bring-up: a window without OpenGL (threepp input listeners attached), cleared through
// the surface with an ImGui window on top, for 120 frames.
#include "gpu/context.hpp"
#include "gpu/window.hpp"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_wgpu.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
int main(){
    int status=0;
    try{
        albert::gpu::Window window("Albert WebGPU",800,450);albert::gpu::Context gpu(window.handle());
        std::printf("adapter: %s, format %d\n",gpu.adapter().c_str(),static_cast<int>(gpu.format()));
        window.onResize([&](threepp::WindowSize){int w,h;glfwGetFramebufferSize(window.handle(),&w,&h);gpu.resize(w,h);});
        ImGui::CreateContext();ImGui::GetIO().IniFilename=nullptr;ImGui_ImplGlfw_InitForOther(window.handle(),true);
        ImGui_ImplWGPU_InitInfo info;info.Device=gpu.device();info.RenderTargetFormat=gpu.format();info.DepthStencilFormat=WGPUTextureFormat_Undefined;ImGui_ImplWGPU_Init(&info);
        const int count=std::getenv("FRAMES")?std::atoi(std::getenv("FRAMES")):120;
        const auto start=std::chrono::steady_clock::now();int frames=0;
        for(;frames<count&&window.isOpen();++frames){
            window.poll();WGPUTextureView target=gpu.acquire();if(!target){int w,h;glfwGetFramebufferSize(window.handle(),&w,&h);gpu.resize(w,h);continue;}
            ImGui_ImplWGPU_NewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
            ImGui::Begin("Albert");ImGui::Text("WebGPU: %s",gpu.adapter().c_str());ImGui::Text("Frame %d",frames);ImGui::End();ImGui::Render();
            WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);
            WGPURenderPassColorAttachment color{};color.view=target;color.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;color.loadOp=WGPULoadOp_Clear;color.storeOp=WGPUStoreOp_Store;color.clearValue={.02,.05,.12,1};
            WGPURenderPassDescriptor pass{};pass.colorAttachmentCount=1;pass.colorAttachments=&color;
            WGPURenderPassEncoder p=wgpuCommandEncoderBeginRenderPass(encoder,&pass);ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(),p);wgpuRenderPassEncoderEnd(p);wgpuRenderPassEncoderRelease(p);
            WGPUCommandBuffer commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(gpu.queue(),1,&commands);
            wgpuCommandBufferRelease(commands);wgpuCommandEncoderRelease(encoder);gpu.present();
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames;
        std::printf("%d frames, %.2f ms each, %u WebGPU errors\n",frames,ms,albert::gpu::errors());status=albert::gpu::errors()?1:0;
        ImGui_ImplWGPU_Shutdown();ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());status=1;}
    glfwTerminate();return status;
}
