#pragma once
// A site's scenery (core::Scenery) as one node: its parts and the structures' bars merged by colour, in metres east, north and
// up of the site (the node's scale takes them to world units); those on a hinged structure under a pivot that animate() turns
// by the structures' angles (a sample's parts).
#include "core/scene.hpp"
#include "production.hpp"
#include "launch_cloud.hpp"
#include "site_lighting.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/textures/DataTexture.hpp"
#include <array>
#include <memory>
#include <vector>
namespace albert {
class Site {
public:
    explicit Site(const core::Scenery& scenery);
    const std::shared_ptr<threepp::Mesh>& node()const{return root_;}
    void animate(const std::vector<float>* angles);
    void quality(int value); // Small furnishings and tower fittings are optional; the structural silhouettes survive.
    void view(threepp::Camera& camera){if(production_)production_->view(camera);}
    void venting(double utc); // Simulated UTC keeps tank vapour on the acoustic schedule.
    void lighting(const threepp::Vector3& sun); // World position, from the scene's own sun; local +z is up.
    void cloudQuality(int value){cloudQuality_=value;if(launchCloud_)launchCloud_->quality(quality_>0?value:0);}
    void resetExhaust();
    float night()const{return night_;}
    void exhaust(float thrust,float above,float seconds,float waterFlow,const threepp::Vector3& wind=threepp::Vector3(2,-.8f,0),bool enabled=true); // Water: m³/s, 0 off, -1 ignition-only fallback; wind: site ENU m/s. Call while idle to age clouds.
private:
    struct Hinge {std::shared_ptr<threepp::Object3D> pivot;threepp::Vector3 axis;};
    struct Vent {threepp::Vector3 centre,ahead,across,up;float width,height;std::size_t structure;float pending=1;std::uint64_t id=0;};
    struct Puff {std::shared_ptr<threepp::Mesh> mesh;threepp::Vector3 origin,velocity,drift;float age=0,life=0,width=0,height=0,strength=0,seed=0,ground=0;unsigned kind=0;};
    std::shared_ptr<threepp::DataTexture> contactTexture_;
    std::shared_ptr<threepp::Mesh> root_;std::vector<Hinge> hinges_; // By structure; no pivot when fixed.
    std::unique_ptr<ProductionSite> production_;
    std::unique_ptr<SiteLighting> lighting_;
    std::vector<std::shared_ptr<threepp::ShaderMaterial>> lamps_;
    float night_=0;int quality_=1,cloudQuality_=1;
    std::shared_ptr<threepp::Mesh> detail_,industrialFine_;std::shared_ptr<threepp::ShaderMaterial> concrete_;
    std::vector<std::shared_ptr<threepp::Mesh>> towerDetails_;
    std::vector<Vent> vents_,tankVents_,sprays_,pools_;std::unique_ptr<LaunchCloud> launchCloud_;std::array<Puff,72> spray_;std::array<Puff,24> boiloff_;std::uint32_t seed_=0x73a91b5du,tankSeed_=0xd19bc273u;std::size_t tankCursor_=0,sprayCursor_=0,poolCursor_=0,runoffCursor_=0;float sprayPending_=0,mistPending_=0,runoffPending_=0;
    double ventUtc_=0;bool ventTimed_=false;
};
}
