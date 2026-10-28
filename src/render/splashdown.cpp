#include "splashdown.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
Splashdown::Splashdown(float length,float radius):length_(length),radius_(radius){
    root_=Mesh::create(BufferGeometry::create());root_->name="sea response";root_->frustumCulled=false;
    auto material=[](const char* name){auto m=ShaderMaterial::create();m->name=name;m->transparent=true;m->depthWrite=false;m->side=Side::Double;
        m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;return m;};
    auto m=material("albert:splashdown");m->uniforms={{"bank",Uniform(Vector4())},{"extent",Uniform(Vector3(1,1,1))},{"source",Uniform(Vector3())},{"samples",Uniform(20.f)},{"burst",Uniform(Vector4())}};
    volume_=Mesh::create(SphereGeometry::create(1,32,20),m);volume_->name="sea steam and spray";volume_->renderOrder=9;root_->add(volume_);
    m=material("albert:sea-foam");m->uniforms={{"state",Uniform(Vector4())},{"area",Uniform(Vector4())}};
    foam_=Mesh::create(PlaneGeometry::create(2,2,96,96),m);foam_->name="splash foam and ripples";foam_->renderOrder=7;root_->add(foam_);reset();
}
void Splashdown::quality(int value){
    volume_->material()->as<ShaderMaterial>()->uniforms.at("samples").setValue(value<=0?12.f:value==1?20.f:32.f);
}
void Splashdown::reset(){energy_=age_=idle_=glow_=previousLow_=impact_=foamAge_=0;burstAge_=1000;touched_=tipped_=timed_=false;source_.set(0,0,0);impactPosition_.set(0,0,0);drift_.set(0,0,0);root_->visible=false;}
void Splashdown::update(Vector3 centre,Vector3 nose,float thrust,bool contact,double utc,const Vector3& wind,bool enabled){
    if(!std::isfinite(utc))return;
    if(timed_&&utc<utc_)reset();
    const float dt=timed_?static_cast<float>(utc-utc_):0;utc_=utc;timed_=true;
    nose.normalize();const float vertical=std::abs(nose.z),crossRadius=radius_*std::sqrt(std::max(0.f,1-nose.z*nose.z));
    const Vector3 bell=Vector3(centre).addScaledVector(nose,-length_*.5f);
    const float heat=enabled&&bell.z>-.5f&&nose.z>.2f?std::sqrt(std::clamp(thrust,0.f,1.f))*std::clamp(1-bell.z/140.f,0.f,1.f):0;
    float wet=0;for(int i=0;i<16;++i){const float z=centre.z+(float(i)/15-.5f)*length_*nose.z;wet+=std::clamp(.5f-z/std::max(.3f,2*crossRadius),0.f,1.f)/16;}
    const float low=centre.z-length_*.5f*vertical-crossRadius;
    const bool touching=low<0||(contact&&low<radius_*.5f);
    const float speed=dt>0?std::clamp((previousLow_-low)/dt,0.f,30.f):0;previousLow_=low;
    // Each entry and broadside immersion fires once. A paused contact cannot keep emitting spray.
    if(dt>0&&enabled&&touching&&!touched_){touched_=true;impact_=.75f+speed*.07f;burstAge_=0;foamAge_=0;impactPosition_.copy(bell);impactPosition_.z=0;if(energy_==0)source_.copy(impactPosition_);}
    if(dt>0&&enabled&&touched_&&!tipped_&&vertical<.65f&&wet>.18f){tipped_=true;impact_=1.8f;burstAge_=0;foamAge_=0;impactPosition_.copy(centre);impactPosition_.z=0;if(energy_==0)source_.copy(impactPosition_);}
    if(heat>0&&dt>0){Vector3 footprint=Vector3(bell).addScaledVector(nose,-std::max(0.f,bell.z)/std::max(.2f,nose.z));footprint.z=0;
        source_.lerp(footprint,energy_==0?1.f:1-std::exp(-dt/3.f));
        energy_=std::min(16.f,energy_+heat*dt);idle_=0;}else idle_+=dt;
    age_+=dt;burstAge_+=dt;foamAge_+=dt;glow_=heat+(glow_-heat)*std::exp(-dt/.35f);
    if(!std::isfinite(wind.x)||!std::isfinite(wind.y))return;
    drift_.addScaledVector(wind,dt*.28f);drift_.z=0;
    const float end=std::clamp((idle_-35)/20.f,0.f,1.f);
    const float strength=(1-std::exp(-energy_/1.8f))*std::exp(-idle_/24.f)*(1-end*end*(3-2*end));
    const float splash=impact_*std::exp(-burstAge_/3.8f);
    const float radius=12+55*(1-std::exp(-age_/9.f))+26*impact_*(1-std::exp(-burstAge_/3.f));
    const float height=8+24*strength+24*splash;
    volume_->position.copy(source_).add(drift_);volume_->position.z=height*.45f;volume_->scale.set(radius,radius,height);
    auto& u=volume_->material()->as<ShaderMaterial>()->uniforms;u.at("bank").setValue(Vector4(age_,strength,1.f,glow_));u.at("extent").setValue(volume_->scale);
    u.at("source").setValue(Vector3(source_).sub(volume_->position));u.at("burst").setValue(Vector4(burstAge_,splash,impactPosition_.x-volume_->position.x,impactPosition_.y-volume_->position.y));
    volume_->visible=strength>.005f||splash>.005f;
    const float foamStrength=std::max(strength*.65f,(touched_?1.f:0.f)*std::exp(-foamAge_/95.f));
    const float area=std::min(230.f,30+age_*1.3f+impact_*20);
    foam_->position.copy(source_).addScaledVector(drift_,.45f);foam_->position.z=.12f;foam_->scale.set(area,area,1);
    auto& f=foam_->material()->as<ShaderMaterial>()->uniforms;f.at("state").setValue(Vector4(age_,foamStrength,foamAge_,impact_));
    f.at("area").setValue(Vector4(area,area,radius_,length_));foam_->visible=foamStrength>.008f;
    root_->visible=enabled&&(volume_->visible||foam_->visible);
}
}
