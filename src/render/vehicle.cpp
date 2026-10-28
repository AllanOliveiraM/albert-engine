#include "vehicle.hpp"
#include "lighting.hpp"
#include "core/vents.hpp"
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
        {"time",Uniform(0.f)},{"radii",Uniform(Vector2(1,1))},{"falloff",Uniform(3.f)},{"rarefied",Uniform(0.f)},{"disc",Uniform(0.f)},{"metres",Uniform(1.f)},{"vacuum",Uniform(0.f)},{"impingement",Uniform(0.f)},{"flash",Uniform(0.f)},{"inner",Uniform(0.f)},{"quality",Uniform(1.f)},{"spacing",Uniform(1.f)}};return m;
}
std::shared_ptr<ShaderMaterial> engineCloud(){ // One soft chord, for a brief flame or a faint purge parcel; no ray march.
    auto m=ShaderMaterial::create();m->name="albert:engine-cloud";m->transparent=true;m->depthWrite=false;m->side=Side::Double;
    m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;
    m->uniforms={{"density",Uniform(0.f)},{"time",Uniform(0.f)},{"seed",Uniform(0.f)},{"flame",Uniform(0.f)}};return m;
}
Vector3 graphical(core::Vector p){return {static_cast<float>(p.x),static_cast<float>(p.z),static_cast<float>(-p.y)};}
core::Vector physical(Vector3 p){return {p.x,-p.z,p.y};}
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
    auto surface=[&](Color colour,float metal,float rough,float kind,Vector4 hull={},float width=0,float span=0,bool open=false){
        auto m=assistedMaterial(colour,metal,rough,false,open);m->name="albert:vehicle";
        m->uniforms.emplace("vehicleHull",Uniform(hull));m->uniforms.emplace("vehicleSurface",Uniform(Vector4(kind,width,span,0.f)));
        m->uniforms.emplace("vehicleQuality",Uniform(1.f));m->uniforms.emplace("vehicleSoot",Uniform(0.f));m->uniforms.emplace("frostHeight",Uniform(1.f));surfaces_.push_back(m);return m;};
    for(const auto& s:look.sections){ // One frustum: the shader joins the wavy tile edge to the steel without overlapping shells.
        const float top=static_cast<float>(s.radiusFrom),bottom=static_cast<float>(s.radiusTo),height=static_cast<float>(s.to-s.from),y=static_cast<float>(half-(s.from+s.to)/2);
        const float tiles=static_cast<float>(std::clamp(s.tiles,0.,2*3.14159265358979));const bool skirt=!look.engines.empty()&&s.to>=base-.01;
        const bool vents=std::any_of(look.parts.begin(),look.parts.end(),[&](const auto& p){return p.name=="vent"&&p.hinge.x>=s.from&&p.hinge.x<s.to;});
        auto m=surface(vents?Color(.36f,.35f,.33f):Color(s.color).multiplyScalar(.79f),1.f,.3f,vents?3.f:0.f,
            Vector4(tiles,static_cast<float>(look.diameter/2),static_cast<float>(half-base),tiles>0?0.f:7.f),y,height,skirt);
        if(tiles>0)tiles_.push_back(m);else if(skirt)aft_.push_back(m);
        auto g=CylinderGeometry::create(top,bottom,height,64,1,skirt);g->translate(0,y,0);auto mesh=part(g,m);mesh->name=vents?"hot-staging vents":"vehicle hull";root_->add(mesh);
        if(skirt){auto g=CircleGeometry::create(bottom*.995f,64);g->rotateX(pi/2);g->translate(0,static_cast<float>(half-(base-bay)),0); // The shield, facing aft.
            auto m=assistedMaterial(Color(0x26262a),.35f,.6f,false,true);aft_.push_back(m);root_->add(part(g,m));}
    }
    for(const auto& p:look.parts){ // A plate out from its hinge line; a grid fin's as a lattice of bars.
        if(p.name=="vent")continue; // The band now draws the slots and their steel ligaments in one call.
        Part x;x.pivot=Object3D::create();x.pivot->position.copy(point(p.hinge));x.axis=axisOf(p.axis).normalize();x.gain=p.gain;x.offset=p.offset;x.surface=p.surface;
        Vector3 out=axisOf(p.outward);out.sub(Vector3(x.axis).multiplyScalar(out.dot(x.axis)));if(out.lengthSq()<1e-12f)out.set(1,0,0);out.normalize();
        Vector3 normal;normal.crossVectors(x.axis,out);Matrix4 basis;basis.makeBasis(out,normal,x.axis);x.base.setFromRotationMatrix(basis);
        const float w=static_cast<float>(p.width),t=static_cast<float>(p.thickness),span=static_cast<float>(p.span);
        const bool flap=p.name.starts_with("flap ")&&p.name!="flap fairing";
        const auto m=surface(p.grid?Color(.19f,.20f,.21f):flap?Color(.31f,.32f,.33f):Color(p.color),.85f,p.grid?.48f:.36f,1.f,Vector4(flap?(normal.z>0?pi:-pi):0.f,0.f,0.f,0.f),w,span);
        if(flap)tiles_.push_back(m);
        x.pivot->name=p.name;
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
        auto exit=CircleGeometry::create(r*.98f,32);exit->rotateX(pi/2);auto m=plumeMaterial();m->uniforms.at("disc").setValue(1.f);
        x.exit=Mesh::create(exit,m);x.exit->position.y=-length-.008f*r;x.exit->visible=false;x.exit->renderOrder=12;x.pivot->add(x.exit); // The open cone alone cannot fill an axial view.
        x.exitY=-length;x.torch=Mesh::create(SphereGeometry::create(1,12,8),glow(0xffb34c,0));x.torch->name="engine torch";x.torch->position.y=-length+.7f*r;
        x.torch->scale.set(.22f*r,.6f*r,.22f*r);x.torch->visible=false;x.torch->renderOrder=11;x.pivot->add(x.torch);
        x.detached=Object3D::create();x.detached->name="released exhaust";x.detached->visible=false;root_->add(x.detached);
        for(std::size_t k=0;k<x.remnant.size();++k){auto p=Mesh::create(x.plume[k]->geometry(),plumeMaterial());p->frustumCulled=false;p->renderOrder=10+static_cast<int>(k);x.detached->add(p);x.remnant[k]=p;}
        auto cloud=SphereGeometry::create(1,12,8);for(auto& p:x.gas){p.mesh=Mesh::create(cloud,engineCloud());p.mesh->name="engine puff";p.mesh->visible=false;p.mesh->renderOrder=9;p.mesh->frustumCulled=false;root_->add(p.mesh);}
        root_->add(x.pivot);engines_.push_back(std::move(x));
    }
    if(!engines_.empty()){exhaustLight_=PointLight::create(Color(1.f,.24f,.055f),0,120,2);exhaustLight_->name="exhaust light";root_->add(exhaustLight_);}
    { // Condensation at Mach 1: a white collar flaring aft from where the nose meets the body (a fifth of the way down).
        const float r=static_cast<float>(look.diameter/2),l=static_cast<float>(look.length);auto g=CylinderGeometry::create(r*1.02f,r*2.3f,.22f*l,48,1,true);
        g->translate(0,static_cast<float>(half)-.31f*l,0);auto m=MeshBasicMaterial::create();m->color=Color(0xf4f6f8);m->transparent=true;m->opacity=0;m->depthWrite=false;m->side=Side::Double;
        vapour_=Mesh::create(g,m);vapour_->visible=false;vapour_->renderOrder=8;root_->add(vapour_);}
    { // The shock layer: an ellipsoid shell, a little longer than the vehicle and wider, set toward its belly (+z).
        const float r=static_cast<float>(look.diameter/2),l=static_cast<float>(look.length);auto g=SphereGeometry::create(1,48,24);
        g->scale(1.55f*r,.6f*l,1.55f*r);g->translate(0,0,.45f*r);auto m=ShaderMaterial::create();m->name="albert:plasma";m->transparent=true;m->depthWrite=false;
        m->side=Side::Front;m->blending=Blending::Additive;m->uniforms={{"core",Uniform(Vector3(1,.62f,.82f))},{"edge",Uniform(Vector3(1,.32f,.12f))},{"strength",Uniform(0.f)},{"upwind",Uniform(Vector3(0,0,1))}};
        plasma_=Mesh::create(g,m);plasma_->visible=false;plasma_->renderOrder=11;root_->add(plasma_);}
    { // Frost below each tank's liquid, over its own steel arc: the ship's tiles must not mask the booster's frost in a stack.
        double tiles=0;for(const auto& sct:look.sections)tiles=std::max(tiles,std::clamp(sct.tiles,0.,2*3.14159265358979));
        const float r=static_cast<float>(look.diameter/2)*1.006f;radius_=r;frostArc_=static_cast<float>(tiles);
        for(const auto& t:look.tanks){double covered=0;for(const auto& s:look.sections)if(s.from<t.to&&s.to>t.from)covered=std::max(covered,std::clamp(s.tiles,0.,2*3.14159265358979));
            const float arc=static_cast<float>(covered);if(arc>=2*pi)continue;auto g=CylinderGeometry::create(r,r,1,64,1,true);g->translate(0,-.5f,0); // Its top at 0: scaled down from there; the shader keeps the wavy tiles clear.
            auto material=surface(Color(.83f,.88f,.91f),0.f,.95f,2.f,Vector4(arc,r,0.f,0.f));material->transparent=true;material->depthWrite=false;
            auto m=Mesh::create(g,material);m->name="tank frost";m->receiveShadow=true;m->visible=false;root_->add(m);frost_.push_back({m,t});}}
    if(!frost_.empty()){ // Fixed slots keep countdown vapour cheap beside the engine plumes.
        for(const auto& v:core::stackVents(look)){Vector3 d(static_cast<float>(std::sin(v.angle)),0,static_cast<float>(std::cos(v.angle)));
            vents_.push_back({Vector3(d).multiplyScalar(radius_).setY(static_cast<float>(v.height)),d,static_cast<float>(v.gain)});}
        auto g=SphereGeometry::create(1,16,10);for(auto& p:mist_){auto m=ShaderMaterial::create();m->name="albert:steam";m->transparent=true;m->depthTest=true;m->depthWrite=false;m->side=Side::Double;
            m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;m->uniforms={{"density",Uniform(0.f)},{"seed",Uniform(0.f)},{"time",Uniform(0.f)},{"up",Uniform(Vector3(0,1,0))},{"hull",Uniform(Vector4())},{"size",Uniform(Vector3(1,1,1))}};
            p.mesh=Mesh::create(g,m);p.mesh->visible=false;p.mesh->renderOrder=8;root_->add(p.mesh);}}
    for(const auto& t:look.thrusters){ // A cold-gas puff out against the push.
        Jet j;auto g=CylinderGeometry::create(.15f,.9f,3.5f,16,1,true);g->translate(0,-1.75f,0);j.mesh=Mesh::create(g,glow(0xdfe8ff,.5f));j.mesh->visible=false;j.mesh->renderOrder=10;
        j.mesh->position.copy(point(t.position));j.mesh->quaternion.setFromUnitVectors(Vector3(0,1,0),axisOf(t.direction).normalize());root_->add(j.mesh);jets_.push_back(std::move(j));
    }
}
void Vehicle::quality(int value){
    for(auto& m:surfaces_)m->uniforms.at("vehicleQuality").setValue(value>0?1.f:0.f);
    for(auto& e:engines_)for(auto* shells:{&e.plume,&e.remnant})for(auto& p:*shells)p->material()->as<ShaderMaterial>()->uniforms.at("quality").setValue(value>0?1.f:0.f);
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
        f.mesh->visible=level>.005;f.mesh->position.y=static_cast<float>(half-top);f.mesh->scale.y=static_cast<float>(f.tank.to-top);
        auto* m=f.mesh->material()->as<ShaderMaterial>();m->uniforms.at("opacity").setValue(std::min(1.f,static_cast<float>(level)*8));m->uniforms.at("frostHeight").setValue(f.mesh->scale.y);
        auto hull=m->uniforms.at("vehicleHull").value<Vector4>();hull.z=f.mesh->position.y;m->uniforms.at("vehicleHull").setValue(hull);}
}
void Vehicle::impinge(float metres,float glow){obstruction_=std::max(0.f,metres);impingement_=std::clamp(glow,0.f,1.f);}
void Vehicle::venting(float strength,double utc,bool closed){
    if(!std::isfinite(utc))return;const bool reset=ventTimed_&&utc<ventUtc_;
    const float dt=ventTimed_&&!reset?static_cast<float>(utc-ventUtc_):0.f;ventUtc_=utc;ventTimed_=true;
    if(reset){for(auto& p:mist_){p.life=0;p.mesh->visible=false;}for(auto& v:vents_)v.pending=0;}
    strength=std::isfinite(strength)?std::clamp(strength,0.f,1.f):0;if(vents_.empty())return;
    if(strength<=0||fire_){for(auto& p:mist_){p.life=0;p.mesh->visible=false;}for(auto& v:vents_)v.pending=0;return;}
    const float step=std::min(dt,.25f),half=static_cast<float>(length_/2);
    auto random=[&]{ventSeed_^=ventSeed_<<13;ventSeed_^=ventSeed_>>17;ventSeed_^=ventSeed_<<5;return static_cast<float>(ventSeed_>>8)/16777216.f;};
    Vector3 pole(0,1,0);Quaternion inverse=root_->quaternion;inverse.invert();pole.applyQuaternion(inverse);Vector3 east;east.crossVectors(pole,Vector3(0,1,0));
    if(east.lengthSq()<1e-6f)east.set(1,0,0);else east.normalize(); // Earth's render pole is +y; wind must not rotate with the vehicle's roll.
    for(auto& p:mist_)if(p.life>0){p.age+=dt;if(p.age>=p.life){p.life=0;p.mesh->visible=false;}}
    for(std::size_t k=0;k<vents_.size();++k){auto& v=vents_[k];const float flow=static_cast<float>(core::ventFlow(k,utc,closed));
        if(flow<=0){v.pending=0;continue;}v.pending=std::min(2.f,v.pending+step*5.f/static_cast<float>(vents_.size())*flow*v.gain*strength);
        while(v.pending>=1){v.pending-=1;Puff* slot=nullptr;for(std::size_t i=0;i<24;++i)if(mist_[i].life<=0){slot=&mist_[i];break;}if(!slot)break;
            auto& p=*slot;p.age=0;p.life=10+4*random();p.seed=random();p.strength=v.gain*flow;p.outward=v.direction;p.origin=Vector3(v.origin).add(Vector3(p.outward).multiplyScalar(1.5f));p.velocity=Vector3(v.direction).multiplyScalar(4+2*random());
            p.drift=Vector3(east).multiplyScalar(1.1f+.8f*random());p.drift.y=-.18f-.16f*random();p.mesh->quaternion.setFromUnitVectors(Vector3(1,0,0),v.direction);}}
    for(std::size_t i=24;i<40;++i){auto& p=mist_[i];const auto& f=frost_[(i-24)%frost_.size()];
        if(!f.mesh->visible||frostArc_>=2*pi){p.life=0;p.mesh->visible=false;continue;}
        if(p.life<=0){p.age=0;p.seed=random();p.strength=.09f+.05f*random();p.life=3+3*random();p.level=random();
            const float arc=2*pi-frostArc_;p.width=std::min(.25f+.35f*random(),radius_*arc*.25f);const float margin=std::atan2(p.width,radius_);
            const float angle=frostArc_/2+margin+(arc-2*margin)*(static_cast<float>(i-24)+.15f+.7f*random())/16.f;
            p.origin.set(std::sin(angle)*(radius_+.11f),0,std::cos(angle)*(radius_+.11f));p.mesh->rotation.set(0,angle,0);}
    }
    for(std::size_t i=40;i<mist_.size();++i){auto& p=mist_[i];if(p.life<=0){p.age=0;p.life=9+6*random();p.seed=random();p.strength=.15f+.1f*random();
            const float angle=2*pi*random();p.outward.set(std::sin(angle),0,std::cos(angle));p.origin=Vector3(p.outward).multiplyScalar(radius_+3).setY(-half-1.f);p.drift=Vector3(east).multiplyScalar(1.1f+.7f*random());p.mesh->rotation.set(0,angle,0);}}
    for(std::size_t i=0;i<mist_.size();++i){auto& p=mist_[i];if(p.life<=0)continue;auto& m=*p.mesh;const float a=p.age,t=a/p.life,fade=std::min(1.f,(p.life-a)/2.f),birth=std::min(1.f,.15f+a/.3f);float density=0;
        if(i<24){const float grow=1-std::exp(-a/2.6f),jet=.7f*(1-std::exp(-a/.7f));m.scale.set(1.5f+(13+3*p.seed)*grow,.35f+(8+3*p.seed)*grow,.45f+(11+3*p.seed)*grow);
            m.position.copy(p.origin).add(Vector3(p.outward).multiplyScalar(m.scale.x-1.5f)).add(Vector3(p.velocity).multiplyScalar(jet)).add(Vector3(p.drift).multiplyScalar(a));m.position.y=std::max(m.position.y,-half-1.f+.5f*m.scale.y);
            density=.65f*p.strength*birth*std::exp(-a/4.5f)*fade;}
        else if(i<40){const auto& f=frost_[(i-24)%frost_.size()];const float h=f.mesh->scale.y,sheet=std::min(1.2f,h*.2f);m.scale.set(p.width,sheet,.11f);
            m.position.copy(p.origin);m.position.y=f.mesh->position.y-sheet-(h-2*sheet)*p.level-.5f*a;density=p.strength*std::sin(pi*t)*fade;
            auto& u=m.material()->as<ShaderMaterial>()->uniforms;u.at("hull").setValue(Vector4(radius_,radius_+.11f,f.mesh->position.y-h-m.position.y,f.mesh->position.y-m.position.y));u.at("size").setValue(m.scale);}
        else{m.scale.set(3+10*t,.3f+.65f*t,3+8*t);m.position.copy(p.origin).add(Vector3(p.drift).multiplyScalar(a));m.position.y+=m.scale.y*.7f;density=p.strength*birth*fade*(1-.65f*t);}
        if(i<24||i>=40){const float reach=i<24?m.scale.x:m.scale.z,gap=radius_+reach-m.position.dot(p.outward);if(gap>0)m.position.add(Vector3(p.outward).multiplyScalar(gap));} // Keep growing puffs outside the hull.
        m.visible=density*strength>.001f;auto& u=m.material()->as<ShaderMaterial>()->uniforms;u.at("density").setValue(density*strength);u.at("seed").setValue(p.seed*97.f);u.at("time").setValue(a);
    }
}
void Vehicle::animate(const std::vector<float>* parts,double utc,core::Vector position,core::Vector velocity){
    const auto now=std::chrono::steady_clock::now();const float dt=static_cast<float>(std::chrono::duration<double>(now-updated_).count());updated_=now;time_+=dt;const float time=static_cast<float>(std::fmod(time_,4096.));
    const std::size_t engines=engines_.size(),need=engines*3+jets_.size()+7;const std::size_t surfaces=parts&&parts->size()>=need?parts->size()-need:0;
    auto at=[&](std::size_t i){return parts&&i<parts->size()?(*parts)[i]:0.f;};
    if(std::isfinite(utc)){ // Cosmetic deposition only, from observed engine flow and simulated elapsed time; rewind cleans it.
        if(surfaceTimed_&&utc<surfaceUtc_)soot_=0;
        const float seconds=surfaceTimed_?static_cast<float>(std::clamp(utc-surfaceUtc_,0.,10.)):0.f;
        float flow=0;for(std::size_t i=0;i<engines;++i)flow+=std::clamp(at(3*i),0.f,1.f)/static_cast<float>(engines);
        soot_=std::min(1.f,soot_+seconds*flow/90.f);surfaceUtc_=utc;surfaceTimed_=true;
        for(auto& m:surfaces_)m->uniforms.at("vehicleSoot").setValue(soot_);}
    if(parts&&parts->size()>=7&&parts->back()>=0){if(exhaustLight_)exhaustLight_->intensity=0;explode(parts->back());return;} // Broken up: its pieces.
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
    const float space=std::clamp((expansion-.98f)/.02f,0.f,1.f),vacuum=space*space*(3-2*space),hit=obstruction_>0?impingement_:0; // Keep the MECO bloom before smoothly losing collisional light in vacuum.
    Quaternion inverse=root_->quaternion;inverse.invert();
    auto local=[&](core::Vector p){return graphical(p-position).applyQuaternion(inverse);};
    const float al=tail(4,0),be=tail(5,0);Vector3 relative(-std::sin(be),std::cos(al)*std::cos(be),std::sin(al)*std::cos(be));
    relative.multiplyScalar(mach*340).applyQuaternion(root_->quaternion);const core::Vector relativeFlow=physical(relative); // Approximate airspeed from the Mach and incidence already in the scene.
    if(!engines_.empty()&&engines_.front().cycle.known&&utc<engines_.front().cycle.utc){engineSounds_={};soundSerial_=0;}
    std::size_t lit=0;thrust_=0;for(std::size_t i=0;i<engines;++i){lit+=at(3*i)>.01f;thrust_+=std::clamp(at(3*i),0.f,1.f)/static_cast<float>(engines);}
    const float crowd=std::min(1.f,6.f/static_cast<float>(std::max<std::size_t>(lit,1))); // Many plumes overlap: each fainter.
    Vector3 lightCentre;float lightFlow=0;
    for(std::size_t i=0;i<engines;++i){ // The gimballed thrust as the physics turns it: yaw about z, then pitch about y.
        auto& e=engines_[i];const float share=at(3*i),pitch=at(3*i+1),yaw=at(3*i+2);const auto& d=e.direction;
        const bool wasOn=e.cycle.on;e.cycle.step(share,utc);const auto& cycle=e.cycle;
        if(cycle.reset){e.detached->visible=false;e.remnantLife=0;for(auto& p:e.gas){p.life=0;p.mesh->visible=false;}}
        if(cycle.on&&!wasOn)for(auto& p:e.gas)if(p.pending)p.life=0; // A relight closes the purge; parcels already outside keep drifting.
        const float r=static_cast<float>(e.radius);
        if(cycle.shutdown){ // Freeze the last flowing shells in space; the next burn has its own pair.
            e.releasedUtc=utc;e.remnantWidth=e.plume[0]->scale.x;e.remnantLife=e.plume[0]->visible?.16f+.18f*air:0;e.inherited=velocity;e.flow=relativeFlow;e.attitude=root_->quaternion;e.attitude.multiply(e.pivot->quaternion);
            Vector3 exit(0,e.exitY,0);exit.applyQuaternion(e.pivot->quaternion).add(e.pivot->position).applyQuaternion(root_->quaternion);
            e.released=position+physical(exit);Vector3 out(0,-(18+22*air)*r,0);out.applyQuaternion(e.attitude);e.velocity=physical(out);
            for(std::size_t k=0;k<e.remnant.size();++k){auto& p=*e.remnant[k];const auto& from=*e.plume[k];p.scale.copy(from.scale);
                auto& u=p.material()->as<ShaderMaterial>()->uniforms;for(auto& [key,v]:from.material()->as<ShaderMaterial>()->uniforms)u.at(key)=v;
                e.remnantStrength[k]=u.at("strength").value<float>();u.at("diamonds").setValue(0.f);u.at("flash").setValue(0.f);}
        }
        const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);const core::Vector yawed{d.x*cy+d.y*sy,-d.x*sy+d.y*cy,d.z};
        e.pivot->quaternion.setFromUnitVectors(Vector3(0,1,0),axisOf({yawed.x*cp-yawed.z*sp,yawed.y,yawed.x*sp+yawed.z*cp}).normalize());
        const float throttle=cycle.burning(),flow=std::sqrt(throttle),mainAge=static_cast<float>(utc-cycle.started),rich=cycle.rich();const bool on=throttle>1e-5f;
        const float flash=cycle.on&&cycle.main?std::max(0.f,1-mainAge/.065f):0,torch=cycle.torching()?(.6f+.4f*std::sin(static_cast<float>(utc-cycle.started)*pi/.24f)):0;
        if(on){Vector3 source(0,e.exitY-3*r,0);source.applyQuaternion(e.pivot->quaternion).add(e.pivot->position);lightCentre.addScaledVector(source,throttle);lightFlow+=throttle;}
        e.torch->visible=torch>0;e.torch->material()->opacity=.85f*torch;
        if(cycle.ignition||cycle.shutdown){const auto serial=++soundSerial_;engineSounds_[serial%engineSounds_.size()]={serial,cycle.ignition?cycle.started:cycle.stopped,r,cycle.ignition};}
        auto release=[&](Gas& p,double born,float life,unsigned kind){p.born=born;p.pending=born>utc;p.life=life;p.kind=kind;p.radius=r;p.air=air;p.speed=(kind==1?8.f:3.f)*r;
            p.attitude=root_->quaternion;p.attitude.multiply(e.pivot->quaternion);Vector3 exit(0,e.exitY,0);exit.applyQuaternion(e.pivot->quaternion).add(e.pivot->position).applyQuaternion(root_->quaternion);
            p.origin=position+physical(exit)+velocity*(born-utc);p.velocity=velocity;p.flow=relativeFlow;};
        if(cycle.ignition&&air>.05f)release(e.gas[0],cycle.started,.32f,0); // Flow attachment pushes a little vapour out of the lip.
        if(cycle.shutdown){release(e.gas[0],utc,.18f,1);for(std::size_t k=1;k<e.gas.size();++k)release(e.gas[k],utc+.14+.26*(k-1),air>.05f?.8f:.45f,2);}
        e.heat+=(on?flow-e.heat:-e.heat)*(1-std::exp(-dt/(on?8.f:45.f))); // Thermal inertia leaves a red lip for roughly 30-60 seconds after a long burn.
        auto& bell=e.bell->uniforms;bell.at("burn").setValue(blackBody(300+1000*e.heat).add(Vector3(1,.3f,.04f).multiplyScalar(.7f*torch+2*flash)));bell.at("lip").setValue(blackBody(300+1350*e.heat));bell.at("emissive").setValue(aftGlow_);
        if(e.hot){e.hot->visible=e.heat>.4f;e.hot->material()->as<MeshBasicMaterial>()->opacity=.35f*std::max(0.f,(e.heat-.4f)/.6f);} // Keep the radiatively cooled vacuum extension warm between burns.
        e.exit->visible=on||flash>0;auto& exit=e.exit->material()->as<ShaderMaterial>()->uniforms;exit.at("strength").setValue((obstruction_>0?1.4f*hit*std::sqrt(crowd):.95f*(1-.9f*vacuum))*throttle+flash);exit.at("time").setValue(time+static_cast<float>(i)*1.73f);exit.at("flash").setValue(4*flash);
        e.exit->position.y=e.plume[0]->position.y-(obstruction_>0?obstruction_:.008f*r);const float splash=obstruction_>0?5+3*hit:1;e.exit->scale.set(splash,1,splash); // Reuse the axial glow where the gas spreads over the obstruction.
        exit.at("core").setValue(obstruction_>0?Vector3(1,.75f,.3f):Vector3(5.f,4.6f,3.8f).lerp(Vector3(1,.7f,.2f),rich));exit.at("edge").setValue(obstruction_>0?Vector3(1,.55f,.15f):Vector3(1,.28f,.19f).lerp(Vector3(1,.48f,.06f),rich));
        for(std::size_t k=0;k<e.plume.size();++k){auto& p=*e.plume[k];const bool core=k==1;
            const float strength=(core?2.8f*(.2f+.8f*air)*std::sqrt(crowd):(.48f*air+.045f+.09f*(1-expansion))*crowd)*throttle*(1+rich*.8f)*(1-.88f*vacuum);p.visible=on;
            const float free=r*flow*(core?(18.f+32.f*expansion)*(1-vacuum)+30.f*vacuum:(48.f+292.f*expansion)*(1-vacuum)+100.f*vacuum),metres=obstruction_>0?obstruction_:free;
            const float nozzle=core?1.f:1.04f,spread=core?1.55f+2.8f*expansion:6.f+41.f*expansion+44.f*expansion*expansion; // Width grows faster than length as the ambient confinement falls.
            const float width=r*(.88f+.12f*flow); // Chamber pressure changes brightness and reach, not the bell's exit diameter.
            p.scale.set(width,metres,width);
            auto& u=p.material()->as<ShaderMaterial>()->uniforms;u.at("strength").setValue(strength);u.at("time").setValue(time+static_cast<float>(i)*1.73f);
            u.at("radii").setValue(Vector2(nozzle,nozzle+(spread-nozzle)*std::min(1.f,metres/std::max(free,.001f))));u.at("metres").setValue(metres);
            u.at("core").setValue((core?Vector3(5.f,4.6f,3.8f):Vector3(1.8f-.9f*expansion,.65f-.12f*expansion,.3f+.9f*expansion)).lerp(Vector3(1,.72f,.18f),rich));u.at("edge").setValue((core?Vector3(2.8f,1.6f,.65f):Vector3(1.5f-expansion,.28f+.1f*expansion,.07f+1.13f*expansion)).lerp(Vector3(1,.36f,.03f),rich));
            const float chamber=std::clamp((throttle-.12f)/.25f,0.f,1.f)*std::clamp((mainAge-.12f)/.25f,0.f,1.f);u.at("diamonds").setValue(diamonds*chamber*chamber*(3-2*chamber));u.at("inner").setValue(core?1.f:0.f);u.at("spacing").setValue(r*(2.4f+1.2f*flow));u.at("falloff").setValue(core?2.6f:1.2f-.85f*expansion);u.at("rarefied").setValue(expansion);u.at("vacuum").setValue(vacuum);u.at("impingement").setValue(hit);u.at("flash").setValue(flash);}
        const float offAge=static_cast<float>(utc-e.releasedUtc);e.detached->visible=e.remnantLife>0&&offAge>=0&&offAge<e.remnantLife;
        if(e.detached->visible){const float fade=1-offAge/e.remnantLife; // Gas leaves the bell and loses its feed, rather than switching off with thrust.
            const double carried=(1-std::exp(-offAge/(.15+2*(1-air))))*(.15+2*(1-air));
            e.detached->position.copy(local(e.released+e.inherited*offAge-e.flow*(offAge-carried)+e.velocity*offAge));e.detached->quaternion.copy(inverse).multiply(e.attitude);
            for(std::size_t k=0;k<e.remnant.size();++k){auto& p=*e.remnant[k];
                auto& u=p.material()->as<ShaderMaterial>()->uniforms;const float metres=u.at("metres").value<float>();
                const float width=e.remnantWidth*(1+.8f*offAge);p.scale.set(width,metres*(.2f+.8f*fade),width);
                u.at("strength").setValue(e.remnantStrength[k]*fade*fade);u.at("time").setValue(time+static_cast<float>(i)*1.73f);}}
        for(auto& p:e.gas){const float age=static_cast<float>(utc-p.born);p.mesh->visible=p.life>0&&age>=0&&age<p.life;if(!p.mesh->visible)continue;
            if(p.pending){p.pending=false;p.attitude=root_->quaternion;p.attitude.multiply(e.pivot->quaternion); // Later parcels still come from the moving bell, then drift freely.
                Vector3 exit(0,e.exitY,0);exit.applyQuaternion(e.pivot->quaternion).add(e.pivot->position).applyQuaternion(root_->quaternion);
                p.origin=position+physical(exit)-velocity*age;p.velocity=velocity;p.flow=relativeFlow;}
            const float t=age/p.life,expand=p.kind==1?1.5f+10*age:.6f+(p.air>.05f?4.f:24.f)*age;
            Vector3 out(0,-p.speed*age,0);out.applyQuaternion(p.attitude);const double tau=.18+2*(1-p.air),carried=tau*(1-std::exp(-age/tau));
            p.mesh->position.copy(local(p.origin+p.velocity*age-p.flow*(age-carried)+physical(out)));p.mesh->quaternion.copy(inverse).multiply(p.attitude);
            p.mesh->scale.set(p.radius*expand,p.radius*expand*(p.kind==1?1.4f:.75f),p.radius*expand);
            auto& u=p.mesh->material()->as<ShaderMaterial>()->uniforms;const float density=(p.kind==1?1.2f:(p.air>.05f?.5f:.045f))*(1-t)*(1-t);
            u.at("density").setValue(density);u.at("time").setValue(age);u.at("seed").setValue(static_cast<float>(i)*7.13f);u.at("flame").setValue(p.kind==1?1.f:0.f);}
    }
    for(auto& p:parts_){const float d=p.surface>0&&p.surface<=surfaces?at(engines*3+p.surface-1):0.f;
        Quaternion turn;turn.setFromAxisAngle(p.axis,static_cast<float>(p.gain*d+p.offset));p.pivot->quaternion.copy(turn).multiply(p.base);}
    if(exhaustLight_){exhaustLight_->intensity=450*lightFlow*air;if(lightFlow>0)exhaustLight_->position.copy(lightCentre).multiplyScalar(1/lightFlow);}
    for(std::size_t i=0;i<jets_.size();++i){const float duty=at(engines*3+surfaces+i);auto& j=*jets_[i].mesh;j.visible=duty>.02f;
        j.scale.set(1,.4f+.6f*duty,1);j.material()->as<MeshBasicMaterial>()->opacity=.55f*std::min(1.f,duty*2)*(.3f+.7f*thin);}
}
}
