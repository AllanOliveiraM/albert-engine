#include "scenery.hpp"
#include "lighting.hpp"
#include "industrial.hpp"
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
void tint(const std::shared_ptr<BufferGeometry>& g,std::uint32_t colour){
    const Color c(colour);std::vector<float> colors;
    for(int i=0;i<g->getAttribute<float>("position")->count();++i)colors.insert(colors.end(),{c.r,c.g,c.b});
    g->setAttribute("color",FloatBufferAttribute::create(std::move(colors),3));
}
// Named scenery markers describe drawn steel only. All coordinates remain in the marker's frame until Site puts the
// merged pieces under its original structure pivot. The optional fittings use the very same transform and carrier.
bool towerGeometry(Pieces& solid,Pieces& fine,const core::SceneryPart& p){
    const float w=float(p.size.x),d=float(p.size.y),l=float(p.size.z);const auto& name=p.name;
    auto add=[&](std::shared_ptr<BufferGeometry> g,std::uint32_t colour,bool detail){tint(g,colour);(detail?fine:solid).push_back(g);};
    auto box=[&](Vector3 at,Vector3 size,std::uint32_t colour,bool detail=false){auto g=BoxGeometry::create(size.x,size.y,size.z);g->translate(at.x,at.y,at.z);add(g,colour,detail);};
    auto beam=[&](Vector3 a,Vector3 b,float thickness,std::uint32_t colour,bool detail=false){Pieces pieces;bar(pieces,a,b,thickness);for(auto& g:pieces)add(g,colour,detail);};
    constexpr std::uint32_t black=0x252b30,edge=0x606a72,rubber=0x171c20,steel=0xa3abb0;
    if(name=="launch tower lattice"){
        const float x=w/2-.36f,y=d/2-.36f,bay=l/16;
        for(float sx:{-1.f,1.f})for(float sy:{-1.f,1.f}){
            box({sx*x,sy*y,0},{.72f,.72f,l},p.color);
            // Flange edges and splice collars catch the sun without filling the open bays.
            for(float z=-l/2+bay;z<l/2-.1f;z+=bay)box({sx*x,sy*y,z},{.84f,.84f,.24f},edge,true);
        }
        for(int face=0;face<4;++face){auto at=[&](float across,float z,float offset=0.f){return face<2?Vector3(across,(face==0?-y:y)+offset,z):Vector3((face==2?-x:x)+offset,across,z);};
            for(int j=0;j<=16;++j){const float z=-l/2+std::clamp(j*bay,.24f,l-.27f);beam(at(-x,z),at(x,z),.46f,p.color);}
            for(int j=0;j<16;++j){const float bottom=-l/2+j*bay+.44f,top=-l/2+(j+1)*bay-.44f;
                beam(at(-x+.14f,bottom,-.02f),at(x-.14f,top,-.02f),.28f,p.color);
                beam(at(x-.14f,bottom,.02f),at(-x+.14f,top,.02f),.28f,p.color);
                for(float side:{-1.f,1.f})for(float z:{bottom,top})box(at(side*(x-.25f),z),face<2?Vector3(.65f,.12f,.55f):Vector3(.12f,.65f,.55f),edge,true);
            }
        }
    }else if(name=="tower cladding panels"){
        const float x=w/2,y=d/2;
        for(float side:{-1.f,1.f}){
            box({side*(x-.06f),0,0},{.12f,d-.24f,l},p.color);box({0,side*(y-.06f),0},{w-.24f,.12f,l},p.color);
            for(int i=-3;i<=3;++i){box({side*(x+.025f),i*1.35f,0},{.08f,.055f,l},edge,true);box({i*1.35f,side*(y+.025f),0},{.055f,.08f,l},edge,true);}
            for(float z=-l/2+3.f;z<l/2;z+=3.f){box({side*(x+.015f),0,z},{.045f,d-.24f,.045f},edge,true);box({0,side*(y+.015f),z},{w-.24f,.045f,.045f},edge,true);}
        }
    }else if(name=="tower crane crown"){
        const float x=w/2,y=d/2;
        for(float side:{-1.f,1.f}){
            for(float z:{.15f,4.2f}){beam({-x,side*y,z},{x,side*y,z},.4f,black);beam({side*x,-y,z},{side*x,y,z},.4f,black);}
            for(float end:{-1.f,1.f})beam({side*x,end*y,0},{side*x,end*y,4.2f},.48f,black);
            beam({-x,side*y,0},{x,side*y,4.2f},.22f,black);
            beam({-3,side*.85f,4.6f},{17,side*.55f,4.6f},.24f,black);
            beam({-3,side*.85f,6.3f},{17,side*.55f,5.5f},.24f,black);
            for(int i=0;i<8;++i){const float a=-3+i*2.5f,b=a+2.5f,t=float(i)/8,u=float(i+1)/8;
                beam({a,side*(.85f-.3f*t),4.6f},{b,side*(.85f-.3f*u),6.3f-.8f*u},.14f,black);
                beam({a,side*(.85f-.3f*t),6.3f-.8f*t},{a,side*(.85f-.3f*t),4.6f},.14f,black);}
            beam({-3,side*1.1f,4.2f},{-1,0,11},.22f,black);
            beam({-1,0,11},{16,side*.55f,5.55f},.065f,edge,true);
            beam({-1,0,11},{-7,side*.6f,4.6f},.065f,edge,true);
            box({-5,side*1.3f,3.8f},{4,1.2f,1.4f},edge);
            beam({16,side*.55f,4.5f},{16,side*.55f,-.8f},.065f,edge,true);
        }
        beam({0,0,.5f},{0,0,17.5f},.24f,steel);beam({-3,-3,3},{0,0,10},.07f,edge,true);beam({3,3,3},{0,0,10},.07f,edge,true);
        box({16,0,-.8f},{.7f,1.4f,.6f},black,true);
        for(float side:{-1.f,1.f})for(int i=0;i<=8;++i)box({-x+i*w/8,side*y,4.9f},{.08f,.08f,1.3f},steel,true);
        for(float side:{-1.f,1.f})beam({-x,side*y,5.55f},{x,side*y,5.55f},.07f,steel,true);
    }else if(name=="catch carriage frame"){
        const float x=w/2,y=d/2,z=l/2;
        for(float side:{-1.f,1.f}){
            for(float h:{-z,z}){beam({-x,side*y,h},{x,side*y,h},.5f,black);beam({side*x,-y,h},{side*x,y,h},.5f,black);}
            for(float end:{-1.f,1.f}){beam({side*x,end*y,-z},{side*x,end*y,z},.65f,black);
                box({side*(x-.25f),end*3.05f,0},{.85f,1.3f,l+1.2f},edge);}
            beam({-x,side*y,-z},{x,side*y,z},.35f,black);beam({side*x,-y,-z},{side*x,y,z},.35f,black);
            for(float end:{-1.f,1.f})for(float h:{-z+.4f,z-.4f})box({side*(x+.12f),end*3.05f,h},{.4f,.9f,.9f},steel,true);
        }
        box({x,0,0},{1,4.4f,2.2f},black); // Crosshead between the two original arm roots.
    }else if(name=="tapered catch truss"){
        const int bays=8;auto at=[&](int side,int bottom,int k){const float t=float(k)/bays;return Vector3(side*(w/2-.12f)*(1-.58f*t),-d/2+.12f+bottom*(d-.24f)*(1-.65f*t),-l/2+l*t);};
        for(int side:{-1,1}){
            for(int bottom:{0,1})beam(at(side,bottom,0),at(side,bottom,bays),.24f,black);
            for(int k=0;k<bays;++k){beam(at(side,0,k),at(side,1,k),.17f,black);beam(at(side,k%2,k),at(side,1-k%2,k+1),.17f,black);}
            beam(at(side,0,bays),at(side,1,bays),.17f,black);
        }
        for(int bottom:{0,1})for(int k=0;k<=bays;++k){beam(at(-1,bottom,k),at(1,bottom,k),.16f,black);
            if(k<bays)beam(at(-1,bottom,k),at(1,bottom,k+1),.10f,edge,true);}
        for(int side:{-1,1})for(int k=0;k<=bays;++k)box(at(side,0,k),{.3f,.12f,.48f},edge,true);
    }else if(name=="catch arm walkway"){
        for(float side:{-1.f,1.f}){beam({side*w*.4f,0,-l/2},{side*w*.17f,0,l/2},.18f,edge);
            for(int i=0;i<=40;++i){const float t=float(i)/40,z=-l/2+l*t,x=side*w*(.4f-.23f*t);box({x,0,z},{.38f,.12f,.075f},steel,true);}}
    }else if(name=="catch inner rail bed"){
        box({0,0,0},{w,d,l},edge);
        for(float z=-l/2+.5f;z<l/2;z+=2){box({0,-.25f,z},{1.05f,.3f,.65f},rubber,true);box({0,.25f,z},{.65f,.3f,.85f},black,true);}
    }else if(name=="ship QD truss"||name=="booster QD truss hood"){
        Pieces pieces;truss(pieces,w,d,l,.17f);for(auto& g:pieces)add(g,black,false);
        if(name=="booster QD truss hood")box({0,0,l/2-.15f},{w,d,.3f},edge); // The connector shield survives, its support stays open.
        for(float side:{-1.f,1.f})for(float z:{-l/2+.25f,l/2-.25f})box({side*w/2,0,z},{.12f,d,.35f},edge,true);
    }else return false;
    return true;
}
std::shared_ptr<BufferGeometry> pier(float w,float d,float h){ // Splayed on all faces; small edge bevels catch the sun.
    std::vector<Vector3> vertices;const float bevel=.12f;
    for(int ring=0;ring<4;++ring){const float inset=ring==0||ring==3?bevel:0,scale=ring<2?1.f:.58f;
        const float x=w*.5f*scale-inset,y=d*.5f*scale-inset,c=.28f;
        const float z=ring==0?-h/2:ring==1?-h/2+bevel:ring==2?h/2-bevel:h/2;
        for(const auto& v:std::array<Vector3,8>{{{-x+c,-y,z},{x-c,-y,z},{x,-y+c,z},{x,y-c,z},{x-c,y,z},{-x+c,y,z},{-x,y-c,z},{-x,-y+c,z}}})vertices.push_back(v);}
    std::vector<float> positions;std::vector<unsigned> indices;
    auto triangle=[&](int a,int b,int c){for(int i:{a,b,c}){const auto& v=vertices[i];indices.push_back(static_cast<unsigned>(indices.size()));positions.insert(positions.end(),{v.x,v.y,v.z});}};
    for(int r=0;r<3;++r)for(int i=0;i<8;++i){const int a=r*8+i,b=r*8+(i+1)%8;triangle(a,b,b+8);triangle(a,b+8,a+8);}
    for(int i=1;i<7;++i){triangle(0,i+1,i);triangle(24,24+i,25+i);}
    auto g=BufferGeometry::create();g->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));g->setIndex(indices);g->computeVertexNormals();
    g->setAttribute("uv",FloatBufferAttribute::create(std::vector<float>(indices.size()*2,0),2));return g;
}
bool concretePart(const std::string& n){return n.find("apron")!=std::string::npos||n=="mount leg"||n=="trench floor"||n=="trench wall"||n=="raised pad terrace"||n=="flame bucket ramp"||n=="tower concrete plinth"||n=="tower service building"||n=="perimeter T wall"||n=="floodlight pole footing"||n=="tank saddle";}
void furnishing(Pieces& out,const core::SceneryPart& p,const Matrix4& m){
    auto add=[&](std::shared_ptr<BufferGeometry> g,std::uint32_t color,Vector3 at){g->translate(at.x,at.y,at.z);g->applyMatrix4(m);const Color c(color);std::vector<float> colors;
        for(int i=0;i<g->getAttribute<float>("position")->count();++i)colors.insert(colors.end(),{c.r,c.g,c.b});
        g->setAttribute("color",FloatBufferAttribute::create(std::move(colors),3));out.push_back(g);};
    auto box=[&](float x,float y,float z,float w,float d,float h,std::uint32_t color){add(BoxGeometry::create(w,d,h),color,{x,y,z});};
    constexpr std::uint32_t rubber=0x242c30,glass=0x344e60,steel=0x858d91;
    if(p.name=="detail cone"){
        box(0,0,.035f,.48f,.48f,.07f,rubber);auto g=CylinderGeometry::create(.045f,.20f,.62f,8);g->rotateX(pi/2);add(g,0xf37924,{0,0,.38f});
        auto band=CylinderGeometry::create(.107f,.145f,.15f,8);band->rotateX(pi/2);add(band,0xe4e4e2,{0,0,.38f});
    }else if(p.name=="detail cable tray"){
        for(float x:{-.38f,.38f})box(x,0,.22f,.09f,8,.4f,steel);
        for(int i=-7;i<=7;++i)box(0,i*.5f,.08f,.7f,.07f,.1f,steel);
    }else if(p.name=="detail skid"){
        box(0,0,.12f,2.5f,4,.24f,steel);box(-.65f,0,.8f,.9f,2.5f,1.3f,p.color);box(.5f,.3f,.6f,1,1.8f,.9f,rubber);
        for(float y:{-1.4f,1.4f})box(0,y,.5f,2,.15f,.6f,steel);
    }else{
        const bool truck=p.name=="detail truck",car=p.name=="detail car";const float length=truck?7.f:car?4.5f:5.5f,width=truck?2.5f:2.f;
        box(0,0,.68f,width,length,.65f,p.color);box(0,length*.17f,1.25f,width-.12f,car?2.2f:1.9f,.75f,glass);
        box(0,length*.17f,1.67f,width,car?2.4f:2.1f,.14f,p.color);
        for(float x:{-.5f,.5f})box(x*width,length*.17f,1.3f,.12f,1.95f,.8f,p.color);
        if(truck)box(0,-1.4f,1.45f,2.4f,3.6f,1.25f,steel);
        else if(!car){box(0,-1.55f,1.03f,1.65f,1.7f,.12f,rubber);for(float x:{-.94f,.94f})box(x,-1.55f,1.12f,.12f,1.85f,.45f,p.color);}
        for(float y:{-length*.32f,length*.31f})for(float x:{-width*.5f,width*.5f}){auto g=CylinderGeometry::create(truck?.48f:.36f,truck?.48f:.36f,.22f,10);g->rotateZ(pi/2);add(g,rubber,{x,y,truck?.48f:.36f});}
        for(float x:{-.65f,.65f})box(x,length*.5f+.015f,.78f,.35f,.035f,.18f,0xe4e4e2);
    }
}
}
Site::Site(const core::Scenery& s){
    root_=Mesh::create(BufferGeometry::create());root_->frustumCulled=false;
    if(!s.buildings.empty()){production_=std::make_unique<ProductionSite>(s.buildings);root_->add(production_->node());}
    const std::size_t n=s.bars.size();hinges_.resize(n);
    for(std::size_t k=0;k<n;++k)if(s.bars[k].hinged){auto& h=hinges_[k];h.pivot=Object3D::create();h.pivot->position.copy(vec(s.bars[k].pivot));h.axis=vec(s.bars[k].axis).normalize();root_->add(h.pivot);}
    // By what carries them (0: the ground, k: the k-th structure's pivot) and colour, about that pivot.
    std::map<std::pair<std::size_t,std::uint32_t>,Pieces> by;Pieces details,industrialPaint,industrialMetal,industrialFine;constexpr std::uint32_t steel=0x1000000,concrete=0x1000001,beacon=0x1000002; // Past any colour: shared materials.
    std::map<std::size_t,Pieces> towerSteel,towerFine;
    std::array<Vector4,2> pads{Vector4(0,0,0,1),Vector4(1e6f,1e6f,0.f,1.f)};unsigned pad=0;
    for(const auto& p:s.parts)if(p.name=="trench floor"&&pad<2)pads[pad++]=Vector4(float(p.centre.x),float(p.centre.y),float(std::sin(p.heading)),float(std::cos(p.heading)));
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
        Matrix4 m;m.makeBasis(across,y,z);m.setPosition(vec(p.centre));
        const auto np=industrialPaint.size(),nm=industrialMetal.size(),nf=industrialFine.size();
        if(industrialGeometry(industrialPaint,industrialMetal,industrialFine,p)){
            for(std::size_t j=np;j<industrialPaint.size();++j)industrialPaint[j]->applyMatrix4(m);
            for(std::size_t j=nm;j<industrialMetal.size();++j)industrialMetal[j]->applyMatrix4(m);
            for(std::size_t j=nf;j<industrialFine.size();++j)industrialFine[j]->applyMatrix4(m);
            continue;
        }
        if(p.name.starts_with("detail ")){furnishing(details,p,m);continue;}
        const std::size_t k=carrier(p.structure);Pieces fine;
        if(towerGeometry(local,fine,p)){
            auto collect=[&](Pieces& from,Pieces& into){for(auto& g:from){g->applyMatrix4(m);into.push_back(relative(k,std::move(g)));}};
            collect(local,towerSteel[k]);collect(fine,towerFine[k]);continue;
        }
        switch(p.shape){
        case core::SceneryPart::Shape::Box:
            if(p.name=="mount leg")local.push_back(pier(w,d,l));
            else if(p.name=="perimeter T wall"){auto foot=BoxGeometry::create(w,d,.3f);foot->translate(0,0,-l/2+.15f);local.push_back(foot);auto stem=BoxGeometry::create(.28f,d,l-.3f);stem->translate(0,0,.15f);local.push_back(stem);}
            else local.push_back(BoxGeometry::create(w,d,l));
            break;
        case core::SceneryPart::Shape::Cylinder:{ // Built round (radius 1) along y, then laid along z and stretched to its diameters.
            const float k=static_cast<float>(p.inner)/w;
            if(k<=0)local.push_back(CylinderGeometry::create(1,1,l,48));
            else{local.push_back(CylinderGeometry::create(1,1,l,48,1,true));local.push_back(inside(k,l));
                for(const float side:{1.f,-1.f}){auto r=RingGeometry::create(k,1,48);r->rotateX(-side*pi/2);r->translate(0,side*l/2,0);local.push_back(r);}}
            for(auto& g:local){g->rotateX(pi/2);g->scale(w/2,d/2,1);}break;}
        case core::SceneryPart::Shape::Truss:truss(local,w,d,l,p.bar>0?static_cast<float>(p.bar):.08f*std::min(w,d));break;
        }
        for(auto& g:local){g->applyMatrix4(m);by[{k,concretePart(p.name)?concrete:p.name=="tower aviation beacon"?beacon:p.color}].push_back(relative(k,std::move(g)));}
    }
    for(std::size_t i=0;i<n;++i){const auto& b=s.bars[i];const Vector3 a=vec(b.from),e=vec(b.to);Vector3 dir=Vector3(e).sub(a);const float l=dir.length();if(l<1e-4f||!(b.radius>0))continue; // Round steel bars (radius 0: not drawn).
        auto g=CylinderGeometry::create(static_cast<float>(b.radius),static_cast<float>(b.radius),l,24);Quaternion q;q.setFromUnitVectors(Vector3(0,1,0),dir.normalize());
        Matrix4 m;m.compose(Vector3(a).add(e).multiplyScalar(.5f),q,Vector3(1,1,1));g->applyMatrix4(m);const std::size_t k=carrier(static_cast<unsigned>(i+1));by[{k,steel}].push_back(relative(k,std::move(g)));}
    for(const auto& [key,pieces]:by){
        auto material=key.second==steel?assistedMaterial(Color(0x9c9c9a),.8f,.35f):assistedMaterial(Color(key.second),.25f,.7f);
        if(key.second==concrete){concrete_=assistedMaterial(Color(0xffffff),0,.92f);concrete_->name="albert:concrete";concrete_->uniforms["padOne"].setValue(pads[0]);concrete_->uniforms["padTwo"].setValue(pads[1]);concrete_->uniforms["concreteQuality"].setValue(Vector4(1,0,0,0));material=concrete_;}
        if(key.second==beacon){material=assistedMaterial(Color(0x9a1005),.1f,.5f);material->uniforms.at("emissive").setValue(Color(0xff1505));}
        auto x=Mesh::create(mergeBufferGeometries(pieces),material);
        x->castShadow=x->receiveShadow=key.second!=beacon;if(key.second==beacon)x->name="tower aviation beacons";(key.first?hinges_[key.first-1].pivot:root_)->add(x);}
    auto towerMaterial=assistedMaterial(Color(0xffffff),.65f,.53f);towerMaterial->name="albert:site-detail";
    auto mergedTower=[&](const auto& groups,bool fine){for(const auto& [k,pieces]:groups)if(!pieces.empty()){
        auto x=Mesh::create(mergeBufferGeometries(pieces),towerMaterial);x->name=fine?"tower fittings":"tower steel";x->castShadow=x->receiveShadow=true;
        (k?hinges_[k-1].pivot:root_)->add(x);if(fine)towerDetails_.push_back(x);}};
    mergedTower(towerSteel,false);mergedTower(towerFine,true);
    if(!details.empty()){auto material=assistedMaterial(Color(0xffffff),.15f,.75f);material->name="albert:site-detail";detail_=Mesh::create(mergeBufferGeometries(details),material);detail_->name="pad furnishings";detail_->castShadow=detail_->receiveShadow=true;root_->add(detail_);}
    auto industrial=[&](const Pieces& pieces,const char* name,float metalness,float roughness){if(pieces.empty())return std::shared_ptr<Mesh>{};
        auto material=assistedMaterial(Color(0xffffff),metalness,roughness);material->name="albert:site-detail";
        auto mesh=Mesh::create(mergeBufferGeometries(pieces),material);mesh->name=name;mesh->castShadow=mesh->receiveShadow=true;root_->add(mesh);return mesh;};
    industrial(industrialPaint,"tank farm shells",.03f,.8f);industrial(industrialMetal,"tank farm steel",.55f,.48f);
    industrialFine_=industrial(industrialFine,"tank farm fittings",.4f,.6f);
    auto active=[&](const Vent& s){return std::none_of(vents_.begin(),vents_.end(),[&](const Vent& v){return Vector3(s.centre).sub(v.centre).lengthSq()<120*120;});};
    std::erase_if(sprays_,active);std::erase_if(pools_,active); // Only water the mount served by the exhaust markers.
    if(!vents_.empty()||!tankVents_.empty()){auto g=SphereGeometry::create(1,20,12);auto pool=[&](auto& puffs){for(auto& p:puffs){
        auto m=ShaderMaterial::create();m->name="albert:steam";m->transparent=true;m->depthTest=true;m->depthWrite=false;m->side=Side::Double;m->blending=Blending::Custom;m->blendSrc=BlendFactor::One;m->blendDst=BlendFactor::OneMinusSrcAlpha;
        m->uniforms={{"density",Uniform(0.f)},{"seed",Uniform(0.f)},{"time",Uniform(0.f)},{"up",Uniform(Vector3(0,0,1))},{"kind",Uniform(0.f)},{"size",Uniform(Vector3(1,1,1))},{"cloud",Uniform(Vector4(0,0,0,0))}};p.mesh=Mesh::create(g,m);p.mesh->visible=false;root_->add(p.mesh);}};
        if(!vents_.empty()){pool(steam_);pool(spray_);}if(!tankVents_.empty())pool(boiloff_);} // Reserve water slots so the launch cannot starve the deluge; 256 shared-geometry shells total.
}
void Site::quality(int value){if(production_)production_->quality(value);if(detail_)detail_->visible=value>0;if(industrialFine_)industrialFine_->visible=value>0;for(auto& x:towerDetails_)x->visible=value>0;if(concrete_)concrete_->uniforms["concreteQuality"].setValue(Vector4(value>0?1.f:0.f,0.f,0.f,0.f));}
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
