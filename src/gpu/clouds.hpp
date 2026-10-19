#pragma once
#include "gpu/context.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
namespace albert::gpu {
// Volumetric clouds in the air's planet frame (km), ray-marched in compute at a fraction of the resolution and laid over the
// opaque scene by a full-screen draw at their transmittance-weighted depth (nearer terrain and vehicles hide them).
// The layer: the live cloud map's cover (frame binding 9, as the cloud shadows read it) between a base and a top over the
// air's ground that rises with the cover, carved by periodic Perlin-Worley noise; extinction from the droplets
// (σ = 3 LWC / (2 ρw re): 0.5 g/m³ and 10 µm give 75 /km). Light: single scattering of the sun through the air (Bruneton's
// transmittance) and the cloud above (a five-step march) on a two-lobe Henyey-Greenstein phase (g 0.85); the rest, the
// diffuse field, from the δ-Eddington solution for the local column (Joseph, Wiscombe and Weinman 1976: conservative,
// lit by the sun and the sky's irradiance on its top), seen through the δ-scaled extinction (1 - g²) σ; the air between the
// eye and the clouds over them.
class Clouds {
public:
    struct View {
        unsigned width=0,height=0;     // The scene target (pixels).
        unsigned divisor=2,steps=64;   // The march runs at width/divisor × height/divisor with this many steps a ray.
        float strength=1;              // 0-1: the layer's opacity over the scene (cross-fades it with the cloud shell).
        float baseKm=1.5f,topKm=5.5f;  // Over the air's ground: the base, and the top under full cover.
        float extinction=75;           // Per km at full density.
        std::array<float,3> drift{};   // The noise's offset (km, planet frame), wrapped to its period (tile).
        bool flat=false;               // A uniform slab between base and top at the map's cover, without noise (validation).
    };
    static constexpr float tile=12; // The shape noise's period (km); the detail's divides it.
    struct Noise {std::vector<std::uint8_t> shape,detail;}; // 64³ and 32³ texels.
    static Noise noise(); // The periodic noise, on the CPU (~0.5 s): made off the frame's thread.
    Clouds(Context& context,WGPUBindGroupLayout frameLayout,const Noise& noise);
    ~Clouds();
    Clouds(const Clouds&)=delete;Clouds& operator=(const Clouds&)=delete;
    // Records the march into `encoder`: `frame` is the scene's frame group (uniforms, air tables, cloud map), already written;
    // timing: timestamps for the march's compute pass (optional).
    void march(WGPUCommandEncoder encoder,WGPUBindGroup frame,const View& view,const WGPUPassTimestampWrites* timing=nullptr);
    // Draws the last march into the scene pass (after the opaque objects): its colour format and multisampling. Where they are
    // mostly opaque (alpha 0.5 and up) they also write their depth, hiding the transparent objects drawn after them.
    void composite(WGPURenderPassEncoder pass,WGPUTextureFormat format,unsigned samples);
    WGPUTexture colour()const; // rgba16f: radiance over the scene (linear, sun-coloured), view transmittance.
    WGPUTexture depth()const;  // r32f: the clouds' view depth (world units; 1e30 where there are none).
    static const char* const wgsl; // The march's functions (after the prelude, atmosphere and air modules).
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
