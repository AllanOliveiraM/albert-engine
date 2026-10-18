#pragma once
#include "net/download.hpp"
#include "physics/msis.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
namespace albert::providers {
// CelesTrak space-weather days (SW-*.csv): observed, daily predicted or monthly predicted.
using SpaceWeatherDay=physics::SpaceWeatherDay;
std::vector<SpaceWeatherDay> parseSpaceWeather(const std::string& csv);
// NRLMSISE-00 inputs at an instant (Unix UTC): previous-day F10.7, 81-day centred mean, daily
// Ap and, when the last four days have 3-hourly values, the ap history.
std::optional<physics::SpaceWeather> spaceWeatherAt(const std::vector<SpaceWeatherDay>& days,double utc,std::string* note=nullptr);
class SpaceWeatherService {
public:
    using Transport=std::function<std::string(const std::string&,DownloadProgress*)>;
    using Now=std::function<double()>;
    explicit SpaceWeatherService(std::filesystem::path cache,Transport transport={},Now now={});
    // Refreshes the last five years twice a day and the full history weekly, on demand.
    std::shared_ptr<const physics::Msis> msis(double utc,std::vector<SpaceWeatherDay>* used=nullptr); // used: days from 4 before to 60 after, for replay.
private:
    std::vector<SpaceWeatherDay> table(const std::string& name,double maximumAge);
    std::filesystem::path cache_;Transport transport_;Now now_;std::mutex mutex_;
};
SpaceWeatherService& spaceWeather();
std::shared_ptr<const physics::Msis> msisFrom(const std::vector<SpaceWeatherDay>& days,double utc); // Null without indices for utc.
}
