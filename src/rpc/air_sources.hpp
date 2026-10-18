#pragma once
#include "physics/msis.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace albert::rpc {
// Air for flights. weather: live air around a site (degrees) at an instant (Unix UTC) with the
// raw GRIB2 it came from (default NOAA GFS); upper: upper or climatological air with the daily
// indices it used (default NRLMSISE-00 with CelesTrak). The raw inputs go into flight logs.
struct LiveAir {std::shared_ptr<const physics::AirModel> model;std::vector<std::string> grib;};
struct UpperAir {std::shared_ptr<const physics::AirModel> model;std::vector<physics::SpaceWeatherDay> days;};
struct AirSources {std::function<LiveAir(double latitude,double longitude,double utc)> weather;std::function<UpperAir(double utc)> upper;};
}
