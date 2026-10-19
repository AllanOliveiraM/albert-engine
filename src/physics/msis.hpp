#pragma once
#include "physics/atmosphere.hpp"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace albert::physics {
// NRLMSISE-00 inputs: previous-day and 81-day centred F10.7 (sfu); ap: daily, current 3 h,
// 3, 6 and 9 h before, mean of 12-33 h and of 36-57 h before. history selects the 3-hourly
// form (switch 9 = -1) instead of the daily one.
struct SpaceWeather {double f107=150,f107a=150;std::array<double,7> ap{4,4,4,4,4,4,4};bool history=false;};
// Number densities (m⁻³): He, O, N2, O2, Ar, [total mass kg/m³], H, N, anomalous O;
// exospheric and local temperatures (K).
struct MsisOutput {std::array<double,9> density{};double exospheric=0,temperature=0;};
MsisOutput nrlmsise00(int dayOfYear,double secondsUt,double altitudeKm,double latitudeDeg,double longitudeDeg,double localSolarHours,const SpaceWeather& weather,bool anomalousOxygen=true);
// Daily solar and geomagnetic indices (CelesTrak layout): previous-day F10.7 comes from the
// day before, the 81-day centred mean and daily Ap from the day itself, and the ap history from
// 3-hourly values when the last four days have them.
struct SpaceWeatherDay {double day=0;double f107=0,f107a=0,apAverage=0;std::array<double,8> ap{};bool hasAp=false;std::string type;}; // day: Unix days.
class SpaceWeatherTable {
public:
    explicit SpaceWeatherTable(const std::vector<SpaceWeatherDay>& days);
    std::optional<SpaceWeather> at(double utc,std::string* note=nullptr)const;
private:
    const SpaceWeatherDay* day(double d)const;
    double first_=0;std::vector<SpaceWeatherDay> days_;std::vector<bool> present_;
};
// NRLMSISE-00 (Picone et al. 2002) as an air model, 0 to 1000 km; total mass density with
// anomalous oxygen (gtd7d), pressure from the number densities; horizontal winds from HWM14 with
// the 3-hour ap (the daily one without history). With a table the indices follow the instant;
// outside it (or without one) the fixed indices apply.
class Msis final:public AirModel {
public:
    Msis(SpaceWeather weather,std::string source,std::shared_ptr<const SpaceWeatherTable> table={}):weather_(weather),source_(std::move(source)),table_(std::move(table)){}
    Air at(std::int64_t tt,double latitude,double longitude,double height)const override;
    std::string source()const override{return source_;}
    const SpaceWeather& weather()const{return weather_;}
private:
    SpaceWeather weather_;std::string source_;std::shared_ptr<const SpaceWeatherTable> table_;
};
}
