#pragma once
#include "threepp/objects/Group.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <vector>

namespace albert {
// Shared buffers, spatial draw ranges and two geometric detail levels.
class BorderLines : public threepp::Group {
public:
    explicit BorderLines(const std::vector<float>& endpoints);
    std::shared_ptr<threepp::ShaderMaterial> style;
    void updateDetail(float distance);
private:
    std::shared_ptr<threepp::Group> coarse_,fine_;
    bool detailed_=false;
};
}
