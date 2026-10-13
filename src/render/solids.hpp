#pragma once
#include "core/bodies.hpp"
#include "lighting.hpp"
#include "regional.hpp"
#include "uploads.hpp"
#include "threepp/threepp.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include <map>
namespace albert {
struct SurfaceAssets {std::string body;threepp::Image day,height;std::optional<threepp::Image> ring;};
class SolidBodies {
public:
    SolidBodies(threepp::Scene& scene,std::vector<SurfaceAssets> assets);
    ~SolidBodies();
    bool contains(const std::string& id) const{return nodes_.contains(id);}
    const auto& nodes() const{return nodes_;}
    bool uploading() const{return uploads_.active();}
    void stage(RegionalImages images);
    void update(threepp::GLRenderer&,threepp::PerspectiveCamera&,const threepp::Vector3& sun,double jdTDB,const std::string& focus,bool details,float seconds,const AuxiliaryLights& auxiliary={});
private:
    struct Surface;
    std::map<std::string,std::shared_ptr<threepp::Mesh>> nodes_;
    std::map<std::string,std::unique_ptr<Surface>> surfaces_;
    std::optional<RegionalImages> pending_;
    TextureUploads uploads_;
};
}
