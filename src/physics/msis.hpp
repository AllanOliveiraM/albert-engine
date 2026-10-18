#pragma once
#include "physics/atmosphere.hpp"
#include <array>
#include <string>
namespace albert::physics {
// NRLMSISE-00 inputs: previous-day and 81-day centred F10.7 (sfu); ap: daily, current 3 h,
// 3, 6 and 9 h before, mean of 12-33 h and of 36-57 h before. history selects the 3-hourly
// form (switch 9 = -1) instead of the daily one.
struct SpaceWeather {double f107=150,f107a=150;std::array<double,7> ap{4,4,4,4,4,4,4};bool history=false;};
// Number densities (m⁻³): He, O, N2, O2, Ar, [total mass kg/m³], H, N, anomalous O;
// exospheric and local temperatures (K).
struct MsisOutput {std::array<double,9> density{};double exospheric=0,temperature=0;};
MsisOutput nrlmsise00(int dayOfYear,double secondsUt,double altitudeKm,double latitudeDeg,double longitudeDeg,double localSolarHours,const SpaceWeather& weather,bool anomalousOxygen=true);
// NRLMSISE-00 (Picone et al. 2002) as an air model, 0 to 1000 km; total mass density with
// anomalous oxygen (gtd7d), pressure from the number densities; no winds.
class Msis final:public AirModel {
public:
    Msis(SpaceWeather weather,std::string source):weather_(weather),source_(std::move(source)){}
    Air at(std::int64_t tt,double latitude,double longitude,double height)const override;
    std::string source()const override{return source_;}
    const SpaceWeather& weather()const{return weather_;}
private:
    SpaceWeather weather_;std::string source_;
};
}
