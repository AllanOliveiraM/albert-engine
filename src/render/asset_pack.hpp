#pragma once

#include "download.hpp"
#include "geography.hpp"
#include "threepp/textures/Image.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace albert {
// Immutable index/file handles survive atomic replacement. Short synchronized
// reads; decoding stays parallel. No extraction and no complete-file RAM copy.
// Several archives (the complete library or download containers) form one index;
// each layer is read up to the highest resolution level whose tiles are all present.
class AssetPack {
public:
    explicit AssetPack(std::filesystem::path path);
    explicit AssetPack(const std::vector<std::filesystem::path>& paths);
    threepp::Image image(const std::string& layer, const GeoRegion& region,
                         unsigned maximum, DownloadProgress& progress) const;
    std::uint64_t bytes() const;
    bool contains(const std::string& layer) const;
    int availableLevel(const std::string& layer) const; // -1 when absent.
    int maximumLevel(const std::string& layer) const;   // Declared pyramid depth.
    bool hasPayload(const std::string& path) const;
    std::string payload(const std::string& path) const; // Indexed, SHA-256 checked.
private:
    struct Index;
    std::shared_ptr<const Index> index_;
};

// Download containers published with a release (scripts/split-asset-pack.py).
struct AssetCatalog {
    struct Container {std::string file,group,sha256;int level=0;bool essential=false;std::uint64_t bytes=0;};
    std::string id,release;std::vector<Container> containers; // Download priority order.
};
AssetCatalog readAssetCatalog(const std::filesystem::path& path);
// assets/packs beside the executable, or data/packs when assets is not writable.
std::vector<std::filesystem::path> assetContainerDirectories();

// Current library: complete visuals.pak when present, otherwise the installed
// containers listed by assets/catalog.json. Installing a verified container
// publishes a new immutable snapshot; readers keep the snapshot they hold.
std::shared_ptr<const AssetPack> visualAssetPack();
std::uint64_t visualAssetGeneration();
bool visualAssetsComplete();
void installVisualAssetContainer(const std::filesystem::path& path);
}
