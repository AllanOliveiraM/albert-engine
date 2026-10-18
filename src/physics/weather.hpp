#pragma once
#include "physics/atmosphere.hpp"
#include "physics/earth.hpp"
#include <string>
#include <vector>
namespace albert::physics {
// Gridded weather on isobaric levels (NOAA GFS): regular latitude/longitude grid, rows
// south to north, levels from the surface up, valid times ascending. Heights are
// geopotential (gpm above mean sea level).
struct WeatherGrid {
    double south=0,west=0,step=.25;int rows=0,columns=0; // Degrees; west in [-180, 180).
    std::vector<double> pressure;                          // Pa, decreasing.
    struct Time {Tt valid=0;std::vector<float> height,temperature,east,north,humidity;}; // [level][row][column]: gpm, K, m/s, m/s, kg/kg.
    std::vector<Time> times;
    std::string source;
};
// Linear in time, bilinear horizontally, linear in geopotential height between levels
// (pressure log-linear); geoid from EGM2008 and latitude-dependent normal gravity convert
// ellipsoidal heights. Over the top 8 km of the grid, and within 0.5° of its edges, it
// blends into the standard atmosphere, used alone above and outside.
class Weather final:public AirModel {
public:
    explicit Weather(WeatherGrid grid);
    Air at(std::int64_t tt,double latitude,double longitude,double height)const override;
    std::string source()const override{return grid_.source;}
    const WeatherGrid& grid()const{return grid_;}
private:
    WeatherGrid grid_;std::vector<double> geoid_;
};
}
