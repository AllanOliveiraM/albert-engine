#pragma once
#include "gpu/context.hpp"
#include "threepp/math/Vector3.hpp"
#include <memory>
namespace threepp {class Texture;class Scene;class Camera;class Object3D;}
namespace albert::gpu {
class Atmosphere;class Ocean;class Clouds;
// Draws a threepp scene with WebGPU, as threepp's GLRenderer would: frustum culling, opaque then
// transparent objects sorted by renderOrder and depth, threepp blending, depth and face state,
// one directional and one ambient light. Reversed Z on depth32float: infinite for perspective
// cameras (their far plane is unused), near-to-far for orthographic ones. Meshes and Points (instanced quads) with built-in Basic,
// Standard and Phong materials or ShaderMaterials named after a defined Program; anything else
// is skipped and reported once.
class SceneRenderer {
public:
    explicit SceneRenderer(Context& context);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&)=delete;SceneRenderer& operator=(const SceneRenderer&)=delete;
    float exposure=1;
    bool toneMapping=true; // ACES filmic (threepp's ACESFilmic); false: none, as threepp's default.
    bool shadows=false; // Sun shadow map for castShadow meshes on receiveShadow ones.
    unsigned samples=4; // Multisampling: 4 (resolved into the target) or 1.
    bool measured=true; // GPU timing of the render (off for a second view).
    // The air this frame: aerial perspective, sunlight and sky light through `atmosphere` around
    // `body`, whose local frame times kmPerUnit is the atmosphere's planet frame; strength 0-1. Its
    // precomputation advances `airSteps` dispatches a frame and the air shows once it is complete.
    // groundKm (<= 0): the model's ground lowered to the terrain under the camera where that lies below
    // the body's reference surface (the Cape: ~-0.027), so the eye never sits under the model's ground.
    // clouds: the cloud layer's map (density in green, u and v as the globe's), cloudKm above the
    // air's ground, cloudShadow its opacity: sunlight crossing it there is dimmed (no clouds: null).
    struct Air {Atmosphere* atmosphere=nullptr;const threepp::Object3D* body=nullptr;float kmPerUnit=1,strength=0,groundKm=0;
        threepp::Texture* clouds=nullptr;float cloudKm=12,cloudShadow=0;
        float surfaceWind=0; // m/s: water (texels under 0.5 in the roughness maps of materials named "albert:water") takes Cox-Munk's slopes (0: the maps').
        float polarRatio=1;} air; // The body's polar/equatorial axis ratio: the layer's map is in geodetic latitude.
    unsigned airSteps=4;
    // Earthshine on lit materials (earthLight in the photometry module): the Earth and its radius in world units (0: none).
    struct Earthshine {const threepp::Object3D* earth=nullptr;float radius=0;} earthshine;
    // Wind waves on water materials (seaAt in the photometry module, with the air): the simulation, the wind (m/s toward east
    // and north) and the waves' clock (s); the pattern's anchor and east/north axes (world) and metres per world unit.
    struct Sea {Ocean* ocean=nullptr;bool enabled=false;double seconds=0;float east=0,north=0,metersPerUnit=6378137;threepp::Vector3 anchor,eastAxis,northAxis;} sea;
    // Volumetric clouds (gpu::Clouds) on the air's cloud map (air.clouds, with air.cloudShadow > 0), marched before the pass
    // and laid over the opaque objects (gpu::Clouds::View; divisor 0: the march's resolution kept near 300 000 rays, 2 to 4);
    // wind: m/s toward east and north (the noise's drift), seconds its clock.
    struct Volume {bool enabled=false;float strength=1,baseKm=1.5f,topKm=5.5f,extinction=75,windEast=0,windNorth=0;double seconds=0;unsigned divisor=2,steps=64;bool flat=false;} volume;
    [[nodiscard]] const Clouds* clouds()const; // The last march's (for tests); null before the first.
    [[nodiscard]] bool cloudsShown()const;     // The last render marched them (enabled, the air's tables ready, a cloud map).
    // GPU time (gpuMilliseconds) spans the clouds' march, when there is one, the sun's shadow and the scene pass.
    // Clears target (scene background) and depth, then draws, recording into encoder. Uniforms are
    // written through the queue: submit this encoder before the next render().
    void render(threepp::Scene& scene,threepp::Camera& camera,WGPUTextureView target,unsigned width,unsigned height,WGPUCommandEncoder encoder);
    [[nodiscard]] unsigned drawn()const; // Draw calls in the last render.
    [[nodiscard]] std::size_t vertexBytes()const; // Vertex buffers resident on the GPU.
    [[nodiscard]] double gpuMilliseconds()const; // GPU time of a recent scene pass (timestamp queries), or -1; see cloudsShown.
    void submitted(); // Call once the encoder given to render() has been submitted.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
