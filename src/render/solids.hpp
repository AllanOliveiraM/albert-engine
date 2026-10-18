#pragma once
#include "core/bodies.hpp"
#include "lighting.hpp"
#include "regional.hpp"
#include "uploads.hpp"
#include "shapes.hpp"
#include "terrain.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <map>
#include <utility>
namespace albert {
struct SurfaceAssets {std::string body;threepp::Image day,height;std::optional<threepp::Image> ring;ShapeMesh shape;};
class SolidBodies {
public:
    SolidBodies(threepp::Scene& scene,std::vector<SurfaceAssets> assets);
    ~SolidBodies();
    bool contains(const std::string& id) const{return nodes_.contains(id);}
    const auto& nodes() const{return nodes_;}
    bool measuredShape(const std::string& id) const;
    bool hazy(const std::string& id) const;        // Opaque visible haze (Titan).
    bool surfaceView(const std::string& id) const; // Haze faded out to show the surface map.
    void showSurface(const std::string& id,bool surface);
    bool faintRings(const std::string& id) const;   // Jupiter, Uranus, Neptune.
    bool ringsEnhanced(const std::string& id) const;
    void enhanceRings(const std::string& id,bool enhanced); // Exposure stretch, off by default.
    std::shared_ptr<const threepp::BVH> picker(const std::string& id) const;
    const Terrain* terrain(const std::string& id) const; // Mars's relief; null elsewhere.
    // Mars's sky (WebGPU: its air drawn as the background) while its air is the display's.
    void showAir(const std::string& id,bool shown,float visibility);
    bool takeEviction(){return std::exchange(evicted_,false);}
    bool uploading() const{return uploads_.active();}
    void stage(RegionalImages images);
    // terrain: Mars's quadtree relief within three radii of the camera (MOLA).
    void update(threepp::GLRenderer*,threepp::PerspectiveCamera&,const threepp::Vector3& sun,double jdTDB,const std::string& focus,bool details,float seconds,const AuxiliaryLights& auxiliary={},bool terrain=true);
private:
    struct Surface;
    std::map<std::string,std::shared_ptr<threepp::Mesh>> nodes_;
    std::map<std::string,std::unique_ptr<Surface>> surfaces_;
    std::optional<RegionalImages> pending_;
    TextureUploads uploads_;
    bool evicted_=false;
};
}
