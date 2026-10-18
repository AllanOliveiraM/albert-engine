#pragma once
#include "threepp/core/Object3D.hpp"
#include "threepp/materials/Material.hpp"
#include "threepp/math/Vector3.hpp"
#include "threepp/math/Vector4.hpp"
#include "threepp/math/Frustum.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace threepp {class Image;}
namespace albert {
// Heights in metres over the body's reference surface at geodetic latitudes and longitudes
// (radians), for one node at `level`, on the terrain's worker; must not wait for the network.
// False: provisional (finer data still downloading), rebuilt once `generation` advances.
using TerrainHeights=std::function<bool(const std::vector<double>& latitudes,const std::vector<double>& longitudes,int level,std::vector<float>& out)>;
struct TerrainBody {
    double radius=1;      // Metres of the reference radius.
    double scale=1;       // Its length in the mesh's local frame (the Moon's mesh: 1737.4 / 6378.137).
    double flattening=1;  // Polar to equatorial axis ratio of the mesh (its y scale): latitudes are parametric there.
    int maxLevel=12;      // Finest level: a cube face's quarter circle / 2^level / 64 (12: ~38 m on Earth).
    double relief=10e3;   // Highest relief (m), for culling behind the horizon.
    TerrainHeights heights;
    std::function<std::uint64_t()> generation; // Data arrivals so far (provisional nodes rebuild after one).
    double morphSeconds=.4; // Children replacing a drawn parent rise from its surface to theirs (no popping).
};
// The bodies' height sources (terrain_heights.cpp), from the visual pack's global height images.
TerrainHeights earthTerrainHeights(const threepp::Image& global); // GEBCO, Copernicus GLO-30 from level 9, EGM2008 geoid.
TerrainHeights moonTerrainHeights(const threepp::Image& global);  // LOLA LDEM_128 from level 6.
TerrainHeights marsTerrainHeights(const threepp::Image& global);  // MOLA MEGDR from level 7.
// Level-of-detail terrain on a cube-sphere quadtree: nodes of 65×65 posts with skirts, built on a
// worker and drawn with the body's material as children of its mesh, refined by distance to the
// camera (CDLOD-style ranges); a parent stays until its four children are built, which then morph
// from its surface to their own. Positions are
// radial in the mesh's local frame, as the globe's relief: scale × (1 + h / radius) × the direction.
class Terrain {
public:
    static constexpr int posts=65; // Per node side (64 quads).
    Terrain(threepp::Object3D& parent,std::shared_ptr<threepp::Material> material,TerrainBody body);
    ~Terrain();
    Terrain(const Terrain&)=delete;Terrain& operator=(const Terrain&)=delete;
    // Selects the nodes for a camera in the parent's local frame (and, with culling, its view
    // frustum there), queues missing ones and adopts built ones; true once the whole body is covered
    // (the coarse faces exist). Without culling every node is refined by distance only.
    bool update(const threepp::Vector3& camera,const threepp::Frustum* frustum=nullptr,bool culling=true);
    void hide(); // No node drawn (until the next update).
    [[nodiscard]] std::size_t drawn()const;   // Nodes selected by the last update.
    [[nodiscard]] std::size_t pending()const; // Nodes queued or building.
    [[nodiscard]] int finestLevel()const;     // Of the last selection.
    [[nodiscard]] double coveredSolidAngle()const; // Of the last selection, steradians (4π without culling).
    // Imagery of a region on the nodes wholly inside it: that material, and texture coordinates over
    // the region (u' = (u - bounds.x) / bounds.z, v' = (v - bounds.y) / bounds.w, u and v as the globe's;
    // uv2 keeps the global ones). Nodes across its edge keep the body's material; none restores all.
    void detail(std::shared_ptr<threepp::Material> material,std::optional<threepp::Vector4> bounds);
    [[nodiscard]] std::size_t detailed()const; // Drawn nodes with the region's imagery.
    // Radius of the drawn relief toward a point in the parent's local frame (its finest built node,
    // bilinear between posts), in that frame's units; none while nothing is drawn.
    [[nodiscard]] std::optional<double> ground(const threepp::Vector3& local)const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
