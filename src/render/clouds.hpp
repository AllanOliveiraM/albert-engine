#pragma once

#include "download.hpp"
#include "geography.hpp"
#include "threepp/textures/Image.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <optional>
#include <string>

namespace albert {

struct CloudUpdate {
    std::optional<threepp::Image> image;
    std::string date, error;
    double coverage = 0, detailCoverage = 0;
    bool fromCache = false;
    int refreshAfterSeconds = 0;
};

struct CloudDetail {
    CloudUpdate observation;
    GeoRegion region;
};

// NASA GIBS/MODIS supplies daily mosaics updated as orbital swaths arrive.
// This is observed cloud fraction, NOT an instantaneous global cloud mask.
class CloudSync {
public:
    CloudSync(std::filesystem::path cache, std::filesystem::path palette);
    ~CloudSync();
    std::optional<CloudUpdate> poll(bool enabled);
    void refresh(bool clearCache = false);
    void requestDetail(std::optional<GeoRegion> region);
    std::optional<CloudDetail> pollDetail();
    bool detailActive() const { return detailPending_.valid(); }
    DownloadSnapshot detailSnapshot() const { return detailProgress_.snapshot(); }
    std::string detailError;
    bool active = false;
    std::string date, error;
    double coverage = 0, detailCoverage = 0;
    DownloadProgress progress;

private:
    std::filesystem::path cache_, palette_;
    std::future<CloudUpdate> pending_;
    std::chrono::steady_clock::time_point next_{};
    int failures_ = 0;
    bool cacheChecked_ = false;
    DownloadProgress detailProgress_;
    std::optional<GeoRegion> desired_, loaded_;
    std::optional<GeoRegion> working_;
    std::chrono::steady_clock::time_point detailChanged_{}, detailNext_{};
    std::future<CloudDetail> detailPending_;
    int detailFailures_ = 0;
};

} // namespace albert
