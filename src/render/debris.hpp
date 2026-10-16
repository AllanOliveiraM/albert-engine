#pragma once
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <array>
namespace albert {
// Statistical visual populations. Not individual ephemerides/pickable bodies.
class DebrisFields {
public:
    explicit DebrisFields(threepp::Scene&);
    ~DebrisFields();
    void update(const threepp::Vector3& sun,const threepp::Vector3& camera,bool enabled,float seconds);
private:
    std::array<std::shared_ptr<threepp::Points>,3> nodes_;
    std::array<std::shared_ptr<threepp::ShaderMaterial>,3> materials_;
    std::array<float,3> opacity_{};
};
}
