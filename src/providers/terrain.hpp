#pragma once
#include "net/download.hpp"
#include "physics/earth.hpp"
#include <filesystem>
#include <functional>
#include <map>
#include <tuple>
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
// LRO LOLA LDEM_128 (Moon: heights above the 1737.4 km sphere) and MGS MOLA MEGDR 128 px/°
// radii (Mars, planetocentric, ±88°) from the PDS Geosciences Node: pixel-is-area grids read
// by byte range, two pixels per row, bilinear (~237 m posts).
class PlanetaryRelief {
public:
    using Transport=CopernicusDem::Transport;
    explicit PlanetaryRelief(Transport transport={});
    // Height above the flight datum (Moon sphere, Mars ellipsoid) under a geodetic site (degrees).
    double height(physics::Center body,double latitude,double longitude,DownloadProgress& progress);
private:
    double pixel(physics::Center body,int row,int column,DownloadProgress& progress); // Moon: height; Mars: radius (m).
    double bilinear(physics::Center body,double latitude,double longitude,DownloadProgress& progress);
    Transport transport_;std::mutex mutex_;std::map<std::tuple<int,int,int>,double> pixels_;
};
PlanetaryRelief& planetaryRelief();
}
