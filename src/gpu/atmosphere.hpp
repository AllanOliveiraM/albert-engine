#pragma once
#include "gpu/context.hpp"
#include <array>
#include <memory>
namespace albert::gpu {
// Precomputed atmospheric scattering of Bruneton (2017 reference implementation, BSD-3), computed
// on the GPU at 680, 550 and 440 nm for a white solar irradiance of 1: renderers multiply by the
// sun's colour. Lengths in km from the planet's centre; coefficients per km.
struct DensityLayer {float width=0,expTerm=0,expScale=0,linearTerm=0,constantTerm=0;};
struct DensityProfile {DensityLayer lower,upper;}; // `lower` below its width, `upper` above.
struct AtmosphereModel {
    float bottom=0,top=0;
    std::array<float,3> rayleigh{},mieScattering{},mieExtinction{},absorption{},groundAlbedo{};
    DensityProfile rayleighDensity,mieDensity,absorptionDensity;
    float mieG=0,sunAngularRadius=0,maxSunZenith=0; // Radians.
    int orders=4; // Scattering orders.
};
// Bruneton's Earth (Rayleigh, 300 DU of ozone) with clear-day aerosols (optical depth 0.1 at
// 550 nm), on the WGS84 equatorial radius, 80 km deep.
AtmosphereModel earthAtmosphere();
// Mars: CO2 Rayleigh (610 Pa and 11.1 km scale height at the datum) and dust of optical depth 0.5
// above the datum, absorbing toward blue; the model's ground 8.2 km below the equatorial radius (under
// Hellas), so every surface is in its air and basins get denser air than Olympus Mons.
AtmosphereModel marsAtmosphere();
class Atmosphere {
public:
    static constexpr unsigned parameterBytes=240; // The WGSL AtmosphereParameters uniform.
    Atmosphere(Context& context,const AtmosphereModel& model); // Plans the precomputation (nothing runs yet).
    ~Atmosphere();
    Atmosphere(const Atmosphere&)=delete;Atmosphere& operator=(const Atmosphere&)=delete;
    WGPUBuffer parameters()const{return parameters_;}
    WGPUTextureView transmittance()const{return views_[0];} // 2D, (r, mu).
    WGPUTextureView scattering()const{return views_[1];}    // 3D, Rayleigh and multiple scattering.
    WGPUTextureView mie()const{return views_[2];}           // 3D, single Mie scattering.
    WGPUTextureView irradiance()const{return views_[3];}    // 2D, sky irradiance (r, mu_s).
    WGPUTexture table(unsigned i)const{return textures_[i];} // In the order above (for readback).
    const AtmosphereModel& model()const{return model_;}
    // Records up to `steps` precomputation dispatches into `encoder` (one 3D slice or one 2D pass
    // each, ~230 for 4 orders); true once everything has been recorded. The tables are valid when the
    // commands carrying the last step have run.
    bool advance(WGPUCommandEncoder encoder,unsigned steps);
    void precompute(Context& context); // All remaining steps in one submission.
    [[nodiscard]] bool ready()const;
private:
    struct Plan;
    AtmosphereModel model_;WGPUBuffer parameters_=nullptr;std::array<WGPUTexture,4> textures_{};std::array<WGPUTextureView,4> views_{};std::unique_ptr<Plan> plan_;
};
// WGSL: AtmosphereParameters, the LUT sizes and Bruneton's functions, with textures and samplers as
// arguments. The including module declares `atmosphere: AtmosphereParameters`.
extern const char* const atmosphereWgsl;
}
