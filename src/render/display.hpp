#pragma once
#include "threepp/canvas/WindowSize.hpp"
#include "threepp/input/PeripheralsEventSource.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
namespace threepp {class Scene;class Camera;class Object3D;class Texture;}
namespace albert {
// Window, WebGPU scene renderer (src/gpu) and ImGui. `draw` draws the ImGui windows each frame.
// Throws when no WebGPU adapter exists, not even a software one.
class Display {
public:
    Display(const std::string& title,threepp::WindowSize size,const std::filesystem::path& icon,std::function<void()> draw);
    ~Display();
    Display(const Display&)=delete;Display& operator=(const Display&)=delete;
    threepp::PeripheralsEventSource& events();
    [[nodiscard]] bool isOpen()const;
    void close();
    [[nodiscard]] float aspect()const;
    void onResize(std::function<void(threepp::WindowSize)> f);
    bool frame(const std::function<void()>& work); // Polls input, runs work, presents; false once closing.
    void clear();                                  // In work: background only (loading screen).
    void render(threepp::Scene& scene,threepp::Camera& camera); // In work.
    void ui();                                     // In work, after the scene.
    void setExposure(float value);
    void setShadows(bool enabled); // Sun shadow map, every frame.
    [[nodiscard]] float exposure()const;
    // The air this frame around `body` (local frame in units of kmPerUnit km) at strength 0-1 — sky,
    // aerial perspective, sunlight and sky light: the Earth's, or Mars's when `mars`; its tables
    // build over the first frames; groundKm as SceneRenderer::Air.
    void setAir(const threepp::Object3D* body,float kmPerUnit,float strength,float groundKm=0,bool mars=false);
    // The weather the air shows (SceneRenderer::Air): cloud shadows, the sea's wind roughness.
    void setWeather(threepp::Texture* clouds,float cloudKm,float cloudOpacity,float surfaceWind,float polarRatio=1);
    void setEarthshine(const threepp::Object3D* earth,float radius); // Radius in world units; 0 or null: none.
    void setSamples(unsigned samples); // Multisampling of the scene pass: 1 or 4.
    void setVsync(bool on);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
