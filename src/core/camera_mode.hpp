#pragma once
#include <array>
#include <optional>
#include <string_view>
namespace albert::core {
enum class CameraMode {Orbit,Tracking,Drone,Tower,Onboard,Engine,Hotstage,Flap,TowerFisheye,OnboardFisheye,EngineFisheye,HotstageFisheye,FlapFisheye,Auto};
inline constexpr std::array<std::string_view,14> cameraModes{"orbit","tracking","drone","tower","onboard","engine","hotstage","flap","tower-fisheye","onboard-fisheye","engine-fisheye","hotstage-fisheye","flap-fisheye","auto"};
inline std::optional<CameraMode> cameraMode(std::string_view name){
    for(std::size_t i=0;i<cameraModes.size();++i)if(cameraModes[i]==name)return static_cast<CameraMode>(i);
    return {};
}
inline std::string_view cameraName(CameraMode mode){return cameraModes[static_cast<std::size_t>(mode)];}
}
