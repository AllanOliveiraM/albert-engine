#include "surface_region.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
using Point=std::array<double,3>;
constexpr double radians=std::numbers::pi/180.;
int failures=0;
void check(bool ok,const char* name){std::printf("%s %s\n",ok?"ok  ":"FAIL",name);failures+=!ok;}
Point position(double lon,double lat,double radius=1.){
    // Independent ellipsoid relation, not the geographic helper under test.
    const double parameter=std::atan(albert::earthFlattening*std::tan(lat*radians));
    return {radius*std::cos(lon*radians)*std::cos(parameter),radius*std::sin(parameter),-radius*std::sin(lon*radians)*std::cos(parameter)};
}
Point toward(const Point& from,const Point& to){return {to[0]-from[0],to[1]-from[1],to[2]-from[2]};}
bool contains(const albert::GeoRegion& region,double lon,double lat){
    const double center=(region.west+region.east)*.5;
    const double aligned=center+std::remainder(lon-center,360.);
    return (region.east-region.west>=360.-1e-8||(aligned>=region.west-1e-8&&aligned<=region.east+1e-8))&&lat>=region.south-1e-8&&lat<=region.north+1e-8;
}
}
int main(){
    const Point eye{0,0,1.05};
    const auto a=position(-81,4),b=position(-79,8),c=position(-82,6);
    const std::array<Point,3> tilted{toward(eye,a),toward(eye,b),toward(eye,c)};
    const auto view=albert::surfaceRegion(eye,tilted,.1);
    check(view&&contains(*view,-81,4)&&contains(*view,-79,8)&&contains(*view,-82,6),"Tilted view covers independently specified visible surface points");
    check(view&&!contains(*view,-90,0),"The crop follows the viewed ground instead of the point directly below the eye");
    check(view&&view->west<-82&&view->east>-79&&view->south<4&&view->north>8,"The footprint has a margin beyond its visible samples");
    const albert::GeoRegion site{-90.1,-.1,-89.9,.1};
    const auto withSite=albert::surfaceRegion(eye,tilted,.1,site);
    check(withSite&&contains(*withSite,-90.1,-.1)&&contains(*withSite,-89.9,.1)&&contains(*withSite,-79,8),"A vehicle's site extends the viewed crop without replacing its footprint");

    const Point lowEye{0,0,1.001};const std::array<Point,1> grazing{Point{1,0,-.001}};
    const auto horizon=albert::surfaceRegion(lowEye,grazing,.01);
    const double limb=-90+std::acos(1/lowEye[2])/radians;
    check(horizon&&contains(*horizon,limb,0),"A downward ray above the horizon includes the tangent ground below it");
    const std::array<Point,1> away{Point{0,0,1}};
    check(!albert::surfaceRegion(lowEye,away,.01),"A sky-only view has no artificial ground footprint");
    const auto skySite=albert::surfaceRegion(lowEye,away,.01,site);
    check(skySite&&contains(*skySite,-90,0),"The explicit site remains available while looking at the sky");

    const auto dateEye=position(179.99,0,1.001);const std::array<Point,1> seam{toward(dateEye,position(-179.98,.01))};
    const auto date=albert::surfaceRegion(dateEye,seam,.01,albert::GeoRegion{179.96,-.01,180.01,.01});
    check(date&&date->east-date->west<1&&contains(*date,-179.98,.01)&&contains(*date,179.96,-.01),"A crop crossing the date line stays local and covers both longitudes");
    const std::array<Point,1> poleRay{Point{0,-1,0}};const auto pole=albert::surfaceRegion(Point{0,1.05,0},poleRay,.1);
    check(pole&&pole->east-pole->west==360&&pole->north==90,"A polar footprint spans all longitudes without exceeding the pole");
    const auto nearPoleEye=position(40,89,1.001);const std::array<Point,1> offCenterPole{toward(nearPoleEye,Point{0,1,0})};
    const auto polarEdge=albert::surfaceRegion(nearPoleEye,offCenterPole,.01);
    check(polarEdge&&polarEdge->north==90&&polarEdge->east-polarEdge->west==360,"An off-center visible pole still expands the crop to all longitudes");

    const std::array<Point,1> down{Point{0,0,-1}};
    check(albert::surfaceRegion(Point{0,0,.999995},down,.01,std::nullopt,.99999).has_value(),"A camera over a negative geoid height uses the terrain's reference radius");
    check(!albert::surfaceRegion(Point{0,0,.9},down,.01),"An eye below the supplied reference surface is rejected");
    const double nan=std::numeric_limits<double>::quiet_NaN();
    check(!albert::surfaceRegion(eye,down,0)&&!albert::surfaceRegion(eye,down,nan)&&!albert::surfaceRegion(eye,down,.1,std::nullopt,0),"Invalid scales produce no crop");
    const std::array<Point,2> invalid{Point{0,0,0},Point{nan,0,-1}};
    check(!albert::surfaceRegion(eye,invalid,.1),"Invalid rays are skipped without producing non-finite bounds");
    // A wide view, including corners, must retain the whole sampled footprint
    // as camera height and longitude vary. No GPU or network data is needed.
    for(double height:{.0001,.005,.05,.2})for(double longitude:{-179.99,-97.15,0.,179.99}){
        const auto camera=position(longitude,26,1+height);std::vector<Point> rays;
        const double angle=std::acos(1/(1+height))/radians*.3;
        for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)rays.push_back(toward(camera,position(longitude+x*angle,26+y*angle)));
        const auto crop=albert::surfaceRegion(camera,rays,.01);bool covered=bool(crop);
        if(crop)for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x)covered=covered&&contains(*crop,longitude+x*angle,26+y*angle);
        check(covered,"Wide footprint retains its surface samples across zoom and longitude");
    }
    return failures?1:0;
}
