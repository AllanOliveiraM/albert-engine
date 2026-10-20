#pragma once
// A site's scenery (core::Scenery) as one node: its parts and the structures' bars merged by colour, in metres east, north and
// up of the site (the node's scale takes them to world units); those on a hinged structure under a pivot that animate() turns
// by the structures' angles (a sample's parts).
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include <memory>
#include <vector>
namespace albert {
class Site {
public:
    explicit Site(const core::Scenery& scenery);
    const std::shared_ptr<threepp::Mesh>& node()const{return root_;}
    void animate(const std::vector<float>* angles);
private:
    struct Hinge {std::shared_ptr<threepp::Object3D> pivot;threepp::Vector3 axis;};
    std::shared_ptr<threepp::Mesh> root_;std::vector<Hinge> hinges_; // By structure; no pivot when fixed.
};
}
