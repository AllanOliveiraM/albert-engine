#pragma once
#include "gpu/context.hpp"
#include <memory>
namespace threepp {class Scene;class Camera;}
namespace albert::gpu {
// Draws a threepp scene with WebGPU, as threepp's GLRenderer would: frustum culling, opaque then
// transparent objects sorted by renderOrder and depth, threepp blending, depth and face state,
// one directional and one ambient light. Reversed-Z infinite projection on depth32float, so the
// camera's far plane is unused. Meshes and Points (instanced quads) with built-in Basic,
// Standard and Phong materials or ShaderMaterials named after a defined Program; anything else
// is skipped and reported once.
class SceneRenderer {
public:
    explicit SceneRenderer(Context& context);
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&)=delete;SceneRenderer& operator=(const SceneRenderer&)=delete;
    float exposure=1;
    // Clears target (scene background) and depth, then draws, recording into encoder.
    void render(threepp::Scene& scene,threepp::Camera& camera,WGPUTextureView target,unsigned width,unsigned height,WGPUCommandEncoder encoder);
    [[nodiscard]] unsigned drawn()const; // Draw calls in the last render.
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
