#pragma once
// A vehicle's look in the scene (core::VehicleLook): its body, hinged parts, engine bells and plumes and thruster jets, built
// in metres about the stage's middle in the object's local axes (y along the nose, x along body -y, z along body z; the
// node's scale takes them to world units). animate() turns the bells and parts and lights the plumes and jets from a
// sample's parts (core::Sample::parts): also the shock layer's glow by the heating and the tanks' frost by their fill; once the
// vehicle has broken up, a fireball, smoke and debris thrown out instead, by the seconds since.
#include "core/scene.hpp"
#include "engine_cycle.hpp"
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
    void animate(const std::vector<float>* parts,double utc,core::Vector position={},core::Vector velocity={});
    void impinge(float metres,float glow);
    void venting(float strength,double utc,bool closed=false); // Simulated UTC keeps boil-off on the acoustic schedule.
    double length()const{return length_;}
    float thrust()const{return thrust_;} // The share of its full thrust burning, at the last animate().
    void quality(int value); // Fine surface detail and directional atmosphere reflections simplify at low quality.
    struct EngineSound {std::uint64_t serial=0;double utc=0;float radius=0;bool ignition=false;};
    const std::array<EngineSound,256>& engineSounds()const{return engineSounds_;}
    std::uint64_t soundSerial()const{return soundSerial_;}
    std::size_t engineCount()const{return engines_.size();}
private:
    struct Gas {std::shared_ptr<threepp::Mesh> mesh;core::Vector origin,velocity,flow;threepp::Quaternion attitude;double born=-1e30;float life=0,radius=0,speed=0,air=0;unsigned kind=0;bool pending=false;};
    struct Engine {std::shared_ptr<threepp::Object3D> pivot,detached;std::array<std::shared_ptr<threepp::Mesh>,2> plume,remnant;core::Vector direction;double radius=.5,releasedUtc=-1e30;float heat=0,exitY=0,remnantLife=0,remnantWidth=0;std::shared_ptr<threepp::Mesh> hot,exit,torch;std::shared_ptr<threepp::ShaderMaterial> bell;
        EngineCycle cycle;core::Vector released,velocity,inherited,flow;threepp::Quaternion attitude;std::array<float,2> remnantStrength{};std::array<Gas,6> gas;};
    struct Part {std::shared_ptr<threepp::Object3D> pivot;threepp::Quaternion base;threepp::Vector3 axis;double gain=1,offset=0;unsigned surface=0;};
    struct Jet {std::shared_ptr<threepp::Mesh> mesh;};
    struct Piece {std::shared_ptr<threepp::Mesh> mesh;threepp::Vector3 velocity,spin;};
    std::shared_ptr<threepp::Mesh> root_;std::vector<Engine> engines_;std::vector<Part> parts_;std::vector<Jet> jets_;
    double length_=1;std::shared_ptr<threepp::Mesh> fire_,smoke_;std::vector<Piece> pieces_; // The breakup's, made when it comes.
    std::shared_ptr<threepp::PointLight> exhaustLight_; // One bounded warm source per stage, weighted by the lit bells.
    float obstruction_=0,impingement_=0,thrust_=0;
    std::vector<std::shared_ptr<threepp::ShaderMaterial>> surfaces_;float soot_=0;double surfaceUtc_=0;bool surfaceTimed_=false;
    std::array<EngineSound,256> engineSounds_{};std::uint64_t soundSerial_=0;
    std::chrono::steady_clock::time_point updated_=std::chrono::steady_clock::now();double time_=0; // Wall time keeps flicker and cooling independent of the frame rate.
    threepp::Vector3 aftGlow_;std::vector<std::shared_ptr<threepp::ShaderMaterial>> aft_;
    void explode(float seconds);
    struct Vent {threepp::Vector3 origin,direction;float gain=1,pending=0;};
    struct Puff {std::shared_ptr<threepp::Mesh> mesh;threepp::Vector3 origin,velocity,drift,outward;float age=0,life=0,strength=0,seed=0,width=0,level=0;};
    std::vector<Vent> vents_;std::array<Puff,48> mist_;float radius_=1,frostArc_=0;std::uint32_t ventSeed_=0x82ac91b7u;
    double ventUtc_=0;bool ventTimed_=false;
    struct Frost {std::shared_ptr<threepp::Mesh> mesh;core::LookTank tank;};
    std::shared_ptr<threepp::Mesh> plasma_,wake_,vapour_;std::vector<std::shared_ptr<threepp::Mesh>> sheaths_;threepp::Vector3 entryWind_{0,0,1};std::vector<Frost> frost_;std::vector<std::shared_ptr<threepp::ShaderMaterial>> tiles_; // tiles_: the heat shield's, glowing with its heat.
    void shine(float heating,float fill); // The shock layer's glow by the heat flux (W/m²); the tanks' frost by their fill.
};
}
