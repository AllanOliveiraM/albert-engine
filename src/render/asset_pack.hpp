#pragma once

#include "download.hpp"
#include "geography.hpp"
#include "threepp/textures/Image.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace albert {
// Immutable index/file handle survives atomic replacement. Short synchronized
// reads; decoding stays parallel. No extraction and no complete-file RAM copy.
class AssetPack {
public:
    explicit AssetPack(std::filesystem::path path);
    threepp::Image image(const std::string& layer, const GeoRegion& region,
                         unsigned maximum, DownloadProgress& progress) const;
    std::uint64_t bytes() const;
    bool contains(const std::string& layer) const;
    bool hasPayload(const std::string& path) const;
    std::string payload(const std::string& path) const; // Indexed, SHA-256 checked.
private:
    struct Index;
    std::shared_ptr<const Index> index_;
};
std::shared_ptr<const AssetPack> visualAssetPack();
}
