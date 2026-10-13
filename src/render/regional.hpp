#pragma once
#include "geography.hpp"
#include "download.hpp"
#include "threepp/textures/Image.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/textures/Texture.hpp"
#include <array>
#include <future>
#include <optional>
namespace albert {
struct RegionalImages {
    bool moon=false;
    int level=0;
    GeoRegion region;
    std::optional<threepp::Image> day,night,roughness,height,surfaceMask;
    std::shared_ptr<threepp::BufferGeometry> geometry;
    std::array<std::shared_ptr<threepp::Texture>,5> gpu;
    std::string error;
};
void prepareRegionalImages(RegionalImages& images);
class RegionalImagery {
public:
    ~RegionalImagery();
    void request(std::optional<GeoRegion> region,bool moon=false,int level=0);
    std::optional<RegionalImages> poll();
    bool active() const {return pending_.valid();}
    void retry();
    DownloadProgress progress;
    std::string error;
private:
    std::optional<GeoRegion> desired_,loaded_;
    std::optional<GeoRegion> working_;
    bool workingMoon_=false;
    bool moon_=false,loadedMoon_=false;
    int level_=0,loadedLevel_=-1;
    std::chrono::steady_clock::time_point changed_{},next_{};
    std::future<RegionalImages> pending_;
};
}
