#pragma once
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
namespace albert {
// One sea-frame volume and one wave-following foam mesh per vehicle, all in ENU metres.
// Motion is observed, never integrated back into the flight.
class Splashdown {
public:
    Splashdown(float length,float radius);
    const std::shared_ptr<threepp::Mesh>& node()const{return root_;}
    void update(threepp::Vector3 centre,threepp::Vector3 nose,float thrust,bool contact,double utc,
                const threepp::Vector3& wind,bool enabled=true);
    void quality(int value);
    void reset();
private:
    std::shared_ptr<threepp::Mesh> root_,volume_,foam_;
    float length_,radius_,energy_=0,age_=0,idle_=0,glow_=0,previousLow_=0,impact_=0,burstAge_=1000,foamAge_=0;
    double utc_=0;bool timed_=false,touched_=false,tipped_=false;
    threepp::Vector3 source_,drift_,impactPosition_;
};
}
