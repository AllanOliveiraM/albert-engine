#pragma once
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/input/PeripheralsEventSource.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
namespace threepp {class Scene;class Camera;class GLRenderer;class Object3D;class Texture;}
namespace albert {
// Window, scene renderer and ImGui for one graphics backend: WebGPU (src/gpu, the default) or
// threepp's OpenGL renderer (--renderer=opengl, and the fallback without a WebGPU adapter). `draw` draws the
// ImGui windows each frame.
class Display {
public:
    enum class Backend {opengl,webgpu};
    Display(Backend backend,const std::string& title,threepp::WindowSize size,const std::filesystem::path& icon,std::function<void()> draw);
    ~Display();
    Display(const Display&)=delete;Display& operator=(const Display&)=delete;
    [[nodiscard]] Backend backend()const;
    threepp::PeripheralsEventSource& events();
    threepp::GLRenderer* gl(); // OpenGL only (texture uploads, shadow maps); null on WebGPU.
    [[nodiscard]] bool isOpen()const;
    void close();
    [[nodiscard]] float aspect()const;
    void onResize(std::function<void(threepp::WindowSize)> f);
    bool frame(const std::function<void()>& work); // Polls input, runs work, presents; false once closing.
    void clear();                                  // In work: background only (loading screen).
    void render(threepp::Scene& scene,threepp::Camera& camera); // In work.
    void ui();                                     // In work, after the scene.
    void setExposure(float value);
    void setShadows(bool enabled); // Sun shadow map (OpenGL: the renderer's; WebGPU: every frame).
    [[nodiscard]] float exposure()const;
    // WebGPU: Earth's air around `body` (local frame in units of kmPerUnit km) at strength 0-1 —
    // sky, aerial perspective, sunlight and sky light; its tables build over the first frames.
    // OpenGL: no effect (the atmosphere shell's own shader).
    // The air this frame (WebGPU): the Earth's, or Mars's when `mars`; groundKm as SceneRenderer::Air.
    void setAir(const threepp::Object3D* body,float kmPerUnit,float strength,float groundKm=0,bool mars=false);
    void setClouds(threepp::Texture* map,float altitudeKm,float opacity); // Their shadows (WebGPU; SceneRenderer::Air).
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
