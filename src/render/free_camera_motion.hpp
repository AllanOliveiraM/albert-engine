#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>

namespace albert {
class RelativeMouseMotion {
public:
    void reset(){ready_=false;}
    std::array<double,2> sample(double x,double y){
        if(!std::isfinite(x)||!std::isfinite(y)){reset();return {};}
        const std::array<double,2> delta=ready_?std::array<double,2>{x-last_[0],y-last_[1]}:std::array<double,2>{};
        last_={x,y};ready_=true;return delta;
    }
private:
    bool ready_=false;
    std::array<double,2> last_{};
};
// Metres and wall-clock seconds, in the rotating body's orthonormal frame
// (or inertial axes in space). Position rebasing must not rotate momentum.
class FreeCameraMotion {
public:
    using Vector=std::array<double,3>;
    void reset(){velocity_={};look_={};reference_.clear();}
    void reference(std::string_view id){if(reference_!=id){reset();reference_=id;}}
    const Vector& velocity()const{return velocity_;}
    Vector move(const Vector& target,double seconds){
        if(!std::isfinite(seconds)||seconds<=0)return {};
        const double dt=std::min(seconds,.1);
        const bool moving=std::hypot(target[0],target[1],target[2])>0;
        const double tau=moving?.20:.14,blend=-std::expm1(-dt/tau);
        Vector displacement;
        for(int i=0;i<3;++i){
            const double previous=velocity_[i];
            displacement[i]=target[i]*dt+(previous-target[i])*tau*blend;
            velocity_[i]=previous+(target[i]-previous)*blend;
        }
        if(!moving&&std::hypot(velocity_[0],velocity_[1],velocity_[2])<.001)velocity_={};
        return displacement;
    }
    std::array<double,2> look(double yaw,double pitch,double seconds){
        look_[0]+=yaw;look_[1]+=pitch;
        if(!std::isfinite(seconds)||seconds<=0)return {};
        const double blend=-std::expm1(-std::min(seconds,.1)/.035);
        std::array<double,2> turn;
        for(int i=0;i<2;++i){turn[i]=look_[i]*blend;look_[i]-=turn[i];if(std::abs(look_[i])<1e-7)look_[i]=0;}
        return turn;
    }
    void stopInto(const Vector& normal){
        double inward=0;for(int i=0;i<3;++i)inward+=velocity_[i]*normal[i];
        if(inward<0)for(int i=0;i<3;++i)velocity_[i]-=inward*normal[i];
    }
private:
    Vector velocity_{};
    std::array<double,2> look_{};
    std::string reference_;
};
}
