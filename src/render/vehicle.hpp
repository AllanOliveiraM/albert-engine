#pragma once
// A vehicle's look in the scene (core::VehicleLook): its body, hinged parts, engine bells and plumes and thruster jets, built
// in metres about the stage's middle in the object's local axes (y along the nose, x along body -y, z along body z; the
// node's scale takes them to world units). animate() turns the bells and parts and lights the plumes and jets from a
// sample's parts (core::Sample::parts).
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include <memory>
#include <vector>
namespace albert {
class Vehicle {
public:
    explicit Vehicle(const core::VehicleLook& look);
    const std::shared_ptr<threepp::Mesh>& node()const{return root_;}
    void animate(const std::vector<float>* parts);
private:
    struct Engine {std::shared_ptr<threepp::Object3D> pivot;std::vector<std::shared_ptr<threepp::Mesh>> plume;core::Vector direction;double radius=.5;};
    struct Part {std::shared_ptr<threepp::Object3D> pivot;threepp::Quaternion base;threepp::Vector3 axis;double gain=1,offset=0;unsigned surface=0;};
    struct Jet {std::shared_ptr<threepp::Mesh> mesh;};
    std::shared_ptr<threepp::Mesh> root_;std::vector<Engine> engines_;std::vector<Part> parts_;std::vector<Jet> jets_;
};
}
