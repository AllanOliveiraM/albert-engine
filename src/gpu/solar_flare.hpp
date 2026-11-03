#pragma once
#include "gpu/context.hpp"
#include <array>

namespace albert::gpu {
// A small photographic approximation, not a calibrated lens prescription.
// Measure the resolved Sun against opaque depth on the GPU, then add weak
// ghosts before resampling/fisheye and before ImGui. No readback or image-sized targets.
class SolarFlare {
public:
    SolarFlare(Context&,WGPUBindGroupLayout frameLayout);
    ~SolarFlare();
    SolarFlare(const SolarFlare&)=delete;SolarFlare& operator=(const SolarFlare&)=delete;
    void measure(WGPUCommandEncoder,WGPUBindGroup frame,WGPUTextureView depth,unsigned samples,
                 WGPUTextureView clouds,WGPUSampler,const std::array<float,12>& parameters,const WGPUPassTimestampWrites* timing=nullptr);
    void draw(WGPUCommandEncoder,WGPUTextureView target,const WGPUPassTimestampWrites* timing=nullptr);
private:
    Context& gpu_;
    WGPUBuffer parameters_=nullptr,signal_=nullptr;
    WGPUBindGroupLayout measureLayouts_[2]{},drawLayout_=nullptr;
    WGPUComputePipeline measurePipelines_[2]{};
    WGPURenderPipeline drawPipeline_=nullptr;
    WGPUBindGroup drawGroup_=nullptr;
};
}
