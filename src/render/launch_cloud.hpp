#pragma once
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
namespace albert {
// One continuous bank, in the pad's ENU metres. No particle sorting, history buffers or per-frame allocation.
class LaunchCloud {
public:
    LaunchCloud(threepp::Vector3 centre,threepp::Vector3 trench);
    const std::shared_ptr<threepp::Mesh>& node()const{return mesh_;}
    void update(float heat,float water,float seconds,const threepp::Vector3& wind,bool enabled);
    void quality(int value);
    void reset();
private:
    std::shared_ptr<threepp::Mesh> mesh_;threepp::Vector3 centre_,drift_;
    float age_=0,energy_=0,water_=0,idle_=0,glow_=0;
};
}
