#pragma once
#include <webgpu/webgpu.h>
#include <string>
struct GLFWwindow;
namespace albert::gpu {
// WebGPU instance, adapter, device and queue; with a window, its surface (FIFO, no tearing).
// ALBERT_GPU=high|low|software picks the adapter (default: high performance).
class Context {
public:
    explicit Context(GLFWwindow* window=nullptr); // nullptr: offscreen only.
    ~Context();
    Context(const Context&)=delete;Context& operator=(const Context&)=delete;
    WGPUInstance instance()const{return instance_;}
    WGPUDevice device()const{return device_;}
    WGPUQueue queue()const{return queue_;}
    WGPUTextureFormat format()const{return format_;} // Surface format (sRGB when offered).
    const std::string& adapter()const{return adapterName_;}
    void resize(int width,int height);
    // The next surface image, or nullptr when it must be reconfigured (resize) or is lost.
    WGPUTextureView acquire();
    void present();
private:
    WGPUInstance instance_=nullptr;WGPUSurface surface_=nullptr;WGPUAdapter adapter_=nullptr;WGPUDevice device_=nullptr;WGPUQueue queue_=nullptr;
    WGPUTextureFormat format_=WGPUTextureFormat_BGRA8UnormSrgb;WGPUTexture current_=nullptr;WGPUTextureView view_=nullptr;std::string adapterName_;
    int width_=0,height_=0;
};
std::string text(WGPUStringView s);
unsigned errors(); // Uncaptured WebGPU errors so far (validation bugs).
}
