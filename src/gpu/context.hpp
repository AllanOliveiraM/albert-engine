#pragma once
#include <webgpu/webgpu.h>
#include <functional>
#include <string>
#include <vector>
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
    WGPUTextureFormat format()const{return format_;} // Surface format: 8-bit unorm, not sRGB.
    const std::string& adapter()const{return adapterName_;}
    [[nodiscard]] bool software()const{return software_;} // A CPU adapter (llvmpipe, WARP).
    void resize(int width,int height);
    // Vertical sync (FIFO, the default) or, off, the fastest present mode the surface offers (immediate, else mailbox).
    void setVsync(bool on);
    [[nodiscard]] bool vsync()const{return vsync_;}
    // The next surface image, or nullptr when it must be reconfigured (resize) or is lost.
    WGPUTextureView acquire();
    // The size of the image acquire() gave this frame (a resize reaches it a frame on): what a pass on it may draw within.
    [[nodiscard]] unsigned imageWidth()const;[[nodiscard]] unsigned imageHeight()const;
    void capturePng(const std::string& path); // After submission, before present; throws on readback or write failure.
    void readRgba(std::vector<unsigned char>& rgba,unsigned& width,unsigned& height); // The same frame as RGBA rows (no padding).
    static void beforePresent(std::function<void(Context&)> work); // One-shot work on this thread's next surface, after submission.
    void present();
private:
    WGPUInstance instance_=nullptr;WGPUSurface surface_=nullptr;WGPUAdapter adapter_=nullptr;WGPUDevice device_=nullptr;WGPUQueue queue_=nullptr;
    WGPUTextureFormat format_=WGPUTextureFormat_BGRA8Unorm;WGPUTexture current_=nullptr;WGPUTextureView view_=nullptr;std::string adapterName_;bool software_=false;
    int width_=0,height_=0;bool vsync_=true,stale_=false,copySrc_=false;WGPUPresentMode unsynced_=WGPUPresentMode_Fifo;
};
std::string text(WGPUStringView s);
unsigned errors(); // Uncaptured WebGPU errors so far (validation bugs).
}
