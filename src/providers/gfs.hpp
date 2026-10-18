#pragma once
#include "net/download.hpp"
#include "physics/weather.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>
namespace albert::providers {
// One GRIB2 field: templates 3.0 (latitude/longitude), 4.0 and 5.0 (simple packing), no bitmap.
struct GribField {
    int discipline=0,category=0,number=0,surface=0;double level=0; // Level in the surface's unit (Pa for isobaric).
    double reference=0;int forecastMinutes=0;                      // Reference time (Unix UTC).
    int ni=0,nj=0,scan=0;double la1=0,lo1=0,di=0,dj=0;             // Degrees.
    std::vector<float> values;
};
std::vector<GribField> decodeGrib2(std::string_view bytes);
// Isobaric HGT, TMP, UGRD, VGRD and SPFH of each forecast hour, on one grid.
physics::WeatherGrid gfsGrid(const std::vector<std::vector<GribField>>& hours,std::string source);
// Latest NOAA GFS 0.25° cycle around a point: the two forecast hours bracketing `utc`,
// ±6° subregion from the NOMADS filter, cycle found through the AWS open-data index.
class Gfs {
public:
    using Transport=std::function<std::string(const std::string&,DownloadProgress*)>;
    explicit Gfs(std::filesystem::path cache,Transport transport={});
    std::shared_ptr<const physics::Weather> around(double latitude,double longitude,double utc,DownloadProgress* progress=nullptr){return fetch(latitude,longitude,utc,progress).model;}
    struct Download {std::shared_ptr<const physics::Weather> model;std::vector<std::string> grib;}; // Raw GRIB2 of both hours, for replay.
    Download fetch(double latitude,double longitude,double utc,DownloadProgress* progress=nullptr);
private:
    std::filesystem::path cache_;Transport transport_;std::mutex mutex_;
};
Gfs& gfs();
}
