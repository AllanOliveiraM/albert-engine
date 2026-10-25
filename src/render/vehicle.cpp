#include "vehicle.hpp"
#include "lighting.hpp"
#include <algorithm>
#include <array>
#include <cmath>
using namespace threepp;
namespace albert {
namespace {
constexpr float pi=3.14159265358979f;
Vector3 blackBody(float t){ // The same radiative colour and brightness for tiles, bells and the aft shield.
    const float h=t/100,bright=4*std::pow(std::clamp((t-900)/1000,0.f,1.f),2.f);
    return Vector3(1.f,std::clamp((99.47f*std::log(std::max(h,1.f))-161.12f)/255.f,0.f,1.f),h<19?0.f:std::clamp((138.52f*std::log(h-10)-305.04f)/255.f,0.f,1.f)).multiplyScalar(bright);
}
// The flight API's body axes (x forward, y right, z down) as the node's local ones (y along the nose, x along body -y).
Vector3 axisOf(core::Vector v){return {static_cast<float>(-v.y),static_cast<float>(v.x),static_cast<float>(v.z)};}
std::shared_ptr<Mesh> part(std::shared_ptr<BufferGeometry> g,std::shared_ptr<Material> m){auto x=Mesh::create(std::move(g),std::move(m));x->castShadow=x->receiveShadow=true;return x;}
// A plate from its hinge (x 0, along z, span long, t thick) out to its tip edge (x w, tip long centred at shift, tt thick): six
// flat faces, each wound outward.
std::shared_ptr<BufferGeometry> plate(float w,float t,float span,float tip,float shift,float tt){
    const Vector3 c[8]{{0,-t/2,-span/2},{0,-t/2,span/2},{0,t/2,span/2},{0,t/2,-span/2},{w,-tt/2,shift-tip/2},{w,-tt/2,shift+tip/2},{w,tt/2,shift+tip/2},{w,tt/2,shift-tip/2}};
    Vector3 mid;for(const auto& v:c)mid.add(v);mid.multiplyScalar(1.f/8);std::vector<float> p;
    for(auto q:{std::array<int,4>{0,4,5,1},{3,2,6,7},{0,1,2,3},{4,7,6,5},{0,3,7,4},{1,5,6,2}}){
        Vector3 a=c[q[1]],b=c[q[2]],f;a.sub(c[q[0]]);b.sub(c[q[0]]);a.cross(b);for(int i:q)f.add(c[i]);f.multiplyScalar(.25f).sub(mid);
        if(a.dot(f)<0)std::swap(q[1],q[3]);for(int i:{q[0],q[1],q[2],q[0],q[2],q[3]})p.insert(p.end(),{c[i].x,c[i].y,c[i].z});}
    auto g=BufferGeometry::create();g->setAttribute("position",FloatBufferAttribute::create(std::move(p),3));g->computeVertexNormals();return g;
}
std::shared_ptr<ShaderMaterial> plumeMaterial(){ // Dense exhaust hides the background; rarefied gas mostly adds light.
    auto m=ShaderMaterial::create();m->name="albert:plume";m->transparent=true;m->depthWrite=false;m->side=Side::Double;m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;
    m->uniforms={{"core",Uniform(Vector3(.78f,.86f,1))},{"edge",Uniform(Vector3(1,.28f,.19f))},{"strength",Uniform(0.f)},{"diamonds",Uniform(0.f)},
        {"time",Uniform(0.f)},{"radii",Uniform(Vector2(1,1))},{"falloff",Uniform(3.f)},{"rarefied",Uniform(0.f)},{"disc",Uniform(0.f)},{"metres",Uniform(1.f)}};return m;
}
std::shared_ptr<MeshBasicMaterial> glow(std::uint32_t colour,float opacity){ // A jet: unlit, added to what is behind.
    auto m=MeshBasicMaterial::create();m->color=Color(colour);m->transparent=true;m->opacity=opacity;m->blending=Blending::Additive;m->depthWrite=false;m->side=Side::Double;return m;
}
}
Vehicle::Vehicle(const core::VehicleLook& look){
    root_=Mesh::create(BufferGeometry::create());root_->frustumCulled=false;length_=look.length;
    const double half=look.length/2;
    auto point=[&](core::Vector p){return Vector3(static_cast<float>(-p.y),static_cast<float>(half-p.x),static_cast<float>(p.z));}; // x aft of the nose.
    // The engine bay: the aft skirt open, a heat shield across it where the bells come out (most of each below it).
    double base=0,bay=1e9;for(const auto& e:look.engines){base=std::max(base,e.position.x);bay=std::min(bay,(e.exitRadius>1?2.6:2.2)*e.exitRadius*.85);} // The shortest bell's.
    for(const auto& s:look.sections){ // A frustum along the axis, its tiles' arc apart (black, matte) from the steel; the aft one open below.
        const float top=static_cast<float>(s.radiusFrom),bottom=static_cast<float>(s.radiusTo),height=static_cast<float>(s.to-s.from),y=static_cast<float>(half-(s.from+s.to)/2);
        const float tiles=static_cast<float>(std::clamp(s.tiles,0.,2*3.14159265358979));const bool skirt=!look.engines.empty()&&s.to>=base-.01;
        auto body=[&](float t,float b,float from,float arc,std::shared_ptr<Material> m){auto g=CylinderGeometry::create(t,b,height,48,1,skirt,from,arc);g->translate(0,y,0);root_->add(part(g,m));};
        if(tiles<2*pi){auto m=assistedMaterial(Color(s.color),.85f,.32f,false,skirt);if(skirt)aft_.push_back(m);body(top,bottom,tiles/2,2*pi-tiles,m);}
        if(tiles>0){auto tile=assistedMaterial(Color(0x161616),0.f,.85f,false,skirt);tiles_.push_back(tile);body(top*1.004f,bottom*1.004f,-tiles/2,tiles,tile);}
        if(skirt){auto g=CircleGeometry::create(bottom*.995f,64);g->rotateX(pi/2);g->translate(0,static_cast<float>(half-(base-bay)),0); // The shield, facing aft.
            auto m=assistedMaterial(Color(0x26262a),.35f,.6f,false,true);aft_.push_back(m);root_->add(part(g,m));}
    }
    for(const auto& p:look.parts){ // A plate out from its hinge line; a grid fin's as a lattice of bars.
        Part x;x.pivot=Object3D::create();x.pivot->position.copy(point(p.hinge));x.axis=axisOf(p.axis).normalize();x.gain=p.gain;x.offset=p.offset;x.surface=p.surface;
        Vector3 out=axisOf(p.outward);out.sub(Vector3(x.axis).multiplyScalar(out.dot(x.axis)));if(out.lengthSq()<1e-12f)out.set(1,0,0);out.normalize();
        Vector3 normal;normal.crossVectors(x.axis,out);Matrix4 basis;basis.makeBasis(out,normal,x.axis);x.base.setFromRotationMatrix(basis);
        const float w=static_cast<float>(p.width),t=static_cast<float>(p.thickness),span=static_cast<float>(p.span);const auto m=assistedMaterial(Color(p.color),.6f,.45f);
        if(!p.grid){if(p.tipSpan<0&&p.tipShift==0&&p.tipThickness<0){auto g=BoxGeometry::create(w,t,span);g->translate(w/2,0,0);x.pivot->add(part(g,m));}
            else{const float along=Vector3(static_cast<float>(-p.axis.y),static_cast<float>(-p.axis.x),static_cast<float>(p.axis.z)).dot(x.axis)<0?-1.f:1.f; // The shift along the axis as placed (axisOf turns x the other way).
                x.pivot->add(part(plate(w,t,span,static_cast<float>(p.tipSpan<0?p.span:p.tipSpan),along*static_cast<float>(p.tipShift),static_cast<float>(p.tipThickness<0?p.thickness:p.tipThickness)),m));}}
        else for(int i=0;i<=4;++i){auto a=BoxGeometry::create(w,t,span*.03f);a->translate(0,0,span*(i/4.f-.5f));x.pivot->add(part(a,m)); // Centred on its spindle.
            auto b=BoxGeometry::create(w*.03f,t,span);b->translate(w*(i/4.f-.5f),0,0);x.pivot->add(part(b,m));}
        root_->add(x.pivot);parts_.push_back(std::move(x));
    }
    for(const auto& e:look.engines){ // The bell from its throat (the gimbal's pivot) to the exit, and its plume beyond.
        Engine x;x.direction=e.direction;x.radius=e.exitRadius;const float r=static_cast<float>(e.exitRadius),vacuum=r>1?1.f:0.f,length=(2.2f+.4f*vacuum)*r;
        const Vector3 along=axisOf(e.direction).normalize();x.pivot=Object3D::create();x.pivot->position.copy(point(e.position)).add(Vector3(along).multiplyScalar(length));
        // A nozzle's contour, from a throat a sixth of the exit (a third of it for a vacuum one: wider still) growing fast then
        // gently (√ of the way), dark metal outside and in; a vacuum bell's extension glows when it burns (radiatively cooled).
        std::vector<Vector2> contour;const float throat=r*(vacuum?.12f:.18f);
        for(int i=0;i<=24;++i){const float t=static_cast<float>(i)/24;contour.emplace_back(throat+(r-throat)*std::sqrt(t),-length*t);}
        auto bell=LatheGeometry::create(contour,40);x.bell=assistedMaterial(Color(vacuum?0x4a443e:0x3a3530),.75f,.45f,false,true);x.bell->name="albert:bell";
        x.bell->uniforms.emplace("burn",Uniform(Vector3()));x.bell->uniforms.emplace("lip",Uniform(Vector3()));x.bell->uniforms.emplace("bellLength",Uniform(length));x.pivot->add(part(bell,x.bell));
        if(vacuum>0){auto glowing=LatheGeometry::create(contour,40);auto m=glow(0xff7a2a,0.f);x.hot=Mesh::create(glowing,m);x.hot->scale.setScalar(1.01f);x.hot->visible=false;x.hot->renderOrder=9;x.pivot->add(x.hot);}
        // Two shells suffice: a long envelope and a short core; the shader widens only their downstream ends.
        for(std::size_t k=0;k<x.plume.size();++k){
            auto g=CylinderGeometry::create(1,1,1,24,1,true);g->translate(0,-.5f,0);auto plume=Mesh::create(g,plumeMaterial());plume->position.y=-length;plume->visible=false;
            plume->frustumCulled=false;plume->renderOrder=10+static_cast<int>(k);x.pivot->add(plume);x.plume[k]=plume;} // The shader's spread exceeds the cylinder's bounds.
        auto exit=CircleGeometry::create(r*.98f,32);exit->rotateX(pi/2);auto m=plumeMaterial();m->side=Side::Front;m->uniforms.at("disc").setValue(1.f);
        x.exit=Mesh::create(exit,m);x.exit->position.y=-length-.008f*r;x.exit->visible=false;x.exit->renderOrder=12;x.pivot->add(x.exit); // The open cone alone cannot fill an axial view.
        root_->add(x.pivot);engines_.push_back(std::move(x));
    }
    { // Condensation at Mach 1: a white collar flaring aft from where the nose meets the body (a fifth of the way down).
        const float r=static_cast<float>(look.diameter/2),l=static_cast<float>(look.length);auto g=CylinderGeometry::create(r*1.02f,r*2.3f,.22f*l,48,1,true);
        g->translate(0,static_cast<float>(half)-.31f*l,0);auto m=MeshBasicMaterial::create();m->color=Color(0xf4f6f8);m->transparent=true;m->opacity=0;m->depthWrite=false;m->side=Side::Double;
        vapour_=Mesh::create(g,m);vapour_->visible=false;vapour_->renderOrder=8;root_->add(vapour_);}
    { // The shock layer: an ellipsoid shell, a little longer than the vehicle and wider, set toward its belly (+z).
        const float r=static_cast<float>(look.diameter/2),l=static_cast<float>(look.length);auto g=SphereGeometry::create(1,48,24);
        g->scale(1.55f*r,.6f*l,1.55f*r);g->translate(0,0,.45f*r);auto m=ShaderMaterial::create();m->name="albert:plasma";m->transparent=true;m->depthWrite=false;
        m->side=Side::Front;m->blending=Blending::Additive;m->uniforms={{"core",Uniform(Vector3(1,.62f,.82f))},{"edge",Uniform(Vector3(1,.32f,.12f))},{"strength",Uniform(0.f)},{"upwind",Uniform(Vector3(0,0,1))}};
        plasma_=Mesh::create(g,m);plasma_->visible=false;plasma_->renderOrder=11;root_->add(plasma_);}
    { // Frost on the tanks below their liquid: white and matte over the steel (not the tiles: the widest tiles' arc of the body).
        double tiles=0;for(const auto& sct:look.sections)tiles=std::max(tiles,std::clamp(sct.tiles,0.,2*3.14159265358979));const float arc=static_cast<float>(tiles);
        const float r=static_cast<float>(look.diameter/2)*1.006f;
        for(const auto& t:look.tanks){auto g=CylinderGeometry::create(r,r,1,48,1,true,arc/2,2*pi-arc);g->translate(0,-.5f,0); // Its top at 0: scaled down from there.
            auto m=Mesh::create(g,assistedMaterial(Color(0xe8edf0),0.f,.95f));m->visible=false;root_->add(m);frost_.push_back({m,t});}}
    for(const auto& t:look.thrusters){ // A cold-gas puff out against the push.
        Jet j;auto g=CylinderGeometry::create(.15f,.9f,3.5f,16,1,true);g->translate(0,-1.75f,0);j.mesh=Mesh::create(g,glow(0xdfe8ff,.5f));j.mesh->visible=false;j.mesh->renderOrder=10;
        j.mesh->position.copy(point(t.position));j.mesh->quaternion.setFromUnitVectors(Vector3(0,1,0),axisOf(t.direction).normalize());root_->add(j.mesh);jets_.push_back(std::move(j));
    }
}
float Vehicle::gimballing(const Object3D& node,const core::VehicleLook& look){
    float angle=0;std::size_t i=0;
    for(const auto& pivot:node.children){if(pivot->children.empty())continue;const auto bell=pivot->children.front()->material();if(!bell||bell->name!="albert:bell")continue;
        if(i>=look.engines.size())break;Vector3 along(0,1,0);along.applyQuaternion(pivot->quaternion);
        angle=std::max(angle,along.angleTo(axisOf(look.engines[i++].direction).normalize()));}
    return angle;
}
void Vehicle::explode(float t){
    if(!fire_){ // The first moment: the vehicle gone, a fireball, its smoke and pieces of it thrown out.
        for(auto& c:root_->children)c->visible=false;
        auto fire=MeshBasicMaterial::create();fire->transparent=true;fire->blending=Blending::Additive;fire->depthWrite=false;fire_=Mesh::create(SphereGeometry::create(1,32,20),fire);root_->add(fire_);
        smoke_=Mesh::create(SphereGeometry::create(1,24,16),assistedMaterial(Color(0x2a2724),0.f,1.f));smoke_->material()->transparent=true;smoke_->material()->depthWrite=false;root_->add(smoke_);
        std::uint32_t seed=0x9e3779b9u;auto random=[&]{seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return static_cast<float>(seed)/4294967296.f;};
        const float l=static_cast<float>(length_);
        for(int i=0;i<36;++i){const float size=.4f+3.f*random()*random();Vector3 d(random()-.5f,random()-.5f,random()-.5f);d.normalize();
            auto m=part(BoxGeometry::create(size,size*(.3f+random()),size*(.5f+random())),assistedMaterial(Color(i%3?0x9a9894:0x1d1d1d),.7f,.5f));
            m->position.set((random()-.5f)*2,(random()-.5f)*l,(random()-.5f)*2);root_->add(m);pieces_.push_back({m,d.multiplyScalar(15+50*random()),Vector3(random()-.5f,random()-.5f,random()-.5f).multiplyScalar(6)});}}
    const float l=static_cast<float>(length_),grow=1-std::exp(-t/.8f),fade=std::exp(-t/1.5f);
    fire_->scale.setScalar(l*(.25f+.9f*grow));auto* f=fire_->material()->as<MeshBasicMaterial>();f->opacity=std::clamp(fade,0.f,1.f);
    f->color=Color(1.f,std::clamp(.95f-.5f*t,.25f,.95f),std::clamp(.8f-1.2f*t,.05f,.8f));fire_->visible=fade>.01f; // White-hot, then orange, then out.
    smoke_->scale.setScalar(l*(.35f+1.6f*(1-std::exp(-t/4))));smoke_->material()->opacity=.65f*std::clamp(t/.6f,0.f,1.f)*std::exp(-t/25);
    for(auto& p:pieces_){p.mesh->position.copy(p.velocity).multiplyScalar(std::min(t,30.f));p.mesh->rotation.set(p.spin.x*t,p.spin.y*t,p.spin.z*t);p.mesh->visible=t<20;}
}
void Vehicle::shine(float heating,float fill){
    const float glow=std::pow(std::clamp((heating-4e4f)/6e5f,0.f,1.f),.8f)*1.3f;plasma_->visible=glow>.003f;
    plasma_->material()->as<ShaderMaterial>()->uniforms.at("strength").setValue(glow);
    { // The tiles at their radiative equilibrium under 60% of the stagnation's flux (ε 0.85), glowing as a black body does: dull red
      // from ~900 K, orange-yellow by 1900 K (Planck's colour by temperature, its brightness rising steeply with it).
        const float t=std::pow(std::max(.6f*heating,0.f)/(.85f*5.670e-8f),.25f);const Vector3 c=blackBody(t);
        for(auto& m:tiles_)m->uniforms.at("emissive").setValue(c);
        aftGlow_=blackBody(.8f*t).multiplyScalar(.12f);for(auto& m:aft_)m->uniforms.at("emissive").setValue(aftGlow_);} // The sheltered aft metal runs cooler than the tiles.
    const float half=static_cast<float>(length_/2);
    for(auto& f:frost_){const double level=f.tank.full?1.:std::clamp(static_cast<double>(fill),0.,1.),top=f.tank.to-level*(f.tank.to-f.tank.from); // Liquid settles aft.
        f.mesh->visible=level>.005;f.mesh->position.y=static_cast<float>(half-top);f.mesh->scale.y=static_cast<float>(f.tank.to-top);}
}
void Vehicle::animate(const std::vector<float>* parts){
    const auto now=std::chrono::steady_clock::now();const float dt=static_cast<float>(std::chrono::duration<double>(now-updated_).count());updated_=now;time_+=dt;const float time=static_cast<float>(std::fmod(time_,4096.));
    const std::size_t engines=engines_.size(),need=engines*3+jets_.size()+7;const std::size_t surfaces=parts&&parts->size()>=need?parts->size()-need:0;
    auto at=[&](std::size_t i){return parts&&i<parts->size()?(*parts)[i]:0.f;};
    if(parts&&parts->size()>=7&&parts->back()>=0){explode(parts->back());return;} // Broken up: its pieces.
    auto tail=[&](std::size_t k,float fallback){return parts&&parts->size()>=7?(*parts)[parts->size()-7+k]:fallback;}; // Pressure, heat flux, fill, Mach, α, β.
    { // The shock layer's bright side toward the air's way (the body's velocity through it: x along the nose as local y, y as -x, z as z).
        const float al=tail(4,0),be=tail(5,0);Vector3 up(-std::sin(be),std::cos(al)*std::cos(be),std::sin(al)*std::cos(be));
        up.transformDirection(*root_->matrixWorld);plasma_->material()->as<ShaderMaterial>()->uniforms.at("upwind").setValue(up);}
    const float pressure=tail(0,101325.f),ambient=std::clamp(pressure,0.f,101325.f),thin=1-ambient/101325.f,air=std::sqrt(ambient/101325.f),expansion=std::log(101375.f/(ambient+50.f))/std::log(2027.5f),heating=tail(1,0),fill=tail(2,0),mach=std::abs(tail(3,0)); // Log pressure keeps expansion gradual from liftoff through MECO to vacuum.
    shine(heating,fill);
    { // Condensation round the body near Mach 1 in thick air: strongest at 1, gone by 0.88 and 1.2; flaring downstream (toward the
      // nose when it flies engines first).
        vapour_->scale.y=tail(3,0)<0?-1.f:1.f;const float v=std::clamp(1-std::abs(mach-1.f)/(mach<1?.12f:.2f),0.f,1.f)*std::clamp((pressure-1e4f)/3e4f,0.f,1.f);vapour_->visible=v>.01f;
        vapour_->material()->opacity=.42f*v;}
    const float diamonds=std::clamp((pressure-2e4f)/6e4f,0.f,1.f); // Shock diamonds in the plume, in thick air.
    std::size_t lit=0;for(std::size_t i=0;i<engines;++i)lit+=at(3*i)>.01f;
    const float crowd=std::min(1.f,6.f/static_cast<float>(std::max<std::size_t>(lit,1))); // Many plumes overlap: each fainter.
    for(std::size_t i=0;i<engines;++i){ // The gimballed thrust as the physics turns it: yaw about z, then pitch about y.
        auto& e=engines_[i];const float share=at(3*i),pitch=at(3*i+1),yaw=at(3*i+2);const auto& d=e.direction;
        const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);const core::Vector yawed{d.x*cy+d.y*sy,-d.x*sy+d.y*cy,d.z};
        e.pivot->quaternion.setFromUnitVectors(Vector3(0,1,0),axisOf({yawed.x*cp-yawed.z*sp,yawed.y,yawed.x*sp+yawed.z*cp}).normalize());
        const float throttle=std::clamp(share,0.f,1.f),r=static_cast<float>(e.radius),flow=std::sqrt(throttle);const bool on=throttle>.01f;
        e.heat+=(on?flow-e.heat:-e.heat)*(1-std::exp(-dt/(on?8.f:45.f))); // Thermal inertia leaves a red lip for roughly 30-60 seconds after a long burn.
        auto& bell=e.bell->uniforms;bell.at("burn").setValue(blackBody(300+1000*e.heat));bell.at("lip").setValue(blackBody(300+1350*e.heat));bell.at("emissive").setValue(aftGlow_);
        if(e.hot){e.hot->visible=e.heat>.4f;e.hot->material()->as<MeshBasicMaterial>()->opacity=.35f*std::max(0.f,(e.heat-.4f)/.6f);} // Keep the radiatively cooled vacuum extension warm between burns.
        e.exit->visible=on;auto& exit=e.exit->material()->as<ShaderMaterial>()->uniforms;exit.at("strength").setValue(.95f*throttle);exit.at("time").setValue(time+static_cast<float>(i)*1.73f);
        for(std::size_t k=0;k<e.plume.size();++k){auto& p=*e.plume[k];const bool core=k==1;
            const float strength=(core?1.7f*(.2f+.8f*air)*std::sqrt(crowd):(1.5f*air+.045f+.09f*(1-expansion))*crowd)*throttle;p.visible=on;
            const float metres=r*flow*(core?34.f+16.f*expansion:160.f+700.f*expansion); // Raptor: ~90 m visible at sea level, hundreds of metres near MECO.
            p.scale.set(r*flow,metres,r*flow);
            auto& u=p.material()->as<ShaderMaterial>()->uniforms;u.at("strength").setValue(strength);u.at("time").setValue(time+static_cast<float>(i)*1.73f);
            u.at("radii").setValue(Vector2(core?1.f:1.04f,core?1.35f+3.f*expansion:3.f+100.f*expansion*expansion));u.at("metres").setValue(metres);
            u.at("core").setValue(core?Vector3(.78f,.86f,1):Vector3(1-.4f*expansion,.82f-.12f*expansion,.32f+.68f*expansion));u.at("edge").setValue(core?Vector3(.44f,.55f,1):Vector3(1-.55f*expansion,.28f+.22f*expansion,.04f+.96f*expansion));
            u.at("diamonds").setValue(diamonds);u.at("falloff").setValue(core?3.8f:1.2f);u.at("rarefied").setValue(expansion);}
    }
    for(auto& p:parts_){const float d=p.surface>0&&p.surface<=surfaces?at(engines*3+p.surface-1):0.f;
        Quaternion turn;turn.setFromAxisAngle(p.axis,static_cast<float>(p.gain*d+p.offset));p.pivot->quaternion.copy(turn).multiply(p.base);}
    for(std::size_t i=0;i<jets_.size();++i){const float duty=at(engines*3+surfaces+i);auto& j=*jets_[i].mesh;j.visible=duty>.02f;
        j.scale.set(1,.4f+.6f*duty,1);j.material()->as<MeshBasicMaterial>()->opacity=.55f*std::min(1.f,duty*2)*(.3f+.7f*thin);}
}
}
