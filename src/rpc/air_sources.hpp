#pragma once
#include "physics/msis.hpp"
#include "providers/gravity_models.hpp"
#include "providers/terrain.hpp"
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
// gravity: a Moon or Mars field of a degree (default the PDS download, cached); relief: the
// height of a Moon or Mars site (degrees) above the flight datum (default LOLA / MOLA).
struct AirSources {std::function<LiveAir(double latitude,double longitude,double utc)> weather;std::function<UpperAir(double utc)> upper;
    std::function<providers::GravityModel(physics::Center,int degree)> gravity;std::function<double(physics::Center,double latitude,double longitude)> relief;};
}
