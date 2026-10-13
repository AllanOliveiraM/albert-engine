#pragma once

#include "download.hpp"
#include "geography.hpp"
#include "threepp/textures/Image.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace albert {
// Immutable index; each read owns its stream. No extraction and no disk cache.
class AssetPack {
public:
    explicit AssetPack(std::filesystem::path path);
    threepp::Image image(const std::string& layer, const GeoRegion& region,
                         unsigned maximum, DownloadProgress& progress) const;
    std::uint64_t bytes() const;
private:
    struct Index;
    std::shared_ptr<const Index> index_;
};
std::shared_ptr<const AssetPack> visualAssetPack();
}
