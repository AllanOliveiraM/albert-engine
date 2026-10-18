// WebGPU bring-up: a window without OpenGL, cleared through the surface for 120 frames.
#include "gpu/context.hpp"
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
int main(){
    if(!glfwInit())return 2;
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);GLFWwindow* window=glfwCreateWindow(640,360,"Albert WebGPU",nullptr,nullptr);if(!window)return 3;
    int status=0;
    try{
        albert::gpu::Context gpu(window);std::printf("adapter: %s, format %d\n",gpu.adapter().c_str(),static_cast<int>(gpu.format()));
        const auto start=std::chrono::steady_clock::now();int frames=0;
        for(;frames<120&&!glfwWindowShouldClose(window);++frames){
            glfwPollEvents();WGPUTextureView target=gpu.acquire();if(!target){int w,h;glfwGetFramebufferSize(window,&w,&h);gpu.resize(w,h);continue;}
            WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(gpu.device(),nullptr);
            WGPURenderPassColorAttachment color{};color.view=target;color.depthSlice=WGPU_DEPTH_SLICE_UNDEFINED;color.loadOp=WGPULoadOp_Clear;color.storeOp=WGPUStoreOp_Store;
            color.clearValue={.02,.05,.1+.4*(frames%60)/60.,1};
            WGPURenderPassDescriptor pass{};pass.colorAttachmentCount=1;pass.colorAttachments=&color;
            WGPURenderPassEncoder p=wgpuCommandEncoderBeginRenderPass(encoder,&pass);wgpuRenderPassEncoderEnd(p);wgpuRenderPassEncoderRelease(p);
            WGPUCommandBuffer commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuQueueSubmit(gpu.queue(),1,&commands);
            wgpuCommandBufferRelease(commands);wgpuCommandEncoderRelease(encoder);gpu.present();
        }
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/frames;
        std::printf("%d frames, %.2f ms each, %u WebGPU errors\n",frames,ms,albert::gpu::errors());status=albert::gpu::errors()?1:0;
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());status=1;}
    glfwDestroyWindow(window);glfwTerminate();return status;
}
