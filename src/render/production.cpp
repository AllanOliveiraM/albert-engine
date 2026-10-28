#include "production.hpp"
#include "lighting.hpp"
#include "threepp/extras/ShapeUtils.hpp"
#include "threepp/utils/BufferGeometryUtils.hpp"
#include <algorithm>
#include <cmath>
using namespace threepp;
namespace albert {
namespace {
constexpr float pi=3.14159265358979f;
using Pieces=std::vector<std::shared_ptr<BufferGeometry>>;
using Style=core::SceneryBuilding::Style;
struct Surface {
    std::vector<float> positions,colours;
    void triangle(Vector3 a,Vector3 b,Vector3 c,std::uint32_t colour){const Color ink(colour);
        for(const auto& p:{a,b,c}){positions.insert(positions.end(),{p.x,p.y,p.z});colours.insert(colours.end(),{ink.r,ink.g,ink.b});}}
    void quad(Vector3 a,Vector3 b,Vector3 c,Vector3 d,std::uint32_t colour){triangle(a,b,c,colour);triangle(a,c,d,colour);}
    void flush(Pieces& pieces){if(positions.empty())return;auto g=BufferGeometry::create();g->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));
        g->setAttribute("color",FloatBufferAttribute::create(std::move(colours),3));g->computeVertexNormals();pieces.push_back(g);}
};
// Non-indexed position/normal/colour throughout, so curved shells, roofs and bars merge once.
void piece(Pieces& out,std::shared_ptr<BufferGeometry> g,const Matrix4& frame,Vector3 at,std::uint32_t colour){
    if(g->hasIndex())g=g->toNonIndexed();
    g->deleteAttribute("uv");g->clearGroups();g->translate(at.x,at.y,at.z);g->applyMatrix4(frame);
    const Color c(colour);g->setAttribute("color",FloatBufferAttribute::create(std::vector<float>(g->getAttribute<float>("position")->count()*3),3));
    auto* ink=g->getAttribute<float>("color");for(int i=0;i<ink->count();++i)ink->setXYZ(i,c.r,c.g,c.b);out.push_back(g);
}
void building(const core::SceneryBuilding& b,bool high,std::array<Pieces,3>& out){
    const float sn=std::sin(b.heading),cs=std::cos(b.heading),w=b.size.x,d=b.size.y,h=b.size.z;
    const Vector3 centre(float(b.centre.x),float(b.centre.y),float(b.centre.z));Matrix4 frame;frame.makeBasis({cs,-sn,0},{sn,cs,0},{0,0,1});frame.setPosition(centre);
    auto box=[&](Vector3 at,Vector3 size,std::uint32_t colour,int group=0){piece(out[group],BoxGeometry::create(size.x,size.y,size.z),frame,at,colour);};
    auto cylinder=[&](float bottom,float top,float height,float z,std::uint32_t colour,int group=1){auto g=CylinderGeometry::create(top,bottom,height,high?32:12);g->rotateX(pi/2);piece(out[group],g,frame,{0,0,z},colour);};
    constexpr std::uint32_t steel=0xb7bfc3,glass=0x263a48,door=0x293238,edge=0x899399,white=0xe5e6e3;
    if(b.style==Style::Ship||b.style==Style::Booster||b.style==Style::Nosecone){
        const bool ship=b.style==Style::Ship,nose=b.style==Style::Nosecone;const float r=4.5f,cone=ship?13.f:nose?h*.8f:1.f,body=h-cone;
        cylinder(r,r,body,body/2,steel);cylinder(r,ship||nose?.12f:4.1f,cone,body+cone/2,steel);
        box({0,0,.4f},{11,11,.8f},0x8e8c86);
        if(ship){ // The garden ship's dark heat shield and the four projecting fins.
            auto g=CylinderGeometry::create(r+.025f,r+.025f,body,high?16:8,1,true,0,pi);g->rotateX(pi/2);piece(out[1],g,frame,{0,0,body/2},0x283033);
            Surface fins;for(float side:{-1.f,1.f})for(float z:{5.f,body+3.f}){const float span=z<10?5.f:2.6f;
                const Vector3 a(side*4.2f,0,z),c(side*(4.2f+span),0,z+2),e(side*4.2f,0,z+8);
                fins.triangle(a,c,e,steel);fins.triangle(e,c,a,steel);} // Flat fins at this range; both sides shade correctly.
            Pieces local;fins.flush(local);for(auto& g:local){g->applyMatrix4(frame);out[1].push_back(g);}}
        if(high)for(float z=3;z<body;z+=1.8f)cylinder(r+.035f,r+.035f,.045f,z,0x7e898f,2);
        return;
    }
    if(b.style==Style::Tank){const float r=(w+d)/4;cylinder(r,r,h,h/2,0xc4c8c6);cylinder(r,r*.94f,.5f,h+.25f,white);
        if(high){cylinder(r+.04f,r+.04f,.16f,h*.25f,edge,2);cylinder(r+.04f,r+.04f,.16f,h*.75f,edge,2);}return;}
    if(b.rings.empty())return;
    std::vector<Vector2> contour;std::vector<std::vector<Vector2>> holes;
    auto ring=[&](const auto& source){std::vector<Vector2> r;for(const auto& p:source)r.emplace_back(float(p.x-b.centre.x),float(p.y-b.centre.y));return r;};
    contour=ring(b.rings[0]);for(std::size_t i=1;i<b.rings.size();++i)holes.push_back(ring(b.rings[i]));
    const auto triangles=shapeutils::triangulateShape(contour,holes);std::vector<Vector2> vertices=contour;for(const auto& r:holes)vertices.insert(vertices.end(),r.begin(),r.end());
    const bool factory=b.style==Style::Factory,bay=b.style==Style::Bay,house=b.style==Style::House;
    constexpr std::uint32_t roofs[]{0x9b9b94,0xbcbcb4,0x8b8880,0xa7aaa8};const auto roof=roofs[b.osm%4];
    Surface walls,windows,trim;auto world=[&](Vector2 p,float z){return Vector3(p.x+centre.x,p.y+centre.y,z+centre.z);};
    for(const auto& t:triangles){auto a=vertices[t[0]],c=vertices[t[1]],e=vertices[t[2]];if((c.x-a.x)*(e.y-a.y)-(c.y-a.y)*(e.x-a.x)<0)std::swap(c,e);
        walls.triangle(world(a,h),world(c,h),world(e,h),factory?0xa2a8ab:bay?0xc0c4c4:roof);}
    auto sides=[&](std::vector<Vector2> r,bool hole){if(shapeutils::isClockWise(r)!=hole)std::reverse(r.begin(),r.end());
        for(std::size_t i=0;i<r.size();++i){const auto a=r[i],e=r[(i+1)%r.size()];const float length=std::hypot(e.x-a.x,e.y-a.y);if(length<.01f)continue;
            const Vector2 tangent((e.x-a.x)/length,(e.y-a.y)/length),normal(tangent.y,-tangent.x);
            auto p=[&](float x,float z,float offset=0.f){return world({a.x+tangent.x*x+normal.x*offset,a.y+tangent.y*x+normal.y*offset},z);};
            auto face=[&](Surface& into,float left,float right,float bottom,float top,float offset,std::uint32_t ink){into.quad(p(left,bottom,offset),p(right,bottom,offset),p(right,top,offset),p(left,top,offset),ink);};
            const std::uint32_t wall=bay?0xb0b8bd:house?0xd2cec0:0xc3c6c5;
            face(walls,0,length,-1,h,0,wall);
            if(factory){face(windows,0,length,.9f,h-2.f,.035f,glass);face(walls,0,length,h-2.f,h,.05f,0x727d84);}
            if(bay&&length>25){ // Broad dark rolling doors survive the distant/low LOD.
                const float dw=std::min(length-8.f, b.name=="Gigabay"?length*.72f:27.f),left=(length-dw)/2,top=h*.76f;
                face(walls,left,left+dw,.2f,top,.04f,door);face(walls,left-.8f,left,0,top+1.5f,.08f,edge);face(walls,left+dw,left+dw+.8f,0,top+1.5f,.08f,edge);
                if(high)for(float z=4;z<top;z+=4)face(trim,left,left+dw,z,z+.10f,.10f,0x5b656d);}
            if(high){
                if(bay)for(float z=5;z<h;z+=5)face(trim,0,length,z,z+.16f,.13f,0x87939c);
                if(factory){for(float x=3;x<length;x+=6)face(trim,x,x+.10f,1,h-2.f,.08f,0x89969c);
                    for(float z=5;z<h-2.f;z+=5)face(trim,0,length,z,z+.10f,.09f,0x89969c);}
                if(!factory&&!bay&&length>3){const float step=house?4.5f:5.5f;for(float z=1.3f;z<h-1;z+=3.3f)for(float x=1.1f;x<length-2;x+=step)face(windows,x,x+1.6f,z,z+1.2f,.025f,glass);}
            }
        }};
    sides(contour,false);for(const auto& r:holes)sides(r,true);
    if(factory&&w*d>10000){ // White X on a real, east-facing perimeter edge, never the bounding rectangle.
        Vector2 a,e;float longest=0;
        auto r=contour;if(shapeutils::isClockWise(r))std::reverse(r.begin(),r.end());
        for(std::size_t i=0;i<r.size();++i){auto p=r[i],q=r[(i+1)%r.size()];const float l=std::hypot(q.x-p.x,q.y-p.y);
            if(l>longest&&(q.y-p.y)*-centre.x-(q.x-p.x)*-centre.y>0){a=p;e=q;longest=l;}}
        if(longest>20){const Vector2 tangent((e.x-a.x)/longest,(e.y-a.y)/longest),normal(tangent.y,-tangent.x);const auto c=Vector2(a).add(e).multiplyScalar(.5f);
            auto p=[&](float x,float z){return world({c.x+tangent.x*x+normal.x*.16f,c.y+tangent.y*x+normal.y*.16f},z);};
            for(float side:{-1.f,1.f})walls.quad(p(-6.f,8.f-side*4.f-.5f),p(6.f,8.f+side*4.f-.5f),p(6.f,8.f+side*4.f+.5f),p(-6.f,8.f-side*4.f+.5f),white);
        }}
    walls.flush(out[0]);windows.flush(out[1]);trim.flush(out[2]);
    if(high&&house&&contour.size()==4&&b.name!="static_caravan"){ // Gables only on rectangular homes; complex halls keep their polygon roof.
        const float area=std::abs(shapeutils::area(contour));if(std::abs(area-w*d)<.03f*area){Surface roofMesh;
            const Vector3 a(-w/2,-d/2,h),c(w/2,-d/2,h),e(w/2,d/2,h),f(-w/2,d/2,h),r(0,-d/2,h+1.4f),s(0,d/2,h+1.4f);
            roofMesh.quad(a,r,s,f,roof);roofMesh.quad(r,c,e,s,roof);roofMesh.triangle(a,c,r,0xd2cec0);roofMesh.triangle(f,s,e,0xd2cec0);
            Pieces local;roofMesh.flush(local);for(auto& g:local){g->applyMatrix4(frame);out[0].push_back(g);}}}
}
}
ProductionSite::ProductionSite(const std::vector<core::SceneryBuilding>& buildings){
    root_=Object3D::create();root_->name="production site";if(buildings.empty())return;
    for(const auto& b:buildings)centre_.add({float(b.centre.x),float(b.centre.y),float(b.centre.z)});
    centre_.multiplyScalar(1.f/buildings.size());
    for(unsigned level=0;level<2;++level){auto& node=levels_[level];node=Object3D::create();node->name=level?"production near":"production distant";root_->add(node);
        std::array<Pieces,3> groups;for(const auto& b:buildings)building(b,level>0,groups);
        for(unsigned i=0;i<groups.size();++i)if(!groups[i].empty()){auto material=assistedMaterial(Color(0xffffff),i==1?.7f:.15f,i==1?.28f:.78f);material->name="albert:production";
            auto mesh=Mesh::create(mergeBufferGeometries(groups[i]),material);mesh->name=i==0?"production shells":i==1?"production glass and steel":"production fittings";
            mesh->castShadow=mesh->receiveShadow=true;node->add(mesh);}}
    select();
}
void ProductionSite::select(){for(unsigned i=0;i<2;++i)if(levels_[i])levels_[i]->visible=(i==unsigned(high_&&quality_>0));}
void ProductionSite::quality(int value){quality_=value;select();}
void ProductionSite::view(Camera& camera){if(!levels_[0])return;
    root_->updateWorldMatrix(true,false);Vector3 eye;camera.getWorldPosition(eye);Matrix4 inverse;inverse.copy(*root_->matrixWorld).invert();eye.applyMatrix4(inverse);
    // Hysteresis prevents switching repeatedly as a camera crosses the threshold.
    const float distance=eye.distanceTo(centre_);if(distance>(high_?2200.f:1800.f))high_=false;else if(distance<1800.f)high_=true;select();
}
}
