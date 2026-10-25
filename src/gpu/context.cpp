#include "gpu/context.hpp"
#include "stb_image_write.h"
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
namespace albert::gpu {
namespace {std::atomic<unsigned> errorCount{0};thread_local std::function<void(Context&)> pendingPresent;}
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
    WGPUAdapterInfo info{};if(wgpuAdapterGetInfo(adapter_,&info)==WGPUStatus_Success){adapterName_=text(info.device);software_=info.adapterType==WGPUAdapterType_CPU;wgpuAdapterInfoFreeMembers(info);}
    WGPUDeviceDescriptor dd{};dd.label=view("Albert");
    // Linear filtering of float textures (heights, LUTs); GPU timestamps for frame statistics.
    std::vector<WGPUFeatureName> features;
    for(auto f:{WGPUFeatureName_Float32Filterable,WGPUFeatureName_TimestampQuery})if(wgpuAdapterHasFeature(adapter_,f))features.push_back(f);
    dd.requiredFeatureCount=features.size();dd.requiredFeatures=features.data();
    // Called from inside wgpu: never throw across it. Validation errors are bugs; they are counted and logged.
    dd.uncapturedErrorCallbackInfo.callback=[](const WGPUDevice*,WGPUErrorType,WGPUStringView m,void*,void*){++errorCount;std::fprintf(stderr,"WebGPU: %s\n",text(m).c_str());};
    // A lost device (a driver reset, a hung GPU) is reported with its reason; destruction at exit is not.
    dd.deviceLostCallbackInfo.mode=WGPUCallbackMode_AllowSpontaneous;
    dd.deviceLostCallbackInfo.callback=[](const WGPUDevice*,WGPUDeviceLostReason reason,WGPUStringView m,void*,void*){
        if(reason==WGPUDeviceLostReason_Destroyed||reason==WGPUDeviceLostReason_CallbackCancelled)return;
        ++errorCount;std::fprintf(stderr,"WebGPU device lost (reason %d): %s\n",static_cast<int>(reason),text(m).c_str());std::fflush(stderr);};
    WGPURequestDeviceCallbackInfo dc{};dc.mode=WGPUCallbackMode_AllowProcessEvents;dc.userdata1=&device;
    dc.callback=[](WGPURequestDeviceStatus status,WGPUDevice d,WGPUStringView m,void* u,void*){auto& r=*static_cast<Result*>(u);if(status==WGPURequestDeviceStatus_Success)r.handle=d;else r.message=text(m);};
    wgpuAdapterRequestDevice(adapter_,&dd,dc);wgpuInstanceProcessEvents(instance_);
    if(!(device_=static_cast<WGPUDevice>(device.handle)))throw std::runtime_error("No WebGPU device: "+device.message);
    queue_=wgpuDeviceGetQueue(device_);
    if(surface_){
        WGPUSurfaceCapabilities caps{};wgpuSurfaceGetCapabilities(surface_,adapter_,&caps);format_=caps.formatCount?caps.formats[0]:format_;
        copySrc_=(caps.usages&WGPUTextureUsage_CopySrc)!=0;
        // Not sRGB: shaders encode their output (tone mapping, then sRGB), so blending happens in
        // encoded space exactly as in the OpenGL renderer, and ImGui's sRGB colours pass through.
        for(std::size_t i=0;i<caps.formatCount;++i)if(caps.formats[i]==WGPUTextureFormat_BGRA8Unorm||caps.formats[i]==WGPUTextureFormat_RGBA8Unorm){format_=caps.formats[i];break;}
        for(std::size_t i=0;i<caps.presentModeCount;++i){const auto m=caps.presentModes[i];
            if(m==WGPUPresentMode_Immediate||(m==WGPUPresentMode_Mailbox&&unsynced_!=WGPUPresentMode_Immediate))unsynced_=m;}
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
    if(current_){stale_=true;return;} // An image of it held (between acquire and present): reconfigured once presented.
    WGPUSurfaceConfiguration c{};c.device=device_;c.format=format_;c.usage=WGPUTextureUsage_RenderAttachment|(copySrc_?WGPUTextureUsage_CopySrc:WGPUTextureUsage_None);c.width=static_cast<std::uint32_t>(width);c.height=static_cast<std::uint32_t>(height);
    c.alphaMode=WGPUCompositeAlphaMode_Auto;c.presentMode=vsync_?WGPUPresentMode_Fifo:unsynced_;wgpuSurfaceConfigure(surface_,&c);
}
void Context::setVsync(bool on){if(on==vsync_)return;vsync_=on;resize(width_,height_);}
WGPUTextureView Context::acquire(){
    if(!surface_||width_<=0)return nullptr;
    WGPUSurfaceTexture t{};wgpuSurfaceGetCurrentTexture(surface_,&t);
    if(t.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal&&t.status!=WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal){if(t.texture)wgpuTextureRelease(t.texture);return nullptr;}
    current_=t.texture;view_=wgpuTextureCreateView(current_,nullptr);return view_;
}
void Context::capturePng(const std::string& path){
    std::vector<unsigned char> rgba;unsigned w=0,h=0;readRgba(rgba,w,h);
    if(!stbi_write_png(path.c_str(),static_cast<int>(w),static_cast<int>(h),4,rgba.data(),static_cast<int>(w*4)))throw std::runtime_error("Could not write PNG: "+path);
}
void Context::readRgba(std::vector<unsigned char>& rgba,unsigned& width,unsigned& height){
    if(!copySrc_)throw std::runtime_error("Surface does not support CopySrc; PNG capture unavailable");
    if(!current_)throw std::runtime_error("No current surface frame to capture");
    const bool bgra=format_==WGPUTextureFormat_BGRA8Unorm||format_==WGPUTextureFormat_BGRA8UnormSrgb;
    if(!bgra&&format_!=WGPUTextureFormat_RGBA8Unorm&&format_!=WGPUTextureFormat_RGBA8UnormSrgb)throw std::runtime_error("PNG capture requires a BGRA8 or RGBA8 surface");
    const unsigned w=imageWidth(),h=imageHeight();const std::uint64_t row=std::uint64_t(w)*4,stride=(row+255)&~std::uint64_t(255),bytes=stride*h;
    if(!w||!h||row>(std::numeric_limits<int>::max)()||h>static_cast<unsigned>((std::numeric_limits<int>::max)())||bytes>(std::numeric_limits<std::size_t>::max)())throw std::runtime_error("Surface dimensions exceed PNG capture limits");
    rgba.resize(static_cast<std::size_t>(row*h));width=w;height=h;
    WGPUBufferDescriptor d{};d.size=bytes;d.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;
    struct Readback {WGPUBuffer value;~Readback(){if(value){wgpuBufferDestroy(value);wgpuBufferRelease(value);}}} buffer{wgpuDeviceCreateBuffer(device_,&d)};
    if(!buffer.value)throw std::runtime_error("Could not allocate PNG readback buffer");
    WGPUCommandEncoder encoder=wgpuDeviceCreateCommandEncoder(device_,nullptr);if(!encoder)throw std::runtime_error("Could not create PNG copy encoder");
    WGPUTexelCopyTextureInfo source{};source.texture=current_;source.aspect=WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo target{};target.buffer=buffer.value;target.layout.bytesPerRow=static_cast<std::uint32_t>(stride);target.layout.rowsPerImage=h;const WGPUExtent3D size{w,h,1};
    // Texture copies pad rows to 256 bytes; PNG rows have no padding.
    wgpuCommandEncoderCopyTextureToBuffer(encoder,&source,&target,&size);
    WGPUCommandBuffer commands=wgpuCommandEncoderFinish(encoder,nullptr);wgpuCommandEncoderRelease(encoder);
    if(!commands)throw std::runtime_error("Could not finish PNG copy commands");
    wgpuQueueSubmit(queue_,1,&commands);wgpuCommandBufferRelease(commands);
    struct Mapping {bool done=false;WGPUMapAsyncStatus status{};std::string message;} mapping;
    WGPUBufferMapCallbackInfo cb{};cb.mode=WGPUCallbackMode_AllowProcessEvents;cb.userdata1=&mapping;
    cb.callback=[](WGPUMapAsyncStatus status,WGPUStringView message,void* u,void*){auto& m=*static_cast<Mapping*>(u);m.status=status;m.message=text(message);m.done=true;};
    wgpuBufferMapAsync(buffer.value,WGPUMapMode_Read,0,static_cast<std::size_t>(bytes),cb);
    while(!mapping.done){wgpuDevicePoll(device_,true,nullptr);wgpuInstanceProcessEvents(instance_);}
    if(mapping.status!=WGPUMapAsyncStatus_Success)throw std::runtime_error("PNG readback mapping failed: "+mapping.message);
    const auto* pixels=static_cast<const unsigned char*>(wgpuBufferGetConstMappedRange(buffer.value,0,static_cast<std::size_t>(bytes)));
    if(!pixels)throw std::runtime_error("PNG readback returned no mapped data");
    for(unsigned y=0;y<h;++y)std::memcpy(rgba.data()+static_cast<std::size_t>(y*row),pixels+static_cast<std::size_t>(y*stride),static_cast<std::size_t>(row));
    wgpuBufferUnmap(buffer.value);
    if(bgra)for(std::size_t i=0;i<rgba.size();i+=4)std::swap(rgba[i],rgba[i+2]);
}
void Context::present(){
    if(!current_)return;
    auto work=std::exchange(pendingPresent,{});if(work)work(*this);
    wgpuSurfacePresent(surface_);
    wgpuTextureViewRelease(view_);wgpuTextureRelease(current_);view_=nullptr;current_=nullptr;
    if(stale_){stale_=false;resize(width_,height_);}
}
void Context::beforePresent(std::function<void(Context&)> work){pendingPresent=std::move(work);}
unsigned Context::imageWidth()const{return current_?wgpuTextureGetWidth(current_):static_cast<unsigned>(width_);}
unsigned Context::imageHeight()const{return current_?wgpuTextureGetHeight(current_):static_cast<unsigned>(height_);}
}
