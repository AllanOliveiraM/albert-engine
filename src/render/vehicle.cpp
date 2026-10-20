#include "vehicle.hpp"
#include "lighting.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
namespace {
constexpr float pi=3.14159265358979f;
// The flight API's body axes (x forward, y right, z down) as the node's local ones (y along the nose, x along body -y).
Vector3 axisOf(core::Vector v){return {static_cast<float>(-v.y),static_cast<float>(v.x),static_cast<float>(v.z)};}
std::shared_ptr<Mesh> part(std::shared_ptr<BufferGeometry> g,std::shared_ptr<Material> m){auto x=Mesh::create(std::move(g),std::move(m));x->castShadow=x->receiveShadow=true;return x;}
std::shared_ptr<ShaderMaterial> plumeMaterial(){ // webgpu_programs "albert:plume": soft, added to what is behind.
    auto m=ShaderMaterial::create();m->name="albert:plume";m->transparent=true;m->depthWrite=false;m->side=Side::Front;m->blending=Blending::Additive;
    m->uniforms={{"core",Uniform(Vector3(1,.75f,.4f))},{"edge",Uniform(Vector3(1,.35f,.08f))},{"strength",Uniform(0.f)}};return m;
}
std::shared_ptr<MeshBasicMaterial> glow(std::uint32_t colour,float opacity){ // A jet: unlit, added to what is behind.
    auto m=MeshBasicMaterial::create();m->color=Color(colour);m->transparent=true;m->opacity=opacity;m->blending=Blending::Additive;m->depthWrite=false;m->side=Side::Double;return m;
}
}
Vehicle::Vehicle(const core::VehicleLook& look){
    root_=Mesh::create(BufferGeometry::create());root_->frustumCulled=false;
    const double half=look.length/2;
    auto point=[&](core::Vector p){return Vector3(static_cast<float>(-p.y),static_cast<float>(half-p.x),static_cast<float>(p.z));}; // x aft of the nose.
    for(const auto& s:look.sections){ // A frustum along the axis, its tiles' arc apart (black, matte) from the steel.
        const float top=static_cast<float>(s.radiusFrom),bottom=static_cast<float>(s.radiusTo),height=static_cast<float>(s.to-s.from),y=static_cast<float>(half-(s.from+s.to)/2);
        const float tiles=static_cast<float>(std::clamp(s.tiles,0.,2*3.14159265358979));
        if(tiles<2*pi){auto g=CylinderGeometry::create(top,bottom,height,48,1,false,tiles/2,2*pi-tiles);g->translate(0,y,0);root_->add(part(g,assistedMaterial(Color(s.color),.85f,.32f)));}
        if(tiles>0){auto g=CylinderGeometry::create(top*1.004f,bottom*1.004f,height,48,1,false,-tiles/2,tiles);g->translate(0,y,0);root_->add(part(g,assistedMaterial(Color(0x161616),0.f,.85f)));}
    }
    for(const auto& p:look.parts){ // A plate out from its hinge line; a grid fin's as a lattice of bars.
        Part x;x.pivot=Object3D::create();x.pivot->position.copy(point(p.hinge));x.axis=axisOf(p.axis).normalize();x.gain=p.gain;x.offset=p.offset;x.surface=p.surface;
        Vector3 out=axisOf(p.outward);out.sub(Vector3(x.axis).multiplyScalar(out.dot(x.axis)));if(out.lengthSq()<1e-12f)out.set(1,0,0);out.normalize();
        Vector3 normal;normal.crossVectors(x.axis,out);Matrix4 basis;basis.makeBasis(out,normal,x.axis);x.base.setFromRotationMatrix(basis);
        const float w=static_cast<float>(p.width),t=static_cast<float>(p.thickness),span=static_cast<float>(p.span);const auto m=assistedMaterial(Color(p.color),.6f,.45f);
        if(!p.grid){auto g=BoxGeometry::create(w,t,span);g->translate(w/2,0,0);x.pivot->add(part(g,m));}
        else for(int i=0;i<=4;++i){auto a=BoxGeometry::create(w,t,span*.03f);a->translate(0,0,span*(i/4.f-.5f));x.pivot->add(part(a,m)); // Centred on its spindle.
            auto b=BoxGeometry::create(w*.03f,t,span);b->translate(w*(i/4.f-.5f),0,0);x.pivot->add(part(b,m));}
        root_->add(x.pivot);parts_.push_back(std::move(x));
    }
    for(const auto& e:look.engines){ // The bell from its throat (the gimbal's pivot) to the exit, and its plume beyond.
        Engine x;x.direction=e.direction;x.radius=e.exitRadius;const float r=static_cast<float>(e.exitRadius),length=2.2f*r;
        const Vector3 along=axisOf(e.direction).normalize();x.pivot=Object3D::create();x.pivot->position.copy(point(e.position)).add(Vector3(along).multiplyScalar(length));
        auto bell=CylinderGeometry::create(.38f*r,r,length,32,1,true);bell->translate(0,-length/2,0);x.pivot->add(part(bell,assistedMaterial(Color(0x3a3530),.7f,.5f,false,true)));
        // Two plumes of fixed shape per unit length, scaled evenly (the shader's normals stay true): narrow in thick air, a wide fan
        // in vacuum, each a core and a sheath; their strengths cross over with the pressure.
        for(const auto [top,bottom]:{std::pair{.02f,.04f},{.035f,.08f},{.04f,.32f},{.07f,.55f}}){
            auto g=CylinderGeometry::create(top,bottom,1,24,1,true);g->translate(0,-.5f,0);auto plume=Mesh::create(g,plumeMaterial());plume->position.y=-length;plume->visible=false;
            plume->renderOrder=10;x.pivot->add(plume);x.plume.push_back(plume);}
        root_->add(x.pivot);engines_.push_back(std::move(x));
    }
    for(const auto& t:look.thrusters){ // A cold-gas puff out against the push.
        Jet j;auto g=CylinderGeometry::create(.15f,.9f,3.5f,16,1,true);g->translate(0,-1.75f,0);j.mesh=Mesh::create(g,glow(0xdfe8ff,.5f));j.mesh->visible=false;j.mesh->renderOrder=10;
        j.mesh->position.copy(point(t.position));j.mesh->quaternion.setFromUnitVectors(Vector3(0,1,0),axisOf(t.direction).normalize());root_->add(j.mesh);jets_.push_back(std::move(j));
    }
}
void Vehicle::animate(const std::vector<float>* parts){
    const std::size_t engines=engines_.size(),need=engines*3+jets_.size()+1;const std::size_t surfaces=parts&&parts->size()>=need?parts->size()-need:0;
    auto at=[&](std::size_t i){return parts&&i<parts->size()?(*parts)[i]:0.f;};
    const float pressure=parts&&!parts->empty()?parts->back():101325.f,thin=1-std::clamp(pressure/101325.f,0.f,1.f); // 1 in vacuum.
    std::size_t lit=0;for(std::size_t i=0;i<engines;++i)lit+=at(3*i)>.01f;
    const float crowd=std::min(1.f,6.f/static_cast<float>(std::max<std::size_t>(lit,1))); // Many plumes overlap: each fainter.
    for(std::size_t i=0;i<engines;++i){ // The gimballed thrust as the physics turns it: yaw about z, then pitch about y.
        auto& e=engines_[i];const float share=at(3*i),pitch=at(3*i+1),yaw=at(3*i+2);const auto& d=e.direction;
        const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);const core::Vector yawed{d.x*cy+d.y*sy,-d.x*sy+d.y*cy,d.z};
        e.pivot->quaternion.setFromUnitVectors(Vector3(0,1,0),axisOf({yawed.x*cp-yawed.z*sp,yawed.y,yawed.x*sp+yawed.z*cp}).normalize());
        // The plume: longer with the thrust, wider and fainter as the air thins (in vacuum it fans out).
        const bool on=share>.01f;const float r=static_cast<float>(e.radius),length=r*(30+40*thin)*std::sqrt(std::max(share,0.f)),t=thin;
        for(std::size_t k=0;k<e.plume.size();++k){auto& p=*e.plume[k];const bool vacuum=k>=2,core=k%2==0;
            const float strength=(core?2.6f:1.1f)*(vacuum?.35f*t:1-t)*crowd*std::clamp(share*1.5f,0.f,1.f);p.visible=on&&strength>.003f;p.scale.setScalar(length);
            auto& u=p.material()->as<ShaderMaterial>()->uniforms;u.at("strength").setValue(strength); // Warm in thick air, a faint blue in vacuum.
            u.at("core").setValue(vacuum?Vector3(.45f,.55f,1):Vector3(1,.75f,.4f));u.at("edge").setValue(vacuum?Vector3(.25f,.3f,.9f):Vector3(1,.35f,.08f));}
    }
    for(auto& p:parts_){const float d=p.surface>0&&p.surface<=surfaces?at(engines*3+p.surface-1):0.f;
        Quaternion turn;turn.setFromAxisAngle(p.axis,static_cast<float>(p.gain*d+p.offset));p.pivot->quaternion.copy(turn).multiply(p.base);}
    for(std::size_t i=0;i<jets_.size();++i){const float duty=at(engines*3+surfaces+i);auto& j=*jets_[i].mesh;j.visible=duty>.02f;
        j.scale.set(1,.4f+.6f*duty,1);j.material()->as<MeshBasicMaterial>()->opacity=.55f*std::min(1.f,duty*2)*(.3f+.7f*thin);}
}
}
