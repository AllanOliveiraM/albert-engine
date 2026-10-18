#pragma once
#include <threepp/input/PeripheralsEventSource.hpp>
#include <functional>
#include <string>
#include <vector>
struct GLFWwindow;
namespace albert::gpu {
// GLFW window without a graphics API context (WebGPU draws into it) that feeds threepp's input listeners (OrbitControls)
// as threepp::Canvas does: mouse buttons, cursor, wheel and resizes. Keys go through ImGui.
class Window:public threepp::PeripheralsEventSource {
public:
    Window(const std::string& title,int width,int height);
    ~Window() override;
    Window(const Window&)=delete;Window& operator=(const Window&)=delete;
    [[nodiscard]] threepp::WindowSize size()const override{return size_;}
    [[nodiscard]] float aspect()const{return size_.height()>0?static_cast<float>(size_.width())/static_cast<float>(size_.height()):1.f;}
    [[nodiscard]] GLFWwindow* handle()const{return window_;}
    void onResize(std::function<void(threepp::WindowSize)> f){resize_.push_back(std::move(f));}
    [[nodiscard]] bool isOpen()const;
    void close();
    void poll(); // Dispatches pending window events.
private:
    static void mouse(GLFWwindow*,int button,int action,int mods);
    static void cursor(GLFWwindow*,double x,double y);
    static void scroll(GLFWwindow*,double x,double y);
    static void resized(GLFWwindow*,int width,int height);
    GLFWwindow* window_=nullptr;threepp::WindowSize size_;threepp::Vector2 last_;std::vector<std::function<void(threepp::WindowSize)>> resize_;
};
}
