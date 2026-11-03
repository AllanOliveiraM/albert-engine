#include "regional.hpp"
#include "platform.hpp"
#include <array>
#include "physics/earth.hpp"
#include "providers/terrain.hpp"
#include "asset_pack.hpp"
#include "core/bodies.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include <numbers>

using namespace threepp;
#include "threepp/loaders/ImageLoader.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cmath>
#include <thread>
#include <mutex>

albert::RegionalImagery::~RegionalImagery() {progress.cancelled=true;if(pending_.valid()) pending_.wait();}
void albert::RegionalImagery::retry() {
    // An asset upgrade/release during a load invalidates that result too. It
    // must not repopulate loaded_ and suppress the requested fresh load.
    if(pending_.valid())progress.cancelled=true;
    loaded_.reset();refresh_.reset();error.clear();
}
void albert::RegionalImagery::request(std::optional<GeoRegion> region,std::string body,int level) {
    // Losing sight of the surface pauses requests; it is not a switch back to
    // the default Earth source and must not bypass that body's failure delay.
    if(!region){desired_.reset();progress.cancelled=true;refresh_.clearChanges();return;}
    const bool sourceChanged=body!=body_||level!=level_;
    if(region!=desired_ || sourceChanged) {
        if(pending_.valid() && (workingBody_!=body || level!=level_ || (working_ && !regionRelevant(*working_,*region)))) progress.cancelled=true;
        if(sourceChanged){refresh_.reset();error.clear();}
        else if(loaded_||pending_.valid())refresh_.changed(RegionalRefreshSchedule::Clock::now());
        else refresh_.clearChanges();
        desired_=region;body_=std::move(body);level_=level;
    }
}
std::optional<albert::RegionalImages> albert::RegionalImagery::poll() {
    const auto now=std::chrono::steady_clock::now();
    if(pending_.valid()) {
        if(pending_.wait_for(std::chrono::seconds(0))!=std::future_status::ready) return std::nullopt;
        auto result=pending_.get();
        working_.reset();
        if(progress.cancelled || !desired_ || !regionRelevant(result.region,*desired_) || result.body!=body_ || result.level!=level_) return std::nullopt;
        error=result.error;refresh_.finished(now,!error.empty());
        if(result.day || result.night) {loaded_=result.region;loadedBody_=result.body;loadedLevel_=result.level;}
        return result;
    }
    if(!desired_)return std::nullopt;
    if(loaded_ && regionSufficient(*loaded_,*desired_) && loadedBody_==body_ && loadedLevel_==level_ && error.empty()) {
        refresh_.clearChanges();return std::nullopt;
    }
    if(!refresh_.ready(now))return std::nullopt;
    refresh_.clearChanges();
    progress.cancelled=false;
    const auto region=*desired_;const auto body=body_;const int level=level_;
    working_=region;workingBody_=body;
    pending_=std::async(std::launch::async,[this,region,body,level] {backgroundPriority();
        RegionalImages result;result.region=region;result.body=body;result.level=level;
        try {
            const auto pack=visualAssetPack();
            result.textureOnly=body=="earth"||pack->hasPayload("v1/"+body+"/shape.bin");
            progress.stage("Lendo detalhes incluidos",0);
            result.day=pack->image(body+"-day",region,4096,progress);
            if(body=="earth") result.night=pack->image("earth-night",region,4096,progress);
            // Earth's geometry/DEM belongs to its quadtree. Regional colour
            // must not wait for an unrelated 385² DEM download or duplicate it.
            if(body!="earth")result.height=pack->contains(body+"-height")?pack->image(body+"-height",region,1024,progress):Image(std::vector<float>{0,0,0,1},1,1);
            // The Earth's roughness from its colour (water: bluer than red; smooth, for the sun's glint and the waves), at half the
            // image's resolution (a quarter of the memory).
            const auto roughness=[](const Image& image){const auto& rgba=image.data();const unsigned width=std::max(1u,image.width()/2),height=std::max(1u,image.height()/2);
                std::vector<unsigned char> rough(static_cast<std::size_t>(width)*height*4,255);
                for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const size_t at=4*(static_cast<size_t>(std::min(2*y,image.height()-1))*image.width()+std::min(2*x,image.width()-1)),to=4*(static_cast<size_t>(y)*width+x);
                    const float water=std::clamp((rgba[at+2]-rgba[at]*1.15f)/12.f,0.f,1.f);rough[to]=rough[to+1]=rough[to+2]=static_cast<unsigned char>(220.f-water*170.f);}
                return Image(std::move(rough),width,height);};
            if(result.day) {
                auto& image=*result.day;auto& rgba=image.data();
                const auto width=image.width(),height=image.height();
                if(body=="earth")result.roughness=roughness(image);
                for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
                    const size_t at=4*(static_cast<size_t>(y)*width+x);const unsigned edge=std::min({x,y,width-1-x,height-1-y});
                    rgba[at+3]=static_cast<unsigned char>(rgba[at+3]*std::min(1.f,edge/(width*.025f)));
                }
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
    const bool localEarth=images.body=="earth"&&region.east-region.west<=2.;
    if(localEarth){
        const double lat=albert::parametricLatitude((region.north+region.south)*pi/360.),lon=(region.west+region.east+360)*pi/360.;
        images.geometryOrigin={-std::cos(lon)*std::cos(lat),std::sin(lat),std::sin(lon)*std::cos(lat)};
    }
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
            if(localEarth){ // Never round an Earth-radius position before subtracting its regional origin.
                const double lat=albert::parametricLatitude((region.north-double(y)*(region.north-region.south)/segments)*pi/180.);
                const double lon=(region.west+180.+double(x)*(region.east-region.west)/segments)*pi/180.;
                const double scale=1.+double(meters+images.lift)/6378137.;
                position->setXYZ(i,float(-std::cos(lon)*std::cos(lat)*scale-images.geometryOrigin[0]),
                    float(std::sin(lat)*scale-images.geometryOrigin[1]),float(std::sin(lon)*std::cos(lat)*scale-images.geometryOrigin[2]));
            }else position->setXYZ(i,point.x,point.y,point.z);
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
    if(localEarth){std::vector<float> residual;const auto* global=geometry->getAttribute<float>("uv2");
        for(unsigned y=0;y<=segments;++y)for(unsigned x=0;x<=segments;++x){const auto i=y*(segments+1)+x;
            const double u=(region.west+180.+double(x)*(region.east-region.west)/segments)/360.,v=(region.north+90.-double(y)*(region.north-region.south)/segments)/180.;
            residual.insert(residual.end(),{float(u-global->getX(i)),float(v-global->getY(i))});}
        geometry->setAttribute("geoResidual",FloatBufferAttribute::create(std::move(residual),2));}
    images.geometry=std::move(geometry);
}
