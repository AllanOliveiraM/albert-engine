#pragma once
#include "physics/vector.hpp"
#include <cstdint>
#include <string>
namespace albert::physics {
struct Air {double temperature=0,pressure=0,density=0,sound=0;V3 wind;}; // K, Pa, kg/m³, m/s; wind toward NED (m/s).
// U.S. Standard Atmosphere 1976 below 86 km geometric (exact layer equations; the
// lowest layer extends to -5 km). Above 86 km: vacuum until an upper model replaces it.
Air standardAtmosphere(double geometricHeight);
constexpr double standardTop=86000.;
// Weather or climatology at an instant (TT ns), geodetic latitude/longitude (rad) and
// ellipsoidal height (m). Implementations are immutable snapshots, so flights replay.
class AirModel {
public:
    virtual ~AirModel()=default;
    virtual Air at(std::int64_t tt,double latitude,double longitude,double height)const=0;
    virtual std::string source()const=0;
};
}
