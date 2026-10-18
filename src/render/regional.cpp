#include "regional.hpp"
#include "wms.hpp"
#include "physics/earth.hpp"
#include "providers/terrain.hpp"
#include "stb_image.h"
#include "asset_pack.hpp"
#include "core/bodies.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include <numbers>

using namespace threepp;
#include "threepp/loaders/ImageLoader.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <cmath>
#include <thread>
#include <mutex>

albert::RegionalImagery::~RegionalImagery() {progress.cancelled=true;if(pending_.valid()) pending_.wait();}
void albert::RegionalImagery::retry() {loaded_.reset();next_={};error.clear();}
void albert::RegionalImagery::request(std::optional<GeoRegion> region,std::string body,int level) {
    if(region!=desired_ || body!=body_ || level!=level_) {
        if(pending_.valid() && (!region || workingBody_!=body || level!=level_ || (working_ && !regionRelevant(*working_,*region)))) progress.cancelled=true;
        desired_=region;body_=std::move(body);level_=level;next_={};
    }
    if(!region) progress.cancelled=true;
}
std::optional<albert::RegionalImages> albert::RegionalImagery::poll() {
    const auto now=std::chrono::steady_clock::now();
    if(pending_.valid()) {
        if(pending_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return std::nullopt;
        auto result=pending_.get();
        working_.reset();
        if(progress.cancelled || !desired_ || !regionRelevant(result.region,*desired_) || result.body!=body_ || result.level!=level_) return std::nullopt;
        error=result.error;next_=error.empty() ? now+std::chrono::milliseconds(80) : now+std::chrono::minutes(2);
        if(result.day || result.night) {loaded_=result.region;loadedBody_=result.body;loadedLevel_=result.level;}
        return result;
    }
    if(!desired_ || (loaded_ && regionSufficient(*loaded_,*desired_) && loadedBody_==body_ && loadedLevel_==level_ && error.empty()) || now<next_) return std::nullopt;
    progress.cancelled=false;
    const auto region=*desired_;const auto body=body_;const int level=level_;
    working_=region;workingBody_=body;
    pending_=std::async(std::launch::async,[this,region,body,level] {
        RegionalImages result;result.region=region;result.body=body;result.level=level;
        try {
            const auto pack=visualAssetPack();
            result.textureOnly=pack->hasPayload("v1/"+body+"/shape.bin");
            progress.stage("Lendo detalhes incluidos",0);
            bool site=false;
            if(body=="earth"&&region.east-region.west<=2.)try{ // Launch-site detail: 31 m Landsat composite, Copernicus 30 m relief over the EGM2008 geoid.
                progress.stage("Baixando imagem Landsat (NASA GIBS)",0);
                const auto jpeg=fetchWms([](const GeoRegion& r,unsigned w,unsigned h){std::ostringstream u;u<<std::setprecision(12)<<"https://gibs.earthdata.nasa.gov/wms/epsg4326/best/wms.cgi?SERVICE=WMS&REQUEST=GetMap&VERSION=1.3.0"
                    "&LAYERS=Landsat_WELD_CorrectedReflectance_TrueColor_Global_Annual&CRS=EPSG:4326&BBOX="<<r.south<<","<<r.west<<","<<r.north<<","<<r.east<<"&WIDTH="<<w<<"&HEIGHT="<<h<<"&FORMAT=image/jpeg&TIME=2000-12-01";return u.str();},region,2048,2048,progress);
                int w=0,h=0;std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> rgba(stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(jpeg.data()),static_cast<int>(jpeg.size()),&w,&h,nullptr,4),stbi_image_free);
                if(!rgba)throw std::runtime_error("Unreadable Landsat image");
                progress.stage("Baixando relevo Copernicus",0);constexpr unsigned posts=385;
                const auto heights=providers::copernicusDem().heights(region.west,region.south,region.east,region.north,posts,posts,progress);
                const float geoid=static_cast<float>(physics::geoidHeight((region.south+region.north)*std::numbers::pi/360.,(region.west+region.east)*std::numbers::pi/360.));
                std::vector<float> relief(static_cast<std::size_t>(posts)*posts*4,1.f);for(std::size_t i=0;i<heights.size();++i)relief[4*i]=heights[i]+geoid; // Ellipsoidal.
                result.day=Image(std::vector<unsigned char>(rgba.get(),rgba.get()+static_cast<std::size_t>(w)*h*4),static_cast<unsigned>(w),static_cast<unsigned>(h));
                result.height=Image(std::move(relief),posts,posts);result.lift=.5f;result.segments=posts-1;site=true;
            }catch(const std::exception& e){if(progress.cancelled)throw;std::cerr<<"Terreno do sitio indisponivel: "<<e.what()<<std::endl;}
            if(!site)result.day=pack->image(body+"-day",region,4096,progress);
            if(body=="earth") result.night=pack->image("earth-night",region,4096,progress);
            if(!site)result.height=pack->contains(body+"-height")?pack->image(body+"-height",region,1024,progress):Image(std::vector<float>{0,0,0,1},1,1);
            if(result.day) {
                auto& image=*result.day;auto& rgba=image.data();
                std::vector<unsigned char> rough(body=="earth"?rgba.size():0,255);
                const auto width=image.width(),height=image.height();
                for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
                    const size_t at=4*(static_cast<size_t>(y)*width+x);
                    if(body=="earth"){
                        const float water=std::clamp((rgba[at+2]-rgba[at]*1.15f)/12.f,0.f,1.f);
                        rough[at]=rough[at+1]=rough[at+2]=static_cast<unsigned char>(220.f-water*170.f);
                    }
                    const unsigned edge=std::min({x,y,width-1-x,height-1-y});
                    rgba[at+3]=static_cast<unsigned char>(rgba[at+3]*std::min(1.f,edge/(width*.025f)));
                }
                if(body=="earth") result.roughness=threepp::Image(std::move(rough),width,height);
            }
            progress.checkCancelled();
            prepareRegionalImages(result);
            progress.checkCancelled();
        } catch(const std::exception& e) {
            result.error=e.what();result.day.reset();result.night.reset();result.height.reset();result.roughness.reset();
            result.geometry.reset();result.surfaceMask.reset();
        }
        return result;
    });
    return std::nullopt;
}

void albert::prepareRegionalImages(RegionalImages& images) {
    if(!images.day || images.geometry || images.textureOnly) return;
    constexpr double pi=std::numbers::pi;
    const auto& region=images.region;
    constexpr unsigned maskWidth=2048,maskHeight=1024;
    std::vector<unsigned char> mask(maskWidth*maskHeight*4,255);
    for(unsigned y=0;y<maskHeight;++y) for(unsigned x=0;x<maskWidth;++x) {
        const double lat=(y+.5)*180./maskHeight-90.;
        if(lat<=region.south || lat>=region.north) continue;
        const double lon=(x+.5)*360./maskWidth-180.;
        double delta=lon-region.west;delta-=360.*std::floor(delta/360.);
        const double u=delta/(region.east-region.west),v=(lat-region.south)/(region.north-region.south);
        if(u>.025 && u<.975 && v>.025 && v<.975) {
            const size_t at=4*(static_cast<size_t>(y)*maskWidth+x);
            mask[at]=mask[at+1]=mask[at+2]=0;
        }
    }
    images.surfaceMask=Image(std::move(mask),maskWidth,maskHeight);
    const float radians=static_cast<float>(pi/180.);
    const float radius=static_cast<float>(core::surfaceRadius(images.body)/core::earthRadiusMeters);
    const unsigned segments=images.segments?images.segments:images.body=="earth"||images.body=="moon" ?
        std::clamp(std::max(images.day->width(),images.day->height())/8,192u,384u):192u;
    auto geometry=SphereGeometry::create(radius,segments,segments,
        static_cast<float>(region.west+180.)*radians,static_cast<float>(region.east-region.west)*radians,
        static_cast<float>(90.-region.north)*radians,static_cast<float>(region.north-region.south)*radians);
    if(images.height) {
        const auto& height=*images.height;
        auto* position=geometry->getAttribute<float>("position");
        for(unsigned y=0;y<=segments;++y) for(unsigned x=0;x<=segments;++x) {
            // Interpolate the measured DEM. Nearest-neighbour vertices create
            // terraces/striped normals when the mesh is denser than its source.
            const double px=double(x)*(height.width()-1)/segments,py=double(segments-y)*(height.height()-1)/segments;
            const unsigned x0=static_cast<unsigned>(px),y0=static_cast<unsigned>(py);
            const unsigned x1=std::min(x0+1,height.width()-1),y1=std::min(y0+1,height.height()-1);
            auto altitude=[&](unsigned a,unsigned b){return height.data<float>()[4*(static_cast<size_t>(b)*height.width()+a)];};
            const float meters=std::lerp(std::lerp(altitude(x0,y0),altitude(x1,y0),float(px-x0)),
                std::lerp(altitude(x0,y1),altitude(x1,y1),float(px-x0)),float(py-y0));
            const unsigned i=y*(segments+1)+x;
            Vector3 point(position->getX(i),position->getY(i),position->getZ(i));
            if(images.body=="earth") {
                const double latitude=albert::parametricLatitude((region.north-y*(region.north-region.south)/segments)*pi/180.);
                const double phi=(region.west+180.+x*(region.east-region.west)/segments)*pi/180.;
                point.set(static_cast<float>(-std::cos(phi)*std::cos(latitude)),static_cast<float>(std::sin(latitude)),
                    static_cast<float>(std::sin(phi)*std::cos(latitude)));
            }
            point.normalize().multiplyScalar(radius+(meters+images.lift)/6378137.f);
            position->setXYZ(i,point.x,point.y,point.z);
        }
        geometry->computeVertexNormals();geometry->computeBoundingSphere();
    }
    std::vector<float> globalUv;
    const auto* uv=geometry->getAttribute<float>("uv");
    globalUv.reserve(uv->count()*2);
    for(int i=0;i<uv->count();++i) {
        globalUv.push_back(static_cast<float>((region.west+180.+uv->getX(i)*(region.east-region.west))/360.));
        globalUv.push_back(static_cast<float>((region.south+90.+uv->getY(i)*(region.north-region.south))/180.));
    }
    geometry->setAttribute("uv2",FloatBufferAttribute::create(std::move(globalUv),2));
    images.geometry=std::move(geometry);
}
