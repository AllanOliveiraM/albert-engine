#include "paths.hpp"
#include <cmath>
#include <numbers>

using namespace threepp;

std::shared_ptr<ShaderMaterial> albert::orbitMaterial(float horizon) {
    auto material=ShaderMaterial::create();material->name="albert:orbit";
    material->uniforms.emplace("referenceVisibility",Uniform(0.f));
    material->uniforms.emplace("strokeWidth",Uniform(4.f));
    material->uniforms.emplace("present", Uniform(0.f));
    material->uniforms.emplace("viewport",Uniform(Vector2(1280,720)));
    material->uniforms.emplace("nearClip",Uniform(.01f));
    material->uniforms.emplace("dashDays",Uniform(.25f));
    material->uniforms.emplace("horizon",Uniform(horizon));
    material->uniforms.emplace("visibility",Uniform(1.f));
    material->transparent=true;
    material->depthTest=true;
    material->depthWrite=false;
    material->toneMapped=false;
    material->side=Side::Double;
    material->forceSinglePass=true;
    return material;
}

std::shared_ptr<ShaderMaterial> albert::borderMaterial() {
    auto material=ShaderMaterial::create();material->name="albert:border";
    material->uniforms.emplace("viewport",Uniform(Vector2(1280,720)));
    material->uniforms.emplace("nearClip",Uniform(.01f));
    material->uniforms.emplace("visibility",Uniform(0.f));
    material->uniforms.emplace("clampSurface",Uniform(1.f));
    material->uniforms.emplace("widthPixels",Uniform(3.f));
    material->uniforms.emplace("lineColor",Uniform(Vector3(.60f,.73f,.82f)));
    material->uniforms.emplace("surfaceOffset",Uniform(.00005f));
    material->uniforms.emplace("heightMap",Uniform(static_cast<Texture*>(nullptr)));
    material->uniforms.emplace("detailHeightMap",Uniform(static_cast<Texture*>(nullptr)));
    material->uniforms.emplace("detailBounds",Uniform(Vector4(0,0,1,1)));
    material->uniforms.emplace("detailEnabled",Uniform(0.f));
    material->transparent=true;
    material->depthTest=true;
    material->depthWrite=false;
    material->toneMapped=false;
    material->side=Side::Double;
    material->forceSinglePass=true;
    return material;
}

double albert::revolutionDays(std::span<const PathPoint> points,const std::array<double,3>& normal){
    const double length=std::hypot(normal[0],normal[1],normal[2]);
    if(points.size()<3||!(length>0))return 0;
    const double n[]={normal[0]/length,normal[1]/length,normal[2]/length};
    // In-plane basis from the first point.
    const auto& first=points.front();const double along=first.x*n[0]+first.y*n[1]+first.z*n[2];
    double u[]={first.x-along*n[0],first.y-along*n[1],first.z-along*n[2]};const double size=std::hypot(u[0],u[1],u[2]);
    if(!(size>0))return 0;
    for(auto& value:u)value/=size;
    const double v[]={n[1]*u[2]-n[2]*u[1],n[2]*u[0]-n[0]*u[2],n[0]*u[1]-n[1]*u[0]};
    auto angle=[&](const PathPoint& p){return std::atan2(p.x*v[0]+p.y*v[1]+p.z*v[2],p.x*u[0]+p.y*u[1]+p.z*u[2]);};
    constexpr double turn=2*std::numbers::pi;double swept=0,previous=angle(first);
    for(size_t i=1;i<points.size();++i){
        const double current=angle(points[i]),step=std::remainder(current-previous,turn);previous=current;
        if(swept+step>=turn&&step>0)return std::lerp(points[i-1].day,points[i].day,(turn-swept)/step)-first.day;
        swept+=step;
    }
    return 0;
}

double albert::pathPresent(double days,double halfWidth,double period){
    if(std::abs(days)<=halfWidth||!(period>0))return days;
    return std::abs(days)<=halfWidth+3*period?std::remainder(days,period):1e7;
}
