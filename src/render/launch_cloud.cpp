#include "launch_cloud.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
LaunchCloud::LaunchCloud(Vector3 centre,Vector3 trench):centre_(centre){
    auto m=ShaderMaterial::create();m->name="albert:launch-cloud";m->transparent=true;m->depthTest=true;m->depthWrite=false;m->side=Side::Double;
    m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;
    m->uniforms={{"bank",Uniform(Vector4())},{"extent",Uniform(Vector3(1,1,1))},{"source",Uniform(Vector3())},{"samples",Uniform(20.f)}};
    mesh_=Mesh::create(SphereGeometry::create(1,32,20),m);mesh_->name="launch cloud";mesh_->visible=false;
    mesh_->rotation.z=std::atan2(-trench.x,trench.y);
}
void LaunchCloud::quality(int value){mesh_->material()->as<ShaderMaterial>()->uniforms.at("samples").setValue(value<=0?12.f:value==1?20.f:32.f);}
void LaunchCloud::reset(){age_=energy_=water_=idle_=glow_=0;drift_.set(0,0,0);mesh_->visible=false;}
void LaunchCloud::update(float heat,float water,float seconds,const Vector3& wind,bool enabled){
    if(!std::isfinite(seconds)||seconds<0)return;
    heat=enabled?std::clamp(heat,0.f,1.f):0;water=std::clamp(water/41.f,0.f,1.f);
    if(energy_==0&&heat==0){mesh_->visible=false;return;}
    if(heat>0){energy_=std::min(24.f,energy_+heat*seconds);water_=std::min(24.f,water_+heat*water*seconds);idle_=0;}else idle_+=seconds;
    const float previous=age_;age_+=seconds;glow_=heat+(glow_-heat)*std::exp(-seconds/1.2f);
    const float strength=1-std::exp(-energy_/2.f),spread=1-std::exp(-age_/12.f);
    // Momentum keeps spreading the bank after the engines leave; the radius saturates, never allocates more cells.
    const float radius=22+360*strength*spread,height=12+165*strength*(1-std::exp(-age_/8.f));
    Vector3 air=wind;if(!std::isfinite(air.x)||!std::isfinite(air.y))air.set(2,-.8f,0);air.z=0;
    drift_.addScaledVector(air,seconds+.82f*18*(std::exp(-age_/18.f)-std::exp(-previous/18.f)));
    const float density=strength*std::exp(-idle_/95.f)*std::exp(-std::max(0.f,idle_-150.f)/55.f);
    if(idle_>360){reset();return;}
    mesh_->scale.set(radius*1.08f,radius*1.28f,height);
    mesh_->position.copy(centre_).add(drift_);mesh_->position.z+=height*.45f;
    auto& u=mesh_->material()->as<ShaderMaterial>()->uniforms;
    const auto inverse=mesh_->quaternion.clone().invert();
    u.at("bank").setValue(Vector4(age_,density,water_/std::max(.001f,energy_),glow_));u.at("extent").setValue(mesh_->scale);
    u.at("source").setValue(Vector3(centre_).sub(mesh_->position).applyQuaternion(inverse));
    mesh_->visible=enabled&&density>0;
}
}
