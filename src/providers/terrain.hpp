#pragma once
#include "net/download.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
namespace albert::providers {
// Copernicus DEM GLO-30 (AWS open data): 1° cloud-optimised GeoTIFF tiles, heights in metres
// above the EGM2008 geoid (surface model: buildings and canopy included), 1" posts
// (pixel-is-point). Internal 1024² tiles are fetched by byte range and cached compressed.
class CopernicusDem {
public:
    using Transport=std::function<std::string(const std::string& url,const std::string& range,DownloadProgress*)>;
    explicit CopernicusDem(std::filesystem::path cache,Transport transport={});
    // Bilinear heights on a width×height grid spanning the region (rows south to north,
    // edges included). Ocean tiles, which Copernicus omits, read as 0.
    std::vector<float> heights(double west,double south,double east,double north,unsigned width,unsigned height,DownloadProgress& progress);
    double height(double latitude,double longitude,DownloadProgress& progress); // Degrees.
private:
    struct File;
    const File* file(int latitude,int longitude,DownloadProgress& progress);
    const std::vector<float>& block(const File& f,int index,DownloadProgress& progress);
    double sample(double latitude,double longitude,DownloadProgress& progress);
    std::filesystem::path cache_;Transport transport_;std::mutex mutex_;
    std::map<std::pair<int,int>,std::shared_ptr<File>> files_;std::map<std::pair<const File*,int>,std::vector<float>> blocks_;
};
// Decodes one DEFLATE + floating-point-predictor (TIFF 3) float32 tile.
std::vector<float> decodeFloatTile(const std::string& compressed,unsigned width,unsigned height);
CopernicusDem& copernicusDem(); // Cache in the data directory.
}
