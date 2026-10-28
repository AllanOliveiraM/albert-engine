#pragma once
#include "core/scene.hpp"
#include "launch_crowd_detail.hpp"
#include "threepp/threepp.hpp"
#include "threepp/objects/Points.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <functional>
#include <cmath>
#include <optional>
namespace albert {
// Render-only instances in site ENU metres. Never enter the registry, RPC parts or contact structures.
class LaunchCrowd {
public:
    using Ground=std::function<std::optional<float>(const threepp::Vector3&)>;
    struct Instance {threepp::Vector3 position,forward{0,1,0},up{0,0,1};std::uint32_t colour=0xffffff;unsigned kind=0;};
    explicit LaunchCrowd(const core::Scenery& scenery);
    ~LaunchCrowd();
    const std::shared_ptr<threepp::Object3D>& node()const{return root_;}
    static bool launchDay(double count,bool scrubbed){return std::isfinite(count)&&!scrubbed&&count>=-14400&&count<=1200;}
    void enabled(bool value,int quality);
    void view(threepp::Camera& camera,const Ground& ground={});
    const std::vector<Instance>& vehicles()const{return vehicles_;}
    const std::vector<Instance>& spectators()const{return people_;}
    unsigned nearby()const{return nearby_;}
    unsigned detailedCars()const{return detailedCars_;}
    unsigned detailedPeople()const{return detailedPeople_;}
    unsigned carDetailVertices()const{return detail_.carVertices;}
    unsigned personDetailVertices()const{return detail_.personVertices;}
    static constexpr unsigned nearCarLimit=48,nearPersonLimit=128;
    static constexpr float nearEnter=135,nearLeave=150;
private:
    void build();
    void detail();
    void upload(const std::shared_ptr<threepp::Points>& mesh,const std::vector<Instance>& instances);
    std::shared_ptr<threepp::Object3D> root_;
    std::shared_ptr<threepp::Points> cars_,figures_,nearCars_,nearFigures_;
    CrowdDetail detail_;std::vector<unsigned> seats_;
    std::vector<bool> carNear_,personNear_;
    unsigned detailedCars_=0,detailedPeople_=0;
    std::vector<Instance> vehicles_,people_;
    core::Scenery source_;bool built_=false;unsigned nearby_=0,cursor_=0;
};
}
