#pragma once
#include <span>
#include <string_view>
#include <vector>

namespace albert {
// Faint ring systems of Jupiter, Uranus and Neptune (Saturn's rings use their own
// image profile). Geometry and normal optical depths from the PDS Ring-Moon Systems
// Node tables; arcs, eccentricities and inclinations are not drawn.
struct RingBand {double innerKm,outerKm,tau;bool dust;};
struct RingSystem {
    std::string_view planet;std::span<const RingBand> bands;
    float particleAlbedo; // Geometric albedo of macroscopic (dense-ring) particles.
    float dustAlbedo,dustAsymmetry; // Single-scattering albedo and Henyey-Greenstein g of dust.
};
const RingSystem* ringSystem(std::string_view planet);
// Radial profile across [inner, outer]: per texel, the band-averaged normal optical
// depth of macroscopic particles (even indices) and dust (odd indices), so rings
// narrower than a texel keep their equivalent width.
struct RingProfile {double innerKm=0,outerKm=0;std::vector<float> tau;};
RingProfile ringProfile(const RingSystem&,unsigned texels);
// Brightest I/F of the profile in a reference geometry (incidence and emission cosines
// 0.5, 90 deg scattering): the optional enhancement scales it to a visible level and
// keeps every relative brightness.
float ringReferenceReflectance(const RingSystem&,const RingProfile&);
}
