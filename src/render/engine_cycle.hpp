#pragma once
#include <algorithm>
#include <cmath>
namespace albert {
// Only thrust reaches the scene: the first flow is main stage building with the physics' spool, the torch's glow in the
// bell over its first 0.24 s. Joining an already established burn shows it steady; a rewind starts a fresh history. These
// clocks also date the heard transients.
struct EngineCycle {
    static constexpr double torch=.24;
    double started=-1e30,stopped=-1e30,utc=0;float previous=0,cutoff=0;bool known=false,on=false,main=false;
    bool ignition=false,shutdown=false,reset=false;
    void step(float thrust,double now){
        thrust=std::isfinite(thrust)?std::clamp(thrust,0.f,1.f):0;ignition=shutdown=false;reset=known&&now<utc;
        if(reset){*this=EngineCycle{};reset=true;}
        if(!known){known=true;on=thrust>1e-4f;if(on){started=now-(thrust>.65f?10:0);main=true;}}
        else if(thrust>1e-4f&&!on){on=main=ignition=true;started=now;}
        else if(thrust<=1e-4f&&on){on=false;stopped=now;cutoff=previous;shutdown=main;main=false;}
        previous=thrust;utc=now;
    }
    float burning()const{return on?previous*std::clamp(static_cast<float>((utc-started)/.06),0.f,1.f):0;}
    float rich()const{return on?std::clamp(1-static_cast<float>((utc-started)/.5),0.f,1.f):0;}
    bool torching()const{return on&&utc-started<torch;}
};
}
