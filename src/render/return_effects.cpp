#include "return_effects.hpp"
#include "core/vents.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
ReturnEffects::ReturnEffects(const core::VehicleLook& look,Object3D& parent):radius_(static_cast<float>(look.diameter/2)),length_(static_cast<float>(look.length)){
    shoulder_=length_*.5f;
    for(const auto& s:look.sections)if(s.radiusTo>=radius_*.98f){ // The first full-diameter shoulder; a short crown section is the hot ring.
        shoulder_-=static_cast<float>(s.radiusFrom<radius_*.98f||s.to<length_*.2f?s.to:s.from);break;}
    for(const auto& p:look.parts)if(p.name=="vent"){
        if(ring_.z==0)ring_.set(length_*.5f-float(p.hinge.x),float(p.span),0,0);
        ring_.z+=1;}
    auto geometry=SphereGeometry::create(1,32,20);const auto vents=core::stackVents(look);
    for(unsigned i=0;i<shells_.size();++i){auto m=ShaderMaterial::create();m->name="albert:return-gas";m->transparent=true;m->depthWrite=false;m->side=Side::Double;
        m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;
        m->uniforms={{"kind",Uniform(float(i))},{"extent",Uniform(Vector3(1,1,1))},{"state",Uniform(Vector4())},{"body",Uniform(Vector4(radius_,length_,shoulder_,0.f))},{"offset",Uniform(Vector3())},
            {"wind",Uniform(Vector3(0,1,0))},{"source",Uniform(Vector3())},{"seconds",Uniform(0.f)},{"samples",Uniform(6.f)},{"ring",Uniform(ring_)},
            {"tip0",Uniform(Vector4())},{"tip1",Uniform(Vector4())},{"tip2",Uniform(Vector4())},{"tip3",Uniform(Vector4())},{"tip4",Uniform(Vector4())},{"tip5",Uniform(Vector4())}};
        for(unsigned k=0;k<4;++k){Vector4 outlet(0,0,0,0);
            if(k<vents.size()){const auto& v=vents[k];outlet.set(radius_*float(std::sin(v.angle)),float(v.height),radius_*float(std::cos(v.angle)),float(v.gain));}
            m->uniforms.emplace("vent"+std::to_string(k),Uniform(outlet));}
        unsigned outlet=0;for(const auto& p:look.parts)if(p.name=="vent"&&outlet<20){
            m->uniforms.emplace("outlet"+std::to_string(outlet++),Uniform(Vector4(float(-p.hinge.y),length_*.5f-float(p.hinge.x),float(p.hinge.z),float(p.span))));}
        for(;outlet<20;++outlet)m->uniforms.emplace("outlet"+std::to_string(outlet),Uniform(Vector4()));
        auto mesh=Mesh::create(geometry,m);mesh->name=i==0?"rarefied exhaust":i==1?"return ignition and collar":i==2?"transonic condensation":"hot-staging vent jets";
        mesh->visible=false;mesh->frustumCulled=false;mesh->renderOrder=9+i;parent.add(mesh);shells_[i]=mesh;}
}
void ReturnEffects::quality(int value){quality_=std::clamp(value,0,2);for(auto& p:shells_)p->material()->as<ShaderMaterial>()->uniforms.at("samples").setValue(value>=2?12.f:value>0?6.f:2.f);}
void ReturnEffects::hotstage(float flow,float gap,double utc){
    auto& p=*shells_[3];const float feed=std::min(1.f,flow/3.f)*std::clamp(1-gap/24.f,0.f,1.f);
    p.visible=ring_.z>0&&feed>.001f;if(!p.visible)return;
    const float reach=radius_*(2.5f+2.f*feed);p.position.set(0,ring_.x,0);p.scale.set(reach,reach*.65f,reach);
    auto& u=p.material()->as<ShaderMaterial>()->uniforms;u.at("extent").setValue(p.scale);u.at("offset").setValue(p.position);
    u.at("state").setValue(Vector4(feed,std::clamp(pressure_/101325.f,0.f,1.f),0.f,0.f));u.at("seconds").setValue(float(std::fmod(utc,4096.)));
}
void ReturnEffects::update(double utc,float pressure,float mach,float fill,const Vector3& upwind,float flow,float flash,const Vector3& source,float humidity,const std::array<Vector3,6>& tips,unsigned tipCount,float clearance){
    if(!std::isfinite(utc))return;
    pressure_=pressure;const bool reset=timed_&&utc<utc_;const float dt=timed_&&!reset?static_cast<float>(utc-utc_):0;
    if(reset){demand_=0;burned_=false;}
    if(flow>.001f)burned_=true;
    // No valve telemetry reaches the scene. Estimate pressure management from changing tank/ambient state and cold inventory.
    const float cold=std::clamp(fill*8.f,0.f,1.f),idle=1-std::clamp(flow*4.f,0.f,1.f);
    const float change=dt>0?std::clamp((std::abs(fill-previousFill_)*18.f+std::abs(pressure-previousPressure_)/101325.f*4.f)/dt,0.f,1.f):0;
    const float target=cold*idle*(.16f+.84f*change);
    demand_+= (target-demand_)*(1-std::exp(-std::min(dt,2.f)/.6f));
    previousFill_=fill;previousPressure_=pressure;utc_=utc;timed_=true;
    const float rare=std::clamp((4000.f-pressure)/3500.f,0.f,1.f),thrust=std::sqrt(std::clamp(flow,0.f,33.f));
    // An underexpanded jet's area follows pressure ratio; cap the visible envelope rather than allocating more geometry.
    const float expansion=std::clamp(std::sqrt(101325.f/std::max(pressure,1.f)),1.f,240.f);
    const float reach=std::min(6000.f,(30.f+20.f*thrust)*expansion),width=std::min(2400.f,reach*(.12f+.0015f*expansion));
    // A large cluster merges in dense air as well; its rarefied continuation is the same pressure-ratio envelope.
    const float merged=upwind.y>0?std::clamp((flow-6.f)/12.f,0.f,1.f)*(1-rare):0.f;
    auto& bloom=*shells_[0];bloom.visible=flow>.001f&&(rare+merged)*clearance>.001f;
    bloom.position.copy(source).add(Vector3(0,-reach*.5f,0));bloom.scale.set(width,reach*.54f,width);
    // Incoming flow is opposite the exhaust only when travelling engines first. Ve is Raptor's effective atmospheric exhaust speed.
    const float ratio=mach*340.f/3300.f*std::max(0.f,-upwind.y);
    const float collar=std::clamp((ratio-.008f)/.075f,0.f,1.f)*std::clamp(pressure/30000.f,0.f,1.f)*std::min(1.f,flow);
    auto& burn=*shells_[1];burn.visible=quality_>0&&(flash>.001f||collar>.001f)&&pressure>1000;
    const float span=radius_*(2.2f+3.f*collar+1.2f*flash);burn.position.copy(source).add(Vector3(0,span*.25f,0));burn.scale.set(span,span*1.5f,span);
    const float rh=std::isfinite(humidity)&&humidity>=0?std::clamp(humidity,0.f,1.f):.78f;
    const float wet=std::clamp((rh-.35f)/.55f,0.f,1.f),air=std::clamp(pressure/25000.f,0.f,1.f);
    // Forward expansion shocks persist into low supersonic flow; the returning tail's collar is a narrower crossing.
    const float end=upwind.y>0?1.5f:1.08f;
    const float transonic=std::clamp((mach-.82f)/.07f,0.f,1.f)*std::clamp((end-mach)/(upwind.y>0?.3f:.06f),0.f,1.f)*wet*air;
    const float trail=wet*air*std::clamp((mach-.25f)/.5f,0.f,1.f)*std::clamp((2.5f-mach),0.f,1.f);
    const float coast=(burned_||mach>.03f||pressure<40000)&&idle>.5f?demand_:0,rest=burned_&&mach<.03f?coast:0;
    auto& mist=*shells_[2];mist.visible=quality_>0&&(transonic>.001f||trail>.001f||coast>.001f);
    mist.position.set(0,0,0);mist.scale.set(radius_*4+12,length_*.5f+36,radius_*4+12);
    const float seconds=static_cast<float>(std::fmod(utc,4096.));
    for(unsigned i=0;i<3;++i){auto& p=*shells_[i];auto& u=p.material()->as<ShaderMaterial>()->uniforms;
        u.at("extent").setValue(p.scale);u.at("source").setValue(Vector3(source).sub(p.position));u.at("wind").setValue(upwind);u.at("seconds").setValue(seconds);
        u.at("body").setValue(Vector4(radius_,length_,shoulder_,rest));u.at("offset").setValue(p.position);
        u.at("state").setValue(i==0?Vector4((rare+merged)*clearance,std::min(1.f,.18f*thrust),merged,0.f):i==1?Vector4(flash,collar,0.f,0.f):Vector4(transonic,trail,coast,air));
        if(i==2)for(unsigned k=0;k<6;++k){const auto& t=tips[k];u.at("tip"+std::to_string(k)).setValue(Vector4(t.x,t.y,t.z,k<tipCount?1.f:0.f));}}
}
}
