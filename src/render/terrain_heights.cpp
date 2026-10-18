#include "terrain.hpp"
#include "physics/earth.hpp"
#include "providers/terrain.hpp"
#include "threepp/textures/Image.hpp"
#include <algorithm>
#include <optional>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>
// Height sources of the terrain: global grids of the visual pack, refined from a level by the
// cached Copernicus, LOLA and MOLA data (missing pieces are noted for the fetcher).
namespace albert {
namespace {
constexpr double pi=std::numbers::pi;
using threepp::Image;
// EGM2008 (degree 70, wavelengths over ~5°) on a 2° grid, built once by the first caller (~1 s):
// the terrain's worker then interpolates it instead of 54 µs a vertex.
double geoidGrid(double lat,double lon) {
    static const std::vector<float> grid=[]{std::vector<float> g(91*181);
        for(int j=0;j<91;++j)for(int i=0;i<181;++i)g[static_cast<std::size_t>(j*181+i)]=static_cast<float>(physics::geoidHeight((j*2-90)*pi/180,(i*2-180)*pi/180));return g;}();
    const double x=std::clamp((lon*180/pi+180)/2,0.,179.999),y=std::clamp((lat*180/pi+90)/2,0.,89.999);const auto i=static_cast<std::size_t>(x),j=static_cast<std::size_t>(y);const double fx=x-static_cast<double>(i),fy=y-static_cast<double>(j);
    auto g=[&](std::size_t a,std::size_t b){return static_cast<double>(grid[b*181+a]);};
    return (1-fy)*((1-fx)*g(i,j)+fx*g(i+1,j))+fy*((1-fx)*g(i,j+1)+fx*g(i+1,j+1));
}
// A global equirectangular height image (RGBA floats, metres in red, south row first), bilinear
// at texel centres; latitude and longitude in radians.
// A cached lookup that never throws at the terrain's worker: an error (a corrupt piece) reads as not
// cached, so the node stays provisional on the coarse grid.
template<class Lookup> std::optional<double> safely(Lookup lookup){try{return lookup();}catch(const std::exception&){return std::nullopt;}}
struct GlobalHeights {
    std::shared_ptr<const std::vector<float>> grid;long w=0,h=0;
    explicit GlobalHeights(const Image& image):grid(std::make_shared<const std::vector<float>>(image.data<float>())),w(image.width()),h(image.height()){}
    double operator()(double lat,double lon)const{
        const auto& g=*grid;auto at=[&](long x,long y){x=((x%w)+w)%w;y=std::clamp<long>(y,0,h-1);return static_cast<double>(g[static_cast<std::size_t>(4*(y*w+x))]);};
        const double x=(lon/pi*.5+.5)*static_cast<double>(w)-.5,y=(lat/pi+.5)*static_cast<double>(h)-.5,fx=x-std::floor(x),fy=y-std::floor(y);
        const long x0=static_cast<long>(std::floor(x)),y0=static_cast<long>(std::floor(y));
        return (1-fy)*((1-fx)*at(x0,y0)+fx*at(x0+1,y0))+fy*((1-fx)*at(x0,y0+1)+fx*at(x0+1,y0+1));
    }
};
// Earth's relief for the terrain: the global grid (GEBCO, 0-6400 m) bilinear, Copernicus GLO-30
// from level 9 (posts ≤ 300 m) when cached (missing pieces are noted for the fetcher; the node is
// provisional meanwhile), plus the EGM2008 geoid (both over mean sea level). Metres over the ellipsoid.
TerrainHeights earthHeights(const Image& global) {
    return [coarse=GlobalHeights(global)](const std::vector<double>& lat,const std::vector<double>& lon,int level,std::vector<float>& out) {
        bool fine=level>=9;
        if(fine){auto& dem=providers::copernicusDem(); // All samples, so every missing piece gets noted at once.
            for(std::size_t i=0;i<lat.size();++i)if(const auto v=safely([&]{return dem.cachedHeight(lat[i]*180/pi,lon[i]*180/pi);}))out[i]=static_cast<float>(*v);else fine=false;}
        for(std::size_t i=0;i<lat.size();++i)out[i]=static_cast<float>((fine?out[i]:coarse(lat[i],lon[i]))+geoidGrid(lat[i],lon[i]));
        return fine||level<9;
    };
}
// The Moon's: the global grid of the visual pack, LOLA LDEM_128 blocks (~237 m) from level 6 (posts
// ≤ 666 m) when cached. Metres over the 1737.4 km sphere.
TerrainHeights moonHeights(const Image& global) {
    return [coarse=GlobalHeights(global)](const std::vector<double>& lat,const std::vector<double>& lon,int level,std::vector<float>& out) {
        bool fine=level>=6;
        if(fine){auto& relief=providers::planetaryRelief();
            for(std::size_t i=0;i<lat.size();++i)if(const auto v=safely([&]{return relief.cachedHeight(physics::Center::moon,lat[i]*180/pi,lon[i]*180/pi);}))out[i]=static_cast<float>(*v);else fine=false;}
        if(!fine)for(std::size_t i=0;i<lat.size();++i)out[i]=static_cast<float>(coarse(lat[i],lon[i]));
        return fine||level<6;
    };
}

}
TerrainHeights earthTerrainHeights(const Image& global){return earthHeights(global);}
TerrainHeights moonTerrainHeights(const Image& global){return moonHeights(global);}
// Mars: the pack's grid, MOLA MEGDR blocks (~463 m) from level 7 (posts ≤ 650 m). Metres over the
// flight ellipsoid (3396.19 × 3376.2 km), as the physics.
TerrainHeights marsTerrainHeights(const Image& global){
    return [coarse=GlobalHeights(global)](const std::vector<double>& lat,const std::vector<double>& lon,int level,std::vector<float>& out){
        const bool polar=std::any_of(lat.begin(),lat.end(),[](double l){return std::abs(l)>88*pi/180;}); // MOLA stops at ±88°: the grid only, complete.
        bool fine=level>=7&&!polar;
        if(fine){auto& relief=providers::planetaryRelief();
            for(std::size_t i=0;i<lat.size();++i)if(const auto v=safely([&]{return relief.cachedHeight(physics::Center::mars,lat[i]*180/pi,lon[i]*180/pi);}))out[i]=static_cast<float>(*v);else fine=false;}
        if(!fine)for(std::size_t i=0;i<lat.size();++i)out[i]=static_cast<float>(coarse(lat[i],lon[i]));
        return fine||level<7||polar;
    };
}
}
