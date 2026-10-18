#pragma once
#include "core/scene.hpp"
#include "threepp/objects/Mesh.hpp"
#include <map>
namespace albert {
// Fraction of an extended, uniformly bright Sun visible from a point.
double solarVisibility(core::Vector point,core::Vector sun,double sunRadius,core::Vector occluder,double radius);
class EclipseVisuals {
public:
    EclipseVisuals(std::shared_ptr<threepp::Mesh> earth,std::shared_ptr<threepp::Mesh> moon,std::shared_ptr<threepp::Mesh> sun);
    ~EclipseVisuals();
    void update(bool enabled);
    bool lunarActive() const{return lunarActive_;}
private:
    struct Surface {std::weak_ptr<threepp::Mesh> source;std::shared_ptr<threepp::Mesh> overlay;};
    std::shared_ptr<threepp::Mesh> earth_,moon_,sun_;
    std::map<unsigned,Surface> surfaces_;
    bool lunarActive_=false;
};
}
