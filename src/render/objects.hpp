#pragma once
#include "core/scene.hpp"
#include "lighting.hpp"
#include "scenery.hpp"
#include "vehicle.hpp"
#include "splashdown.hpp"
#include "threepp/threepp.hpp"
#include "threepp/utils/BVH.hpp"
#include <algorithm>
#include <array>
#include <map>
namespace albert {
threepp::Vector3 renderPosition(core::Vector position,core::Vector origin={});
// The same in double precision (Earth radii; x, z, -y): a node's float position about 1 unit out carries ~0.4 m of error,
// which a part placed from it (a terrain node, a vehicle's) must not inherit.
std::array<double,3> renderPositionExact(core::Vector position,core::Vector origin={});
class Terrain;
class SceneObjects {
public:
    ~SceneObjects();
    explicit SceneObjects(threepp::Scene& scene):scene_(scene){}
    void bind(const core::ObjectId& id,std::shared_ptr<threepp::Mesh> mesh,std::shared_ptr<const threepp::BVH> picker={});
    void sync(const core::Scene& registry,double utcSeconds,core::Vector origin);
    void deluge(const core::ObjectId& site,float cubicMetres){deluge_[site]=cubicMetres;}
    void wind(const core::ObjectId& site,const threepp::Vector3& enu){winds_[site]=enu;}
    void cloudQuality(int value){splashQuality_=std::clamp(value,0,2);for(auto& [id,site]:sites_)site->cloudQuality(value);for(auto& [id,water]:water_)water->quality(quality_<=0?0:splashQuality_);}
    void delugeEffects(bool on){delugeEffects_=on;} // The pads' spray and launch steam (Ajustes).
    void quality(int value){quality_=value;for(auto& [id,water]:water_)water->quality(value<=0?0:splashQuality_);for(auto& [id,site]:sites_)site->quality(value);for(auto& [id,vehicle]:vehicles_)vehicle->quality(value);}
    void count(const core::ObjectId& flight,double seconds,bool scrubbed,bool launch=true){count_[flight]={seconds,scrubbed,launch};} // Its count time (T, s) and whether it scrubbed.
    void terrain(const Terrain* terrain,const threepp::Object3D* earth){terrain_=terrain;earth_=earth;}
    void updateLighting(threepp::Camera&,const threepp::Vector3& sun,const AuxiliaryLights&,float seconds);
    std::optional<core::ObjectId> pick(const core::Scene&,threepp::Raycaster&) const;
    const std::map<core::ObjectId,std::shared_ptr<threepp::Mesh>>& nodes() const{return nodes_;}
    const Vehicle* vehicle(const core::ObjectId& id)const{const auto it=vehicles_.find(id);return it==vehicles_.end()?nullptr:it->second.get();}
    const std::array<double,3>* exact(const core::ObjectId& id)const{const auto it=exact_.find(id);return it==exact_.end()?nullptr:&it->second;} // Its node's position at the last sync, in double precision.
private:
    const Terrain* terrain_=nullptr;const threepp::Object3D* earth_=nullptr;
    threepp::Scene& scene_;
    std::map<core::ObjectId,std::shared_ptr<threepp::Mesh>> nodes_;std::map<core::ObjectId,std::array<double,3>> exact_;
    std::map<core::ObjectId,std::shared_ptr<const threepp::BVH>> pickers_;
    struct ModelMeshes {std::shared_ptr<const core::Model> source;std::vector<std::shared_ptr<threepp::BufferGeometry>> geometry;std::vector<std::shared_ptr<threepp::Material>> materials;};
    std::map<std::string,ModelMeshes> models_;
    std::map<core::ObjectId,std::uint64_t> instances_;
    std::map<core::ObjectId,float> displayedFill_;
    std::map<core::Shape,std::shared_ptr<threepp::BufferGeometry>> geometries_;
    std::map<core::ObjectId,std::unique_ptr<Vehicle>> vehicles_; // Objects with a look.
    std::map<core::ObjectId,std::unique_ptr<Site>> sites_;       // With scenery.
    std::map<core::ObjectId,float> deluge_; // Each site's deluge water now (m³/s), from its pad's sound source.
    struct Count {double seconds;bool scrubbed,launch;};
    std::map<core::ObjectId,Count> count_;bool delugeEffects_=true;
    std::map<core::ObjectId,threepp::Vector3> winds_;
    std::map<core::ObjectId,std::unique_ptr<Splashdown>> water_;int quality_=1,splashQuality_=1;
    double cloudUtc_=0;bool cloudTimed_=false;
};
}
