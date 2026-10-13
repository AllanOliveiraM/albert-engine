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
#include <future>

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
    bool shadows = true;
    bool borders = false;
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
    void updateUploads(threepp::GLRenderer& renderer);
    void retryDetails() { imageFailed_.fill(false); detailError.clear(); }
    void replaceAssets(DecodedAssets assets);
    void update(const threepp::Vector3& solarDirection, const threepp::Vector3& cameraPosition, const VisualSettings& settings, float seconds);
    void updateBorderMaterial(threepp::ShaderMaterial& material, const threepp::Vector3& cameraPosition);
    void setCloudImage(threepp::Image image);
    void setCloudDetail(threepp::Image image, const GeoRegion& region);
    void setRegionalImages(RegionalImages images);
    void updateImageDetail(const threepp::Vector3& camera, bool enabled);

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
    std::shared_ptr<threepp::MeshStandardMaterial> earthMaterial_;
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
    struct DetailedImage { int index; std::optional<threepp::Image> image; std::string error; };
    std::future<DetailedImage> pendingImage_;
    std::array<bool,3> imageWanted_{}, imageFailed_{};
    std::chrono::steady_clock::time_point imageRetry_{};
};

std::shared_ptr<threepp::Points> makeStarField(const std::filesystem::path& catalogue, double jdTDB);

// IAU_MOON trigonometric approximation from NASA/NAIF pck00011, NOT the
// high-precision binary lunar PCK. Input is Julian date TDB, output ICRF scene.
threepp::Matrix4 moonOrientation(double jdTDB);

} // namespace albert
