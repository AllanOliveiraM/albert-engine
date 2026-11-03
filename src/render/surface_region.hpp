#pragma once
#include "geography.hpp"
#include <array>
#include <optional>
#include <span>

namespace albert {
// Eye/rays in the body's unscaled sphere frame. This also describes an
// ellipsoid after its axis scale has been removed. Angles are geodetic degrees.
// The caller samples the actual view, including a small screen-space margin.
inline std::optional<GeoRegion> surfaceRegion(const std::array<double,3>& eye,
        std::span<const std::array<double,3>> rays,double grid,
        std::optional<GeoRegion> required=std::nullopt,double radius=1.,double flattening=earthFlattening){
    if(!(radius>0&&grid>0&&flattening>0)||!std::isfinite(radius)||!std::isfinite(grid)||!std::isfinite(flattening))return std::nullopt;
    auto dot=[](const auto& a,const auto& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];};
    std::array<double,3> at{eye[0]/radius,eye[1]/radius,eye[2]/radius};
    const double distance=std::sqrt(dot(at,at));if(!(distance>1.)||!std::isfinite(distance))return std::nullopt;
    constexpr double degrees=180./std::numbers::pi;
    const double anchor=longitude(std::atan2(at[2],-at[0])*degrees-180.);
    double west=1e9,east=-1e9,south=90,north=-90;
    bool sampled=false,pole=false;
    auto add=[&](const std::array<double,3>& p){
        const double horizontal=std::hypot(p[0],p[2]);
        const double lat=std::atan2(p[1],flattening*horizontal)*degrees;
        const double lon=anchor+longitude(std::atan2(p[2],-p[0])*degrees-180.-anchor);
        west=std::min(west,lon);east=std::max(east,lon);south=std::min(south,lat);north=std::max(north,lat);
        pole=pole||horizontal<1e-10;sampled=true;
    };
    const double c=distance*distance-1.;
    for(auto ray:rays){
        const double length=std::sqrt(dot(ray,ray));if(!(length>0)||!std::isfinite(length))continue;
        for(auto& x:ray)x/=length;
        const double b=dot(at,ray);if(b>=0)continue; // The ray points away from the body.
        const double discriminant=b*b-c;
        std::array<double,3> point{};
        if(discriminant>=0){
            // Stable near-surface root: avoids subtracting nearly equal distances.
            const double t=c/(-b+std::sqrt(discriminant));
            for(unsigned i=0;i<3;++i)point[i]=at[i]+ray[i]*t;
        }else{
            // A downward ray just above the horizon misses the sphere. Include
            // the tangent point below it so a sky-facing corner cannot cut off
            // the visible ground at the limb.
            const std::array<double,3> up{at[0]/distance,at[1]/distance,at[2]/distance};
            std::array<double,3> tangent{};const double vertical=dot(ray,up);
            for(unsigned i=0;i<3;++i)tangent[i]=ray[i]-up[i]*vertical;
            const double length=std::sqrt(dot(tangent,tangent));if(!(length>0))continue;
            const double across=std::sqrt(std::max(0.,1.-1./(distance*distance)))/length;
            for(unsigned i=0;i<3;++i)point[i]=up[i]/distance+tangent[i]*across;
        }
        add(point);
    }
    if(required){
        const auto r=alignedRegion(*required,GeoRegion{anchor,0,anchor,0});
        west=std::min(west,r.west);east=std::max(east,r.east);south=std::min(south,r.south);north=std::max(north,r.north);
        pole=pole||r.north>=90||r.south<=-90;
    }
    if(!sampled&&!required)return std::nullopt;
    if(!std::isfinite(west+east+south+north)||east<west||north<south)return std::nullopt;
    const double snap=grid/8.;
    auto extent=[&](double lo,double hi,double limit){
        const double middle=(lo+hi)*.5,center=std::round(middle/snap)*snap;
        // Twenty per cent extra span plus one snap step on either side.
        const double half=(hi-lo)*.6+snap+std::abs(center-middle);
        return std::array<double,2>{center,std::min(limit,std::max(grid,std::ceil(2*half/grid)*grid))};
    };
    const auto lon=extent(west,east,360.),lat=extent(south,north,180.);
    const double bottom=std::clamp(lat[0]-lat[1]*.5,-90.,90.-lat[1]);
    if(pole||bottom<=-90||bottom+lat[1]>=90)return GeoRegion{anchor-180,bottom,anchor+180,bottom+lat[1]};
    return GeoRegion{lon[0]-lon[1]*.5,bottom,lon[0]+lon[1]*.5,bottom+lat[1]};
}
}
