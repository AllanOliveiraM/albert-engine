#pragma once
// A vehicle's look in the scene (core::VehicleLook): its body, hinged parts, engine bells and plumes and thruster jets, built
// in metres about the stage's middle in the object's local axes (y along the nose, x along body -y, z along body z; the
// node's scale takes them to world units). animate() turns the bells and parts and lights the plumes and jets from a
// sample's parts (core::Sample::parts): also the shock layer's glow by the heating and the tanks' frost by their fill; once the
// vehicle has broken up, a fireball, smoke and debris thrown out instead, by the seconds since.
#include "core/scene.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <array>
#include <chrono>
#include <memory>
#include <vector>
namespace albert {
class Vehicle {
public:
    explicit Vehicle(const core::VehicleLook& look);
    const std::shared_ptr<threepp::Mesh>& node()const{return root_;}
    static float gimballing(const threepp::Object3D& node,const core::VehicleLook& look); // Read the animated bells through the scene node.
    void animate(const std::vector<float>* parts);
private:
    struct Engine {std::shared_ptr<threepp::Object3D> pivot;std::array<std::shared_ptr<threepp::Mesh>,2> plume;core::Vector direction;double radius=.5;float heat=0;std::shared_ptr<threepp::Mesh> hot,exit;std::shared_ptr<threepp::ShaderMaterial> bell;};
    struct Part {std::shared_ptr<threepp::Object3D> pivot;threepp::Quaternion base;threepp::Vector3 axis;double gain=1,offset=0;unsigned surface=0;};
    struct Jet {std::shared_ptr<threepp::Mesh> mesh;};
    struct Piece {std::shared_ptr<threepp::Mesh> mesh;threepp::Vector3 velocity,spin;};
    std::shared_ptr<threepp::Mesh> root_;std::vector<Engine> engines_;std::vector<Part> parts_;std::vector<Jet> jets_;
    double length_=1;std::shared_ptr<threepp::Mesh> fire_,smoke_;std::vector<Piece> pieces_; // The breakup's, made when it comes.
    std::chrono::steady_clock::time_point updated_=std::chrono::steady_clock::now();double time_=0; // Wall time keeps flicker and cooling independent of the frame rate.
    threepp::Vector3 aftGlow_;std::vector<std::shared_ptr<threepp::ShaderMaterial>> aft_;
    void explode(float seconds);
    struct Frost {std::shared_ptr<threepp::Mesh> mesh;core::LookTank tank;};
    std::shared_ptr<threepp::Mesh> plasma_,vapour_;std::vector<Frost> frost_;std::vector<std::shared_ptr<threepp::ShaderMaterial>> tiles_; // tiles_: the heat shield's, glowing with its heat.
    void shine(float heating,float fill); // The shock layer's glow by the heat flux (W/m²); the tanks' frost by their fill.
};
}
