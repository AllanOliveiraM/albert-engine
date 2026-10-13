#include "layers.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace albert::core {
Vector earthSurfacePoint(Geographic p){
    if(!std::isfinite(p.longitude)||!std::isfinite(p.latitude)||std::abs(p.longitude)>180||std::abs(p.latitude)>90)throw std::invalid_argument("Invalid geographic coordinate");
    constexpr double radians=std::numbers::pi/180.,e2=6.6943799901413165e-3;const double latitude=p.latitude*radians,longitude=p.longitude*radians,s=std::sin(latitude),c=std::cos(latitude),n=earthRadiusMeters/std::sqrt(1-e2*s*s);
    return {n*c*std::cos(longitude),n*c*std::sin(longitude),n*(1-e2)*s};
}
Layer prepareLayer(Layer layer,std::span<const Vector> positions,std::span<const Geographic> geographic){
    auto data=std::make_shared<LayerData>();
    if(layer.frame==LayerFrame::EarthSurface){
        if(!positions.empty()||geographic.empty()||geographic.size()>Scene::maxLayerInputPoints||(!layer.anchor.empty()&&layer.anchor!="earth"))throw std::invalid_argument("Earth surface layer requires geographic points only");layer.anchor="earth";
        for(const auto& p:geographic)earthSurfacePoint(p);
        data->positions.push_back(earthSurfacePoint(geographic.front()));
        for(std::size_t i=1;i<geographic.size();++i){const auto a=geographic[i-1],b=geographic[i];const double delta=std::remainder(b.longitude-a.longitude,360.);
            const unsigned segments=layer.kind==LayerKind::Polyline?std::max(1u,static_cast<unsigned>(std::ceil(std::max(std::abs(delta),std::abs(b.latitude-a.latitude))/.05))):1;
            if(data->positions.size()+segments>Scene::maxLayerPoints)throw std::length_error("Densified surface layer exceeds point limit");
            for(unsigned j=1;j<=segments;++j){const double t=double(j)/segments;const double longitude=std::remainder(a.longitude+delta*t,360.);data->positions.push_back(earthSurfacePoint({longitude,std::lerp(a.latitude,b.latitude,t)}));}
        }
    }else{
        if(!geographic.empty()||positions.empty()||positions.size()>Scene::maxLayerInputPoints)throw std::invalid_argument("Spatial layer requires Cartesian points only");
        for(const auto& p:positions){if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||std::hypot(p.x,p.y,p.z)>1e15)throw std::invalid_argument("Invalid layer position");}
        data->positions.assign(positions.begin(),positions.end());
    }
    layer.data=std::move(data);return layer;
}
}
