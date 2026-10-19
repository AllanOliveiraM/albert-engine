#pragma once
#include "gpu/context.hpp"
#include <array>
namespace albert::gpu {
// Deep-water wind waves by Tessendorf's method (Simulating Ocean Water, 2001) on the GPU: the Pierson-Moskowitz spectrum
// of the 10 m wind (U19.5 = 1.026 U10, peak 0.877 g/U19.5, α 0.0081) spread as (2/π) cos² about the wind, in three
// periodic cascades of 256² (512, 128 and 32 m; each keeps its own band of wavenumbers, split at six of the next one's
// fundamentals), advanced by ω² = g k and brought to space by inverse FFTs. Per cascade (a layer of each array): the
// displacement (east, up, north; m) and the derivatives ∂η/∂east, ∂η/∂north, ∂Dx/∂east, ∂Dz/∂north.
class Ocean {
public:
    static constexpr unsigned size=256,cascades=3;
    static constexpr std::array<float,cascades> patches{512,128,32}; // m.
    explicit Ocean(Context& context);
    ~Ocean();
    Ocean(const Ocean&)=delete;Ocean& operator=(const Ocean&)=delete;
    // Records this instant's fields (and a new spectrum first when the wind moved by 0.25 m/s or 3°). wind: m/s toward
    // east and north; choppiness scales the horizontal displacement (1: Tessendorf's).
    void update(WGPUCommandEncoder encoder,double seconds,float towardEast,float towardNorth,float choppiness=1);
    WGPUTextureView displacement()const{return views_[0];}
    WGPUTextureView derivatives()const{return views_[1];}
    WGPUTexture displacementTexture()const{return textures_[0];} // For readback.
    WGPUTexture derivativesTexture()const{return textures_[1];}
    // The mean square slope (both components) of the waves the cascades resolve, for the shading's unresolved remainder.
    static double resolvedSlope(double windSpeed);
    static double bandSlope(double windSpeed,unsigned cascade); // The part in one cascade's band.
    static double significantHeight(double windSpeed); // Pierson-Moskowitz, fully developed: 4 √m0 (m).
private:
    struct Impl;Impl* impl_=nullptr;std::array<WGPUTexture,2> textures_{};std::array<WGPUTextureView,2> views_{};
};
}
