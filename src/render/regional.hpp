#pragma once
#include "geography.hpp"
#include "regional_refresh.hpp"
#include "download.hpp"
#include "threepp/textures/Image.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/textures/Texture.hpp"
#include <array>
#include <future>
#include <optional>
namespace albert {
struct RegionalImages {
    std::string body="earth";
    int level=0;
    GeoRegion region;
    bool textureOnly=false; // Measured base mesh stays intact when imagery changes.
    float lift=20;unsigned segments=0; // Height bias (m) over the base sphere; mesh resolution (0: from the imagery).
    std::optional<threepp::Image> day,night,roughness,height,surfaceMask;
    std::shared_ptr<threepp::BufferGeometry> geometry;
    std::array<double,3> geometryOrigin{}; // Small Earth patches retain local float vertices, placed in double precision.
    std::array<std::shared_ptr<threepp::Texture>,5> gpu;
    std::string error;
};
void prepareRegionalImages(RegionalImages& images);
class RegionalImagery {
public:
    ~RegionalImagery();
    void request(std::optional<GeoRegion> region,std::string body="earth",int level=0);
    std::optional<RegionalImages> poll();
    bool active() const {return pending_.valid();}
    void retry();
    DownloadProgress progress;
    std::string error;
private:
    std::optional<GeoRegion> desired_,loaded_;
    std::optional<GeoRegion> working_;
    std::string workingBody_="earth",body_="earth",loadedBody_="earth";
    int level_=0,loadedLevel_=-1;
    RegionalRefreshSchedule refresh_;
    std::future<RegionalImages> pending_;
};
}
