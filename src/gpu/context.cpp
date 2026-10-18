#include "gpu/context.hpp"
#include <webgpu/wgpu.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
namespace albert::gpu {
namespace {std::atomic<unsigned> errorCount{0};}
unsigned errors(){return errorCount.load();}
std::string text(WGPUStringView s){return s.data?std::string(s.data,s.length==WGPU_STRLEN?std::char_traits<char>::length(s.data):s.length):std::string();}
namespace {
WGPUStringView view(std::string_view s){return{s.data(),s.size()};}
WGPUSurface surfaceFor(WGPUInstance instance,GLFWwindow* window){
    WGPUSurfaceDescriptor d{};
#if defined(_WIN32)
    WGPUSurfaceSourceWindowsHWND w{};w.chain.sType=WGPUSType_SurfaceSourceWindowsHWND;w.hinstance=GetModuleHandleW(nullptr);w.hwnd=glfwGetWin32Window(window);d.nextInChain=&w.chain;
    return wgpuInstanceCreateSurface(instance,&d);
#else
    // The bundled GLFW is built for X11 only (XWayland under Wayland sessions).
    WGPUSurfaceSourceXlibWindow x{};x.chain.sType=WGPUSType_SurfaceSourceXlibWindow;x.display=glfwGetX11Display();x.window=glfwGetX11Window(window);d.nextInChain=&x.chain;
    return wgpuInstanceCreateSurface(instance,&d);
#endif
}
}
Context::Context(GLFWwindow* window){
    instance_=wgpuCreateInstance(nullptr);if(!instance_)throw std::runtime_error("WebGPU unavailable");
    if(window&&!(surface_=surfaceFor(instance_,window)))throw std::runtime_error("No WebGPU surface for the window");
    const char* choice=std::getenv("ALBERT_GPU");const std::string_view pick=choice?choice:"high";
    WGPURequestAdapterOptions o{};o.compatibleSurface=surface_;o.forceFallbackAdapter=pick=="software";
    o.powerPreference=pick=="low"?WGPUPowerPreference_LowPower:WGPUPowerPreference_HighPerformance;
    struct Result {void* handle=nullptr;std::string message;} adapter,device;
    WGPURequestAdapterCallbackInfo ac{};ac.mode=WGPUCallbackMode_AllowProcessEvents;ac.userdata1=&adapter;
    ac.callback=[](WGPURequestAdapterStatus status,WGPUAdapter a,WGPUStringView m,void* u,void*){auto& r=*static_cast<Result*>(u);if(status==WGPURequestAdapterStatus_Success)r.handle=a;else r.message=text(m);};
    wgpuInstanceRequestAdapter(instance_,&o,ac);wgpuInstanceProcessEvents(instance_);
    if(!(adapter_=static_cast<WGPUAdapter>(adapter.handle)))throw std::runtime_error("No WebGPU adapter: "+adapter.message);
    WGPUAdapterInfo info{};if(wgpuAdapterGetInfo(adapter_,&info)==WGPUStatus_Success){adapterName_=text(info.device);wgpuAdapterInfoFreeMembers(info);}
    WGPUDeviceDescriptor dd{};dd.label=view("Albert");
    // Called from inside wgpu: never throw across it. Validation errors are bugs; they are counted and logged.
    dd.uncapturedErrorCallbackInfo.callback=[](const WGPUDevice*,WGPUErrorType,WGPUStringView m,void*,void*){++errorCount;std::fprintf(stderr,"WebGPU: %s\n",text(m).c_str());};
    WGPURequestDeviceCallbackInfo dc{};dc.mode=WGPUCallbackMode_AllowProcessEvents;dc.userdata1=&device;
    dc.callback=[](WGPURequestDeviceStatus status,WGPUDevice d,WGPUStringView m,void* u,void*){auto& r=*static_cast<Result*>(u);if(status==WGPURequestDeviceStatus_Success)r.handle=d;else r.message=text(m);};
    wgpuAdapterRequestDevice(adapter_,&dd,dc);wgpuInstanceProcessEvents(instance_);
    if(!(device_=static_cast<WGPUDevice>(device.handle)))throw std::runtime_error("No WebGPU device: "+device.message);
    queue_=wgpuDeviceGetQueue(device_);
    if(surface_){
        WGPUSurfaceCapabilities caps{};wgpuSurfaceGetCapabilities(surface_,adapter_,&caps);format_=caps.formatCount?caps.formats[0]:format_;
        // Not sRGB: shaders encode their output (tone mapping, then sRGB), so blending happens in
        // encoded space exactly as in the OpenGL renderer, and ImGui's sRGB colours pass through.
        for(std::size_t i=0;i<caps.formatCount;++i)if(caps.formats[i]==WGPUTextureFormat_BGRA8Unorm||caps.formats[i]==WGPUTextureFormat_RGBA8Unorm){format_=caps.formats[i];break;}
        wgpuSurfaceCapabilitiesFreeMembers(caps);
        int w=0,h=0;glfwGetFramebufferSize(window,&w,&h);resize(w,h);
    }
}
Context::~Context(){
    if(view_)wgpuTextureViewRelease(view_);if(current_)wgpuTextureRelease(current_);
    if(surface_)wgpuSurfaceUnconfigure(surface_);
    if(queue_)wgpuQueueRelease(queue_);if(device_)wgpuDeviceRelease(device_);if(adapter_)wgpuAdapterRelease(adapter_);
    if(surface_)wgpuSurfaceRelease(surface_);if(instance_)wgpuInstanceRelease(instance_);
}
void Context::resize(int width,int height){
    if(!surface_||width<=0||height<=0)return;width_=width;height_=height;
    WGPUSurfaceConfiguration c{};c.device=device_;c.format=format_;c.usage=WGPUTextureUsage_RenderAttachment;c.width=static_cast<std::uint32_t>(width);c.height=static_cast<std::uint32_t>(height);
    c.alphaMode=WGPUCompositeAlphaMode_Auto;c.presentMode=WGPUPresentMode_Fifo;wgpuSurfaceConfigure(surface_,&c);
}
WGPUTextureView Context::acquire(){
    if(!surface_||width_<=0)return nullptr;
    WGPUSurfaceTexture t{};wgpuSurfaceGetCurrentTexture(surface_,&t);
    if(t.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal&&t.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal){if(t.texture)wgpuTextureRelease(t.texture);return nullptr;}
    current_=t.texture;view_=wgpuTextureCreateView(current_,nullptr);return view_;
}
void Context::present(){
    if(!current_)return;wgpuSurfacePresent(surface_);
    wgpuTextureViewRelease(view_);wgpuTextureRelease(current_);view_=nullptr;current_=nullptr;
}
}
