#include "gpu/window.hpp"
#include <GLFW/glfw3.h>
#include <stdexcept>
namespace albert::gpu {
Window::Window(const std::string& title,int width,int height){
    if(!glfwInit())throw std::runtime_error("GLFW unavailable");
    glfwWindowHint(GLFW_CLIENT_API,GLFW_NO_API);glfwWindowHint(GLFW_SCALE_TO_MONITOR,GLFW_TRUE);
    if(!(window_=glfwCreateWindow(width,height,title.c_str(),nullptr,nullptr)))throw std::runtime_error("Window creation failed");
    glfwDefaultWindowHints();int w=0,h=0;glfwGetWindowSize(window_,&w,&h);size_={w,h};
    glfwSetWindowUserPointer(window_,this);
    glfwSetMouseButtonCallback(window_,mouse);glfwSetCursorPosCallback(window_,cursor);glfwSetScrollCallback(window_,scroll);glfwSetWindowSizeCallback(window_,resized);
}
Window::~Window(){if(window_)glfwDestroyWindow(window_);}
bool Window::isOpen()const{return !glfwWindowShouldClose(window_);}
void Window::close(){glfwSetWindowShouldClose(window_,GLFW_TRUE);}
void Window::poll(){glfwPollEvents();}
void Window::mouse(GLFWwindow* w,int button,int action,int){
    auto& self=*static_cast<Window*>(glfwGetWindowUserPointer(w));
    if(action==GLFW_PRESS||action==GLFW_RELEASE)self.onMousePressedEvent(button,self.last_,action==GLFW_PRESS?MouseAction::PRESS:MouseAction::RELEASE);
}
void Window::cursor(GLFWwindow* w,double x,double y){
    auto& self=*static_cast<Window*>(glfwGetWindowUserPointer(w));const threepp::Vector2 p(static_cast<float>(x),static_cast<float>(y));
    self.onMouseMoveEvent(p);self.last_.copy(p);
}
void Window::scroll(GLFWwindow* w,double x,double y){static_cast<Window*>(glfwGetWindowUserPointer(w))->onMouseWheelEvent({static_cast<float>(x),static_cast<float>(y)});}
void Window::resized(GLFWwindow* w,int width,int height){
    auto& self=*static_cast<Window*>(glfwGetWindowUserPointer(w));self.size_={width,height};for(const auto& f:self.resize_)f(self.size_);
}
}
