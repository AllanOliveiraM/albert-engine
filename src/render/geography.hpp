#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>

namespace albert {
constexpr double earthFlattening=6356.752314245/6378.137;
// Latitude of an ellipsoid's sphere parameter differs from geodetic latitude.
// Imagery and geographic vectors use the latter; geometry uses the former.
inline double geodeticLatitude(double parametric) {
    return std::atan2(std::sin(parametric),earthFlattening*std::cos(parametric));
}
inline double parametricLatitude(double geodetic) {
    return std::atan2(earthFlattening*std::sin(geodetic),std::cos(geodetic));
}
struct GeoRegion {
    double west = -180, south = -90, east = 180, north = 90;
    bool operator==(const GeoRegion&) const = default;
};
inline double longitude(double value) { return value - 360. * std::floor((value + 180.) / 360.); }
// Compare unwrapped bounds across the date line; small motion should not
// invalidate an in-flight crop that still improves the visible region.
inline GeoRegion alignedRegion(GeoRegion region,const GeoRegion& reference) {
    const double shift=360.*std::round(((reference.west+reference.east)-(region.west+region.east))/720.);
    region.west+=shift;region.east+=shift;return region;
}
inline bool regionRelevant(GeoRegion crop,const GeoRegion& view) {
    crop=alignedRegion(crop,view);
    const double width=crop.east-crop.west>=360. ? view.east-view.west : std::max(0.,std::min(crop.east,view.east)-std::max(crop.west,view.west));
    const double height=std::max(0.,std::min(crop.north,view.north)-std::max(crop.south,view.south));
    return width*height>=.25*(view.east-view.west)*(view.north-view.south);
}
inline bool regionSufficient(GeoRegion crop,const GeoRegion& view) {
    crop=alignedRegion(crop,view);
    return (crop.east-crop.west>=360. || (crop.west<=view.west && crop.east>=view.east)) && crop.south<=view.south && crop.north>=view.north &&
        crop.east-crop.west<=1.35*(view.east-view.west) && crop.north-crop.south<=1.35*(view.north-view.south);
}
// A circular bound around the entire camera frustum/sphere intersection.
// Including diagonal rays also covers wide windows and rotated views. Quantize
// with an overlap margin, so a cell boundary never cuts through the viewport.
inline GeoRegion viewRegion(double lon,double lat,double distance,double radius,double aspect=16./9.,double fov=60.,double grid=11.25) {
    constexpr double radians=std::numbers::pi/180.;
    const double d=std::max(1.000001,distance/radius);
    const double theta=std::atan(std::tan(fov*radians*.5)*std::sqrt(1.+aspect*aspect));
    const double angle=(d*std::sin(theta)>=1. ? std::acos(1./d) : std::asin(d*std::sin(theta))-theta)/radians;
    const double half=std::min(90.,angle*1.08);
    const double snap=grid/8.;
    const double latitude=std::clamp(std::round(lat/snap)*snap,-90.,90.);
    const double center=std::round(longitude(lon)/snap)*snap;
    const double height=std::min(180.,std::max(grid,std::ceil((2.*half+2.*snap)/grid)*grid));
    const double south=std::clamp(latitude-height*.5,-90.,90.-height);
    const double longitudeHalf=std::abs(lat)+half>=90. ? 180. : std::asin(std::clamp(std::sin(half*radians)/std::cos(lat*radians),0.,1.))/radians;
    const double width=std::min(360.,std::max(grid,std::ceil((2.*longitudeHalf+2.*snap)/grid)*grid));
    return {center-width*.5,south,center+width*.5,south+height};
}
inline GeoRegion cloudRegion(double lon,double lat,double distance,double aspect=16./9.,double fov=60.) {
    return viewRegion(lon,lat,distance,1.,aspect,fov);
}
}
