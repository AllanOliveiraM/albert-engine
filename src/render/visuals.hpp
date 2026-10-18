#pragma once

#include "threepp/threepp.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/materials/ShaderMaterial.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/objects/Points.hpp"
#include "download.hpp"
#include "geography.hpp"
#include "regional.hpp"
#include "uploads.hpp"
#include "solids.hpp"
#include "lighting.hpp"
#include "terrain.hpp"
#include <future>
#include <thread>

#include <array>
#include <filesystem>
#include <optional>

namespace albert {

// All of these are static local assets. Decoding/resampling runs on a worker;
// texture upload and material changes happen on the graphics thread.
struct DecodedAssets {
    std::array<std::optional<threepp::Image>, 6> images;
    std::optional<threepp::Image> oceans;
    std::optional<threepp::Image> moonHeight;
    std::vector<SurfaceAssets> surfaces;
};
DecodedAssets loadVisualAssets(const std::filesystem::path& directory, DownloadProgress& progress);

struct VisualSettings {
    bool atmosphere = true;
    bool clouds = false;
    bool nightLights = true;
    bool moonAssist = false;
    float moonAssistIntensity = .25f;
    bool earthAssist = false;
    float earthAssistIntensity = .22f;
    AuxiliaryLights auxiliary;
    bool shadows = true;
    bool borders = false;
    bool terrain = true; // Quadtree relief near the Earth (Copernicus 30 m close by).
    int quality = 1; // 0: one framebuffer pixel per logical pixel; 1: up to 2x.
};

class PlanetVisuals {
public:
    PlanetVisuals(threepp::Scene& scene, DecodedAssets assets);
    ~PlanetVisuals();
    DownloadProgress detailProgress;
    std::string detailError;
    bool detailActive() const { return pendingImage_.valid() || uploads_.active(); }
    bool uploading() const { return uploads_.active(); }
    void stageRegionalImages(RegionalImages images);
    void updateUploads(threepp::GLRenderer* renderer); // Null on WebGPU.
    void retryDetails() { imageFailed_.fill(false); detailError.clear(); }
    void replaceAssets(DecodedAssets assets);
    // camera: its view limits the terrain's refinement (none: refined all around).
    void update(const threepp::Vector3& solarDirection, const threepp::Vector3& cameraPosition, const VisualSettings& settings, float seconds, threepp::Camera* camera=nullptr);
    void updateBorderMaterial(threepp::ShaderMaterial& material, const threepp::Vector3& cameraPosition);
    void setCloudImage(threepp::Image image);
    void setCloudDetail(threepp::Image image, const GeoRegion& region);
    void setRegionalImages(RegionalImages images);
    bool releaseDistantDetails(const std::string& focus,const threepp::Vector3& camera,bool enabled,float seconds);
    void updateImageDetail(const threepp::Vector3& camera, bool enabled);
    [[nodiscard]] float atmosphereVisibility() const { return displayedAtmosphere_; } // The air's shown strength, 0-1.
    // The cloud layer's map as drawn (live or the pack's) and its opacity there (the shell's 0.88 × shown).
    [[nodiscard]] threepp::Texture* cloudMap() const;
    [[nodiscard]] float cloudOpacity() const { return .88f*displayedClouds_; }
    [[nodiscard]] const Terrain* earthTerrain() const { return earthTerrain_.get(); } // Null until assets load.
    [[nodiscard]] const Terrain* moonTerrain() const { return moonTerrain_.get(); }

    std::shared_ptr<threepp::Mesh> earth, moon, sun;

private:
    TextureUploads uploads_;
    std::optional<RegionalImages> uploadingRegion_;
    std::shared_ptr<threepp::Texture> uploadingSun_;
    std::shared_ptr<threepp::Mesh> clouds_, atmosphere_, night_, earthDetail_, moonDetail_;
    std::array<std::shared_ptr<threepp::Texture>,4> regionalTextures_;
    std::array<std::shared_ptr<threepp::Texture>,2> regionalHeights_;
    std::array<std::shared_ptr<threepp::Texture>,2> surfaceMasks_;
    std::shared_ptr<threepp::Texture> opaqueMask_;
    std::shared_ptr<threepp::MeshPhongMaterial> moonDetailMaterial_;
    std::shared_ptr<threepp::MeshStandardMaterial> earthMaterial_, earthTerrainMaterial_;
    std::unique_ptr<Terrain> earthTerrain_, moonTerrain_; // Replace the globe meshes near the Earth and the Moon (their children).
    std::shared_ptr<threepp::MeshPhongMaterial> moonTerrainMaterial_;
    // The regional imagery on the terrain's nodes (the patches' materials without bump: the relief is geometry).
    std::shared_ptr<threepp::MeshStandardMaterial> earthTerrainDetail_;std::shared_ptr<threepp::MeshPhongMaterial> moonTerrainDetail_;
    std::array<std::optional<threepp::Vector4>,2> terrainBounds_; // Earth, Moon: the region in texture coordinates.
    DownloadProgress demProgress_;std::jthread demFetcher_; // Downloads the Copernicus pieces the terrain noted.
    std::shared_ptr<threepp::MeshPhongMaterial> moonMaterial_;
    std::shared_ptr<threepp::ShaderMaterial> cloudMaterial_;
    std::shared_ptr<threepp::ShaderMaterial> atmosphereMaterial_, nightMaterial_;
    std::shared_ptr<threepp::ShaderMaterial> sunMaterial_;
    std::array<std::shared_ptr<threepp::Texture>, 6> textures_;
    std::shared_ptr<threepp::Texture> oceanTexture_, liveCloudTexture_, opticalDepthTexture_, cloudDetailTexture_;
    std::shared_ptr<threepp::Texture> moonHeightTexture_;
    std::shared_ptr<threepp::Texture> previousCloudDetail_;
    std::optional<GeoRegion> cloudDetailBounds_;
    float cloudDetailBlend_=1.f;
    std::array<std::shared_ptr<threepp::Texture>,2> surfaceFillTextures_;
    std::array<threepp::Vector3,2> surfaceFillDirections_;
    std::array<float,2> displayedFill_{};
    float displayedAtmosphere_=1.f, displayedClouds_=0.f, displayedNight_=1.f;
    std::optional<GeoRegion> earthDetailBounds_;
    std::array<std::shared_ptr<threepp::Texture>,3> imageDetail_;
    struct DetailedImage { int index; std::optional<threepp::Image> image; std::string error; int level=-1; };
    std::future<DetailedImage> pendingImage_;
    std::array<float,2> detailIdle_{};
    std::array<bool,3> imageWanted_{}, imageFailed_{};
    std::chrono::steady_clock::time_point imageRetry_{};
    // Downloaded detail levels arrive over time; refresh the Sun when a finer one lands.
    std::uint64_t detailGeneration_=0;int sunDetailLevel_=-1,uploadingSunLevel_=-1;bool sunStale_=false;
};

// Directions at J2000 plus linear space motion, applied in the vertex shader for
// the epoch set here (and initially by makeStarField).
std::shared_ptr<threepp::Points> makeStarField(const std::filesystem::path& catalogue, double jdTDB);
void setStarEpoch(threepp::Points& stars, double jdTDB);

// IAU_MOON trigonometric approximation from NASA/NAIF pck00011, NOT the
// high-precision binary lunar PCK. Input is Julian date TDB, output ICRF scene.
threepp::Matrix4 moonOrientation(double jdTDB);

} // namespace albert
