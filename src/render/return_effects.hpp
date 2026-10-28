#pragma once
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <array>
namespace albert {
// Three pooled shells: rarefied exhaust, atmospheric start/recirculation, and cold gas/vapour.
class ReturnEffects {
public:
    explicit ReturnEffects(const core::VehicleLook& look,threepp::Object3D& parent);
    void quality(int value);
    void update(double utc,float pressure,float mach,float fill,const threepp::Vector3& upwind,float flow,float flash,
        const threepp::Vector3& source,float humidity,const std::array<threepp::Vector3,6>& tips,unsigned tipCount);
private:
    std::array<std::shared_ptr<threepp::Mesh>,3> shells_;
    float radius_=1,length_=1,shoulder_=0,previousFill_=0,previousPressure_=0,demand_=0;double utc_=0;bool timed_=false,burned_=false;int quality_=1;
};
}
