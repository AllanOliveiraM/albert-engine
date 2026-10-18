#pragma once
#include "gpu/context.hpp"
#include <memory>
namespace threepp {class Scene;class Camera;class Object3D;}
namespace albert::gpu {
class Atmosphere;
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
    // The air this frame: aerial perspective, sunlight and sky light through `atmosphere` around
    // `body`, whose local frame times kmPerUnit is the atmosphere's planet frame; strength 0-1. Its
    // precomputation advances `airSteps` dispatches a frame and the air shows once it is complete.
    struct Air {Atmosphere* atmosphere=nullptr;const threepp::Object3D* body=nullptr;float kmPerUnit=1,strength=0;} air;
    unsigned airSteps=4;
    // Clears target (scene background) and depth, then draws, recording into encoder. Uniforms are
    // written through the queue: submit this encoder before the next render().
    void render(threepp::Scene& scene,threepp::Camera& camera,WGPUTextureView target,unsigned width,unsigned height,WGPUCommandEncoder encoder);
    [[nodiscard]] unsigned drawn()const; // Draw calls in the last render.
    [[nodiscard]] std::size_t vertexBytes()const; // Vertex buffers resident on the GPU.
    [[nodiscard]] double gpuMilliseconds()const; // GPU time of a recent scene pass (timestamp queries), or -1.
    void submitted(); // Call once the encoder given to render() has been submitted.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
