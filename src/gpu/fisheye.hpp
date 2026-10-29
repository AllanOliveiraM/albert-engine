#pragma once
#include "gpu/context.hpp"
namespace albert::gpu {
// Equidistant 150 degree diagonal lens, resampling a 145 degree vertical perspective image.
// Kept before ImGui so menus, telemetry and inset views retain their normal geometry.
class Fisheye {
public:
    explicit Fisheye(Context&,bool lens=true); // lens false: a plain bilinear resample (the scene drawn at a lower resolution).
    ~Fisheye();
    WGPUTextureView source(unsigned width,unsigned height);
    void draw(WGPUCommandEncoder,WGPUTextureView target);
private:
    Context& context_;WGPURenderPipeline pipeline_=nullptr;WGPUBindGroupLayout layout_=nullptr;
    WGPUTexture texture_=nullptr;WGPUTextureView view_=nullptr;WGPUSampler sampler_=nullptr;WGPUBindGroup group_=nullptr;
    unsigned width_=0,height_=0;
};
}
