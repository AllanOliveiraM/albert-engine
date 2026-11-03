#include "free_camera_motion.hpp"
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
double travel(int fps){
    albert::FreeCameraMotion motion;double distance=0;
    for(int i=0;i<fps;++i)distance+=motion.move({10,0,0},1./fps)[0];
    const double full=motion.velocity()[0];
    require(full>9.9&&full<10,"Camera did not accelerate toward the requested speed");
    const double brake=motion.move({},1./fps)[0];distance+=brake;
    require(brake>0&&motion.velocity()[0]<full,"Camera stopped instantly instead of coasting");
    for(int i=1;i<fps*2;++i)distance+=motion.move({},1./fps)[0];
    require(motion.velocity()[0]==0&&motion.move({},1./fps)[0]==0,"Released camera keeps drifting");
    return distance;
}
}
int main(){try{
    albert::RelativeMouseMotion mouse;
    require(mouse.sample(1200,800)==std::array<double,2>{},"Capturing the pointer produced an orientation jump");
    require(mouse.sample(1207,796)==std::array<double,2>{7,-4},"Relative pointer direction or distance changed");
    mouse.reset();require(mouse.sample(1e9,1e9)==std::array<double,2>{},"Recapturing retained the old cursor origin");
    require(mouse.sample(1e9+3,1e9)==std::array<double,2>{3,0},"Recaptured pointer lost relative motion");
    const double reference=travel(60);
    for(int fps:{30,144})require(std::abs(travel(fps)-reference)<.0002,"Inertial travel depends on frame rate");
    albert::FreeCameraMotion motion;motion.reference("earth");
    for(int i=0;i<60;++i)motion.move({10,-5,0},1./60);
    motion.reference("earth");require(motion.velocity()[0]>9,"Rebasing the same body lost momentum");
    motion.stopInto({0,1,0});require(motion.velocity()[0]>9&&std::abs(motion.velocity()[1])<1e-12,"Ground contact did not preserve tangential motion");
    motion.move({-10,0,0},.01);require(motion.velocity()[0]>0,"Direction reversal snapped instantly");
    motion.reference("moon");require(motion.velocity()==albert::FreeCameraMotion::Vector{},"Changing bodies retained an unrelated velocity");
    motion.move({5,0,0},.1);motion.reset();require(motion.move({},.1)==albert::FreeCameraMotion::Vector{},"Camera cut or UI focus left residual movement");
    for(int fps:{30,60,144}){
        albert::FreeCameraMotion look;double angle=0;
        const auto initial=look.look(.1,0,1./fps);angle+=initial[0];
        require(initial[0]>0&&initial[0]<.1,"Mouse look did not ease its input");
        for(int i=1;i<fps;++i)angle+=look.look(0,0,1./fps)[0];
        require(std::abs(angle-.1)<2e-7,"Mouse inertia changed the requested total angle");
        for(double sign:{-1.,1.}){
            albert::FreeCameraMotion keys;double yaw=0;
            for(int i=0;i<fps;++i)yaw+=keys.look(sign*1.2/fps,0,1./fps)[0];
            for(int i=0;i<fps;++i)yaw+=keys.look(0,0,1./fps)[0];
            require(std::abs(yaw-sign*1.2)<2e-6,"Q/E yaw depends on frame rate or changes direction");
        }
    }
    std::puts("Free camera: acceleration, braking, reversal, frame-rate independence, reference changes, ground contact and mouse inertia OK");
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
