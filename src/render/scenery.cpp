#include "scenery.hpp"
#include "lighting.hpp"
#include "core/vents.hpp"
#include "threepp/utils/BufferGeometryUtils.hpp"
#include <algorithm>
#include <cmath>
#include <map>
using namespace threepp;
namespace albert {
namespace {
constexpr float pi=3.14159265358979f;
using Pieces=std::vector<std::shared_ptr<BufferGeometry>>;
Vector3 vec(core::Vector v){return {static_cast<float>(v.x),static_cast<float>(v.y),static_cast<float>(v.z)};}
void bar(Pieces& out,const Vector3& a,const Vector3& b,float t){ // A square bar t thick from a to b.
    Vector3 d=Vector3(b).sub(a);const float l=d.length();if(l<1e-4f)return;
    auto g=BoxGeometry::create(l,t,t);Quaternion q;q.setFromUnitVectors(Vector3(1,0,0),d.normalize());
    Matrix4 m;m.compose(Vector3(a).add(b).multiplyScalar(.5f),q,Vector3(1,1,1));g->applyMatrix4(m);out.push_back(g);
}
std::shared_ptr<BufferGeometry> inside(float r,float h){ // A tube's inner wall: an open cylinder facing its axis.
    auto g=CylinderGeometry::create(r,r,h,48,1,true);for(auto& n:g->getAttribute<float>("normal")->array())n=-n;
    auto& i=g->getIndex()->array();for(std::size_t k=0;k+2<i.size();k+=3)std::swap(i[k+1],i[k+2]);return g;
}
void truss(Pieces& out,float w,float d,float l,float t){ // Four chords along z, and at each bay struts and a zigzag of braces on every face.
    const float x=(w-t)/2,y=(d-t)/2;const Vector3 c[4]{{-x,-y,0},{x,-y,0},{x,y,0},{-x,y,0}};
    const int n=std::max(1,static_cast<int>(std::lround(l/std::max(w,d))));
    auto at=[&](int i,int k){return Vector3(c[i%4]).setZ(-l/2+l*static_cast<float>(k)/static_cast<float>(n));};
    for(int i=0;i<4;++i){bar(out,at(i,0),at(i,n),t);
        for(int k=0;k<=n;++k)bar(out,at(i,k),at(i+1,k),.8f*t);
        for(int k=0;k<n;++k)(k+i)%2?bar(out,at(i,k),at(i+1,k+1),.6f*t):bar(out,at(i+1,k),at(i,k+1),.6f*t);}
}
}
Site::Site(const core::Scenery& s){
    root_=Mesh::create(BufferGeometry::create());root_->frustumCulled=false;
    const std::size_t n=s.bars.size();hinges_.resize(n);
    for(std::size_t k=0;k<n;++k)if(s.bars[k].hinged){auto& h=hinges_[k];h.pivot=Object3D::create();h.pivot->position.copy(vec(s.bars[k].pivot));h.axis=vec(s.bars[k].axis).normalize();root_->add(h.pivot);}
    // By what carries them (0: the ground, k: the k-th structure's pivot) and colour, about that pivot.
    std::map<std::pair<std::size_t,std::uint32_t>,Pieces> by;constexpr std::uint32_t steel=0x1000000; // Past any colour: the bars' own material.
    auto carrier=[&](unsigned structure){return structure>0&&structure<=n&&hinges_[structure-1].pivot?structure:0u;};
    auto relative=[&](std::size_t k,std::shared_ptr<BufferGeometry> g){if(k)g->translate(-hinges_[k-1].pivot->position.x,-hinges_[k-1].pivot->position.y,-hinges_[k-1].pivot->position.z);return g;};
    for(std::size_t i=0;i<s.parts.size();++i){const auto& p=s.parts[i];
        // Its axes: y toward the heading (clockwise from north), z tilted from up toward y.
        const float h=static_cast<float>(p.heading),t=static_cast<float>(p.tilt);
        const Vector3 ahead(std::sin(h),std::cos(h),0),across(std::cos(h),-std::sin(h),0),up(0,0,1);
        const Vector3 z=Vector3(up).multiplyScalar(std::cos(t)).add(Vector3(ahead).multiplyScalar(std::sin(t))),y=Vector3(ahead).multiplyScalar(std::cos(t)).sub(Vector3(up).multiplyScalar(std::sin(t)));
        if(p.name=="steam vent"){vents_.push_back({vec(p.centre),y,across,z,std::max(1.f,static_cast<float>(p.size.x)),std::max(1.f,static_cast<float>(p.size.z)),carrier(p.structure)});continue;} // Openings only seed steam; never fill them with scenery.
        if(p.name=="tank vent"){tankVents_.push_back({vec(p.centre),y,across,z,std::max(.2f,static_cast<float>(p.size.x)),std::max(.2f,static_cast<float>(p.size.z)),carrier(p.structure),0,core::tankVentId(i)});continue;} // Markers emit vapour; a box would cap the outlet.
        if(p.name=="crest spray pipe")sprays_.push_back({vec(p.centre).add(Vector3(z).multiplyScalar(static_cast<float>(p.size.z)*.5f+.05f)),y,across,z,.8f,0,carrier(p.structure)});
        if(p.name=="mount opening rim")for(int i=0;i<8;++i){const float a=2*pi*static_cast<float>(i)/8;const Vector3 out=Vector3(across).multiplyScalar(std::cos(a)).add(Vector3(y).multiplyScalar(std::sin(a)));
            sprays_.push_back({vec(p.centre).add(Vector3(out).multiplyScalar(static_cast<float>(p.inner)*.5f)).add(Vector3(z).multiplyScalar(static_cast<float>(p.size.z)*.5f+.05f)),out,Vector3(z).cross(out),z,.25f,1,carrier(p.structure)});} // Start above the lip so the deck cannot bury the spray.
        if(p.name=="trench floor")pools_.push_back({vec(p.centre).add(Vector3(z).multiplyScalar(static_cast<float>(p.size.z)*.5f+.5f)),y,across,z,static_cast<float>(p.size.x),static_cast<float>(p.size.y),carrier(p.structure)});
        Pieces local;const float w=static_cast<float>(p.size.x),d=static_cast<float>(p.size.y),l=static_cast<float>(p.size.z);
        switch(p.shape){
        case core::SceneryPart::Shape::Box:local.push_back(BoxGeometry::create(w,d,l));break;
        case core::SceneryPart::Shape::Cylinder:{ // Built round (radius 1) along y, then laid along z and stretched to its diameters.
            const float k=static_cast<float>(p.inner)/w;
            if(k<=0)local.push_back(CylinderGeometry::create(1,1,l,48));
            else{local.push_back(CylinderGeometry::create(1,1,l,48,1,true));local.push_back(inside(k,l));
                for(const float side:{1.f,-1.f}){auto r=RingGeometry::create(k,1,48);r->rotateX(-side*pi/2);r->translate(0,side*l/2,0);local.push_back(r);}}
            for(auto& g:local){g->rotateX(pi/2);g->scale(w/2,d/2,1);}break;}
        case core::SceneryPart::Shape::Truss:truss(local,w,d,l,p.bar>0?static_cast<float>(p.bar):.08f*std::min(w,d));break;
        }
        Matrix4 m;m.makeBasis(across,y,z);m.setPosition(vec(p.centre));const std::size_t k=carrier(p.structure);
        for(auto& g:local){g->applyMatrix4(m);by[{k,p.color}].push_back(relative(k,std::move(g)));}
    }
    for(std::size_t i=0;i<n;++i){const auto& b=s.bars[i];const Vector3 a=vec(b.from),e=vec(b.to);Vector3 dir=Vector3(e).sub(a);const float l=dir.length();if(l<1e-4f||!(b.radius>0))continue; // Round steel bars (radius 0: not drawn).
        auto g=CylinderGeometry::create(static_cast<float>(b.radius),static_cast<float>(b.radius),l,24);Quaternion q;q.setFromUnitVectors(Vector3(0,1,0),dir.normalize());
        Matrix4 m;m.compose(Vector3(a).add(e).multiplyScalar(.5f),q,Vector3(1,1,1));g->applyMatrix4(m);const std::size_t k=carrier(static_cast<unsigned>(i+1));by[{k,steel}].push_back(relative(k,std::move(g)));}
    for(const auto& [key,pieces]:by){
        auto x=Mesh::create(mergeBufferGeometries(pieces),key.second==steel?assistedMaterial(Color(0x9c9c9a),.8f,.35f):assistedMaterial(Color(key.second),.25f,.7f));
        x->castShadow=x->receiveShadow=true;(key.first?hinges_[key.first-1].pivot:root_)->add(x);}
    auto active=[&](const Vent& s){return std::none_of(vents_.begin(),vents_.end(),[&](const Vent& v){return Vector3(s.centre).sub(v.centre).lengthSq()<120*120;});};
    std::erase_if(sprays_,active);std::erase_if(pools_,active); // Only water the mount served by the exhaust markers.
    if(!vents_.empty()||!tankVents_.empty()){auto g=SphereGeometry::create(1,20,12);auto pool=[&](auto& puffs){for(auto& p:puffs){
        auto m=ShaderMaterial::create();m->name="albert:steam";m->transparent=true;m->depthTest=true;m->depthWrite=false;m->side=Side::Double;m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;
        m->uniforms={{"density",Uniform(0.f)},{"seed",Uniform(0.f)},{"time",Uniform(0.f)},{"up",Uniform(Vector3(0,0,1))},{"kind",Uniform(0.f)},{"size",Uniform(Vector3(1,1,1))},{"cloud",Uniform(Vector4(0,0,0,0))}};p.mesh=Mesh::create(g,m);p.mesh->visible=false;root_->add(p.mesh);}};
        if(!vents_.empty()){pool(steam_);pool(spray_);}if(!tankVents_.empty())pool(boiloff_);} // Reserve water slots so the launch cannot starve the deluge; 256 shared-geometry shells total.
}
void Site::animate(const std::vector<float>* angles){
    for(std::size_t k=0;k<hinges_.size();++k)if(auto& h=hinges_[k];h.pivot)h.pivot->quaternion.setFromAxisAngle(h.axis,angles&&k<angles->size()?(*angles)[k]:0.f);
}
void Site::venting(double utc){
    if(!std::isfinite(utc))return;const bool reset=ventTimed_&&utc<ventUtc_;
    const float seconds=ventTimed_&&!reset?static_cast<float>(utc-ventUtc_):0.f;ventUtc_=utc;ventTimed_=true;
    if(reset){for(auto& p:boiloff_){p.life=0;p.mesh->visible=false;}for(auto& v:tankVents_)v.pending=0;}
    if(tankVents_.empty())return;auto random=[&]{tankSeed_^=tankSeed_<<13;tankSeed_^=tankSeed_>>17;tankSeed_^=tankSeed_<<5;return static_cast<float>(tankSeed_>>8)/16777216.f;};
    for(auto& p:boiloff_)if(p.life>0){p.age+=seconds;if(p.age>=p.life){p.life=0;p.mesh->visible=false;}}
    for(auto& v:tankVents_)v.pending=std::min(1.f,v.pending+std::min(seconds,.25f)*static_cast<float>(core::ventFlow(v.id,utc)));
    const std::size_t first=tankCursor_;for(std::size_t i=0;i<tankVents_.size();++i){const std::size_t k=(first+i)%tankVents_.size();auto& v=tankVents_[k];if(v.pending<1)continue;
        Puff* slot=nullptr;for(auto& p:boiloff_)if(p.life<=0){slot=&p;break;}if(!slot)break;auto& p=*slot;Vector3 up=v.up;p.origin=v.centre;
        if(v.structure){const auto& pivot=*hinges_[v.structure-1].pivot;p.origin.sub(pivot.position).applyQuaternion(pivot.quaternion).add(pivot.position);up.applyQuaternion(pivot.quaternion);} // Hinges carry the source, never a released cloud.
        p.age=0;p.life=8+3*random();p.seed=random();p.strength=static_cast<float>(core::ventFlow(v.id,utc));p.width=std::clamp(.5f*v.width,.18f,.6f);p.height=std::clamp(.5f*v.height,.25f,.8f);
        p.velocity=Vector3(up).multiplyScalar(.9f+.7f*random());p.drift.set(.7f+.8f*random(),-.3f+.5f*random(),0);p.mesh->rotation.set(0,0,2*pi*p.seed);
        v.pending-=1;tankCursor_=(k+1)%tankVents_.size();}
    for(auto& p:boiloff_)if(p.life>0){const float a=p.age,grow=1-std::exp(-a/3.f),tail=std::clamp((p.life-a)/2.f,0.f,1.f);auto& m=*p.mesh;
        m.scale.set(p.width+(2.8f+p.seed)*grow,p.width+(2.4f+p.seed)*grow,p.height+(2.2f+p.seed)*grow);m.position.copy(p.origin).add(Vector3(p.drift).multiplyScalar(a)).add(Vector3(p.velocity).multiplyScalar(2.2f*(1-std::exp(-a/2.2f))));m.position.z+=.18f*a;
        m.visible=true;auto& u=m.material()->as<ShaderMaterial>()->uniforms;u.at("density").setValue(p.strength*std::min(1.f,.2f+a/.3f)*std::exp(-a/5.f)*tail*tail);u.at("seed").setValue(p.seed*97.f);u.at("time").setValue(a);}
}
void Site::exhaust(float thrust,float above,float seconds,float waterFlow,const Vector3& wind){
    if(!std::isfinite(seconds)||seconds<=0)return;if(vents_.empty())return;
    const float share=std::isfinite(thrust)?std::clamp(thrust,0.f,1.f):0.f,water=std::isfinite(waterFlow)?(waterFlow<0?(share>0?41.f:0.f):std::clamp(waterFlow,0.f,60.f)):0.f;
    const float height=std::isfinite(above)?std::clamp((above-40.f)/110.f,0.f,1.f):1.f,heat=above>=-5?std::sqrt(share)*(1-height*height*(3-2*height)):0.f,drive=heat*std::sqrt(std::min(1.f,water/41.f));
    Vector3 air=wind;if(!std::isfinite(air.x)||!std::isfinite(air.y)||!std::isfinite(air.z))air.set(2,-.8f,0);
    auto random=[&]{seed_^=seed_<<13;seed_^=seed_>>17;seed_^=seed_<<5;return static_cast<float>(seed_>>8)/16777216.f;};
    auto fade=[](float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);};
    auto age=[&](auto& pool){for(auto& p:pool)if(p.life>0){p.age+=seconds;if(p.age>=p.life){p.life=0;p.mesh->visible=false;}}};age(steam_);age(spray_);
    auto source=[&](const Vent& v,Vector3& origin,Vector3& ahead,Vector3& across,Vector3& up){origin=v.centre;ahead=v.ahead;across=v.across;up=v.up;
        if(v.structure){const auto& pivot=*hinges_[v.structure-1].pivot;origin.sub(pivot.position).applyQuaternion(pivot.quaternion).add(pivot.position);ahead.applyQuaternion(pivot.quaternion);across.applyQuaternion(pivot.quaternion);up.applyQuaternion(pivot.quaternion);}}; // A released puff no longer follows a hinge.
    auto free=[](auto& pool)->Puff*{for(auto& p:pool)if(p.life<=0)return &p;return nullptr;};
    float ground=vents_.front().centre.z-.5f*vents_.front().height;for(const auto& v:vents_)ground=std::min(ground,v.centre.z-.5f*v.height);
    auto hot=[&](const Vent& v,bool deck,float backlog,float rate){auto* slot=free(steam_);if(!slot)return false;auto& p=*slot;Vector3 ahead,across,up;source(v,p.origin,ahead,across,up);
        p.origin.add(Vector3(across).multiplyScalar((random()-.5f)*v.width*.7f));
        const float speed=(80+70*random())*(.65f+.35f*std::sqrt(share));
        p.velocity.copy(ahead).multiplyScalar(deck?8+10*random():speed).add(Vector3(across).multiplyScalar((random()-.5f)*(deck?16.f:32.f))).add(Vector3(up).multiplyScalar(deck?12+12*random():2+5*random()));
        p.drift.copy(air).add(Vector3((random()-.5f)*1.2f,(random()-.5f)*1.2f,0));p.age=std::min(seconds,backlog/rate);p.life=95+30*random();
        p.width=12+8*random();p.height=10+8*random();p.seed=random();p.strength=drive;p.ground=ground;p.kind=3;
        p.mesh->rotation.set(0,0,std::atan2(-ahead.x,ahead.y));p.mesh->visible=true;return true;};
    const float rate=18*drive/static_cast<float>(vents_.size()); // Closely spaced parcels keep both outlets joined to the boiling deck.
    for(auto& v:vents_){
        if(!(rate>0)){v.pending=1;continue;}
        v.pending=std::min(4.f,v.pending+seconds*rate);
        while(v.pending>=1){v.pending-=1;if(!hot(v,false,v.pending,rate)){v.pending=std::fmod(v.pending,1.f);break;}}
    }
    const float deckRate=8*drive;if(!(deckRate>0))deckPending_=1;
    else{deckPending_=std::min(4.f,deckPending_+seconds*deckRate);while(deckPending_>=1){const Vent* rim=nullptr;
        for(std::size_t i=0;i<sprays_.size();++i){const auto& v=sprays_[deckCursor_++%sprays_.size()];if(v.height>0){rim=&v;break;}}
        if(!rim){deckPending_=0;break;}deckPending_-=1;if(!hot(*rim,true,deckPending_,deckRate)){deckPending_=std::fmod(deckPending_,1.f);break;}}} // Recirculation through the opening hides the mount before the vehicle clears it.
    const float wet=std::sqrt(std::min(1.f,water/16.f));
    auto waterBirth=[&](unsigned kind){auto& sources=kind==1?sprays_:kind==2?pools_:vents_;auto& pending=kind==1?sprayPending_:kind==2?mistPending_:runoffPending_;auto& cursor=kind==1?sprayCursor_:kind==2?poolCursor_:runoffCursor_;const float r=(kind==1?18.f:kind==2?2.f:2.8f)*wet;
        if(sources.empty()||!(r>0)){pending=0;return;}pending=std::min(4.f,pending+seconds*r);
        const std::size_t first=kind==1?0:kind==2?48:64,last=kind==1?48:kind==2?64:72;
        while(pending>=1){pending-=1;Puff* slot=nullptr;for(std::size_t i=first;i<last;++i)if(spray_[i].life<=0){slot=&spray_[i];break;}if(!slot){pending=std::fmod(pending,1.f);break;}
            const auto& v=sources[cursor++%sources.size()];auto& p=*slot;Vector3 ahead,across,up;source(v,p.origin,ahead,across,up);
            p.age=std::min(seconds,pending/r);p.seed=random();p.kind=kind;p.ground=ground;p.drift.copy(air);p.strength=wet;p.mesh->visible=true;
            if(kind==2){p.origin.add(Vector3(across).multiplyScalar((random()-.5f)*v.width*.8f)).add(Vector3(ahead).multiplyScalar((random()-.5f)*v.height));
                p.velocity.copy(ahead).multiplyScalar((random()-.5f)*5);p.life=7+2*random();p.width=6+6*random();p.height=1.3f+random();p.mesh->rotation.set(0,0,std::atan2(-ahead.x,ahead.y));}
            else if(kind==4){p.origin.z=ground+.9f;p.origin.add(Vector3(across).multiplyScalar((random()-.5f)*v.width*.6f));
                p.velocity.copy(ahead).multiplyScalar(10+8*random());p.life=2.6f+.6f*random();p.width=4+3*random();p.height=.7f+.6f*random();p.mesh->rotation.set(0,0,std::atan2(-ahead.x,ahead.y));}
            else{const float side=v.height>0?1.f:(random()<.5f?-1.f:1.f);p.origin.add(Vector3(across).multiplyScalar((random()-.5f)*v.width));
                p.velocity.copy(ahead).multiplyScalar(side*(6+8*random())).add(Vector3(across).multiplyScalar((random()-.5f)*7)).add(Vector3(up).multiplyScalar(v.height>0?11+4*random():14+4*random()));
                p.life=(p.velocity.z+std::sqrt(p.velocity.z*p.velocity.z+19.62f*std::max(0.f,p.origin.z-ground)))/9.81f+.2f;p.width=.18f+.18f*random();p.height=1.2f+.9f*random();}
        }};waterBirth(1);waterBirth(2);waterBirth(4); // Separate quotas preserve dozens of fans, a continuous mist bed and both runoff streams.
    auto uniforms=[&](Puff& p,float density){auto& m=*p.mesh;auto& u=m.material()->as<ShaderMaterial>()->uniforms;
        u.at("density").setValue(density);u.at("seed").setValue(p.seed*97.f);u.at("time").setValue(p.age);u.at("kind").setValue(static_cast<float>(p.kind));u.at("size").setValue(m.scale);
        u.at("up").setValue(Vector3(0,0,1).applyQuaternion(m.quaternion.clone().invert()));u.at("cloud").setValue(Vector4(m.position.z-p.ground,150.f,p.kind==3?std::exp(-p.age/45.f):0.f,1.f));};
    for(auto& p:spray_)if(p.life>0){const float a=p.age;auto& m=*p.mesh;float density;
        if(p.kind==1){m.position.copy(p.origin).add(Vector3(p.velocity).multiplyScalar(a)).add(Vector3(p.drift).multiplyScalar(.12f*a*a));m.position.z-=4.905f*a*a;
            Vector3 tangent=p.velocity;tangent.z-=9.81f*a;if(tangent.lengthSq()>1e-5f)m.quaternion.setFromUnitVectors(Vector3(0,0,1),tangent.normalize());
            m.scale.set(p.width+.22f*a,p.width+.38f*a,p.height+.8f*a);density=5*p.strength*fade(a/.08f)*fade((p.life-a)/.45f);}
        else if(p.kind==4){m.position.copy(p.origin).add(Vector3(p.velocity).multiplyScalar(1.2f*(1-std::exp(-a/1.2f)))).add(Vector3(p.drift).multiplyScalar(.15f*a));
            m.scale.set(p.width+2*a,7+4*a,p.height+.2f*a);density=3.5f*p.strength*fade(a/.15f)*fade((p.life-a)/.8f);}
        else{m.position.copy(p.origin).add(Vector3(p.velocity).multiplyScalar(a)).add(Vector3(p.drift).multiplyScalar(.2f*a));m.position.z+=.07f*a;
            m.scale.set(p.width+1.8f*a,1.6f*p.width+2.2f*a,p.height+.35f*a);density=1.8f*p.strength*fade(a/.35f)*fade((p.life-a)/2.f);}
        uniforms(p,density);}
    for(auto& p:steam_)if(p.life>0){
        const float a=p.age,grow=(.65f+.35f*p.strength)*(1-std::exp(-a/3.8f)),jet=1.65f*(1-std::exp(-a/1.65f)),rise=(10+12*p.seed)*(1-std::exp(-a/5.f));
        auto& m=*p.mesh;m.scale.set(p.width+(60+30*p.seed)*grow,p.width+(75+25*p.seed)*grow,p.height+(56+22*p.seed)*grow);
        m.position.copy(p.origin).add(Vector3(p.velocity).multiplyScalar(jet)).add(Vector3(p.drift).multiplyScalar(a-jet));m.position.z+=rise;
        m.position.z=std::max(m.position.z,p.ground+.5f*m.scale.z+.35f*rise); // Keep the dense interior on the apron while the billows rise.
        const float density=8*p.strength*fade(a/.16f)*(1-.35f*fade(a/70.f))*fade((p.life-a)/28.f);
        uniforms(p,density); // Stored heat keeps the cloud dense after the vehicle climbs away.
    }
}
}
