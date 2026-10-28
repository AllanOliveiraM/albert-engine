#include "launch_crowd_detail.hpp"
#include "gpu/programs.hpp"
#include "threepp/threepp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
using namespace threepp;
namespace albert {
namespace {
constexpr float pi=3.14159265358979f;
// Position.w selects instance paint/shirt (0), fixed colour (1), skin (2), trousers (3), hair (4).
struct Finish {Color colour;float code=1,rough=.8f,metal=0;};
struct Vertex {Vector3 position,normal;Finish finish;};
using Model=std::vector<Vertex>;
Finish paint(){return {Color(1,1,1),0,.38f,.16f};}
Finish fixed(unsigned colour,float rough=.8f,float metal=0){return {Color(colour),1,rough,metal};}
void triangle(Model& out,Vector3 a,Vector3 b,Vector3 c,Finish finish){const auto n=Vector3(b).sub(a).cross(Vector3(c).sub(a)).normalize();for(const auto& p:{a,b,c})out.push_back({p,n,finish});}
void quad(Model& out,Vector3 a,Vector3 b,Vector3 c,Vector3 d,Finish finish){triangle(out,a,b,c,finish);triangle(out,a,c,d,finish);}
void piece(Model& out,std::shared_ptr<BufferGeometry> geometry,Vector3 at,Vector3 size,Finish finish,const Quaternion& rotation={}){
    if(geometry->hasIndex())geometry=geometry->toNonIndexed();
    const auto* p=geometry->getAttribute<float>("position");const auto* n=geometry->getAttribute<float>("normal");
    for(int i=0;i<p->count();++i){Vector3 v(p->getX(i)*size.x,p->getY(i)*size.y,p->getZ(i)*size.z),normal(n->getX(i)/size.x,n->getY(i)/size.y,n->getZ(i)/size.z);
        v.applyQuaternion(rotation).add(at);normal.normalize().applyQuaternion(rotation);out.push_back({v,normal,finish});}
}
void ellipsoid(Model& out,Vector3 at,Vector3 radius,Finish finish,unsigned sides=6){piece(out,SphereGeometry::create(1,sides,4),at,radius,finish);}
void bar(Model& out,Vector3 a,Vector3 b,float bottom,float top,Finish finish,unsigned sides=6){const Vector3 d=Vector3(b).sub(a);if(d.length()<.001f)return;Quaternion q;q.setFromUnitVectors({0,1,0},Vector3(d).normalize());
    piece(out,CylinderGeometry::create(top,bottom,d.length(),sides,1,false),Vector3(a).add(b).multiplyScalar(.5f),{1,1,1},finish,q);}
void box(Model& out,Vector3 at,Vector3 size,Finish finish){piece(out,BoxGeometry::create(1,1,1),at,size,finish);}
// Rounded rectangles in a plane. Two facets per corner retain a curved silhouette without subdivisions on flat panels.
std::vector<Vector2> rounded(float width,float depth,float radius){std::vector<Vector2> ring;radius=std::min(radius,std::min(width,depth)*.48f);
    for(unsigned corner=0;corner<4;++corner){const float angle=float(corner)*pi/2,cs=std::cos(angle+pi/4),sn=std::sin(angle+pi/4);
        const Vector2 centre(std::copysign(width/2-radius,cs),std::copysign(depth/2-radius,sn));
        for(unsigned j=0;j<3;++j){const float a=angle+float(j)*pi/4;ring.emplace_back(centre.x+radius*std::cos(a),centre.y+radius*std::sin(a));}}
    return ring;
}
void shell(Model& out,const std::vector<std::vector<Vector3>>& rings,Finish finish){
    const unsigned count=rings.front().size();
    for(unsigned k=1;k<rings.size();++k)for(unsigned i=0;i<count;++i){const unsigned j=(i+1)%count;quad(out,rings[k-1][i],rings[k-1][j],rings[k][j],rings[k][i],finish);}
    for(unsigned k:{0u,unsigned(rings.size()-1)}){Vector3 centre;for(const auto& p:rings[k])centre.add(p);centre.divideScalar(float(count));
        for(unsigned i=0;i<count;++i){const unsigned j=(i+1)%count;if(k)triangle(out,centre,rings[k][i],rings[k][j],finish);else triangle(out,centre,rings[k][j],rings[k][i],finish);}}
}
void roundedBox(Model& out,Vector3 at,Vector3 size,float radius,Finish finish){std::vector<std::vector<Vector3>> rings;
    const float bevel=std::min(radius,size.z*.3f);
    for(unsigned k=0;k<4;++k){const bool end=k==0||k==3;const float z=k==0?-size.z/2:k==1?-size.z/2+bevel:k==2?size.z/2-bevel:size.z/2;
        std::vector<Vector3> ring;for(const auto& p:rounded(size.x-(end?bevel*.8f:0),size.y-(end?bevel*.8f:0),radius))ring.emplace_back(at.x+p.x,at.y+p.y,at.z+z);rings.push_back(std::move(ring));}
    shell(out,rings,finish);
}
void cabin(Model& out,float width,float depth,float y,float bottom,float roof,float rake,Finish finish){std::vector<std::vector<Vector3>> rings;
    for(unsigned k=0;k<2;++k){std::vector<Vector3> ring;for(const auto& p:rounded(width*(k?.87f:1.f),depth-(k?rake:0),.12f))ring.emplace_back(p.x,p.y+y-(k?rake*.12f:0),k?roof:bottom);rings.push_back(std::move(ring));}shell(out,rings,finish);
}
void wheel(Model& out,float x,float y,float radius){
    // Tyre shoulders roll into the sidewall. The outer face has a separate inset alloy rim.
    constexpr unsigned sides=10;const float side=x>0?1.f:-1.f;std::vector<std::vector<Vector3>> rings;
    for(unsigned k=0;k<4;++k){const float dx=std::array<float,4>{-.12f,-.075f,.075f,.12f}[k],r=radius*(k==0||k==3?.83f:1.f);std::vector<Vector3> ring;
        for(unsigned i=0;i<sides;++i){const float a=float(i)*2*pi/sides;ring.emplace_back(x+dx,y+std::cos(a)*r,.36f+std::sin(a)*r);}rings.push_back(std::move(ring));}
    shell(out,rings,fixed(0x171a1c,.94f));
    for(unsigned ring=0;ring<2;++ring){const float r=radius*(ring?.19f:.57f),xx=x+side*(ring?.146f:.14f);const unsigned count=ring?6:10;
        for(unsigned i=0;i<count;++i){const float a=float(i)*2*pi/count,b=float(i+1)*2*pi/count;const Vector3 centre(xx,y,.36f),p(xx,y+std::cos(a)*r,.36f+std::sin(a)*r),q(xx,y+std::cos(b)*r,.36f+std::sin(b)*r);
            triangle(out,centre,side>0?p:q,side>0?q:p,ring?fixed(0x42494d,.45f,.5f):fixed(0x879197,.3f,.7f));}}

}
Model car(unsigned kind){Model out;const bool pickup=kind==0||kind==5,suv=kind==1,van=kind==3||kind==4,rv=kind==4;
    const float length=pickup?5.2f:suv?5.5f:van?(rv?8.5f:6.3f):4.65f,width=rv?2.6f:van?2.3f:2.f,roof=rv?3.05f:van?2.45f:suv?1.87f:pickup?1.83f:1.48f;
    const auto glass=fixed(0x173747,.16f,.28f),trim=fixed(0x30383d,.64f),metal=fixed(0x9aa1a4,.3f,.65f);
    roundedBox(out,{0,0,.67f},{width,length,.57f},.17f,paint());
    box(out,{0,0,.38f},{width*.86f,length*.85f,.16f},trim);
    const float depth=pickup?2.40f:suv?3.85f:van?length*.86f:2.45f,cy=pickup?.55f:suv?-.12f:van?-.05f:-.05f,rake=van?.6f:pickup?.48f:suv?.56f:.83f;
    cabin(out,width*.93f,depth,cy,.96f,roof-.08f,rake,van?paint():glass);
    roundedBox(out,{0,cy-rake*.12f,roof-.015f},{width*.83f,depth-rake+.10f,.12f},.10f,paint());
    // Opaque tinted glazing avoids sorting. Separate painted pillars and roof keep the cabin from reading as a black box.
    if(van){const float front=cy+depth/2,upper=front-rake*.62f;
        quad(out,{-width*.40f,front+.008f,1.15f},{width*.40f,front+.008f,1.15f},{width*.35f,upper,roof-.18f},{-width*.35f,upper,roof-.18f},glass);
        for(float side:{-1.f,1.f})quad(out,{side*width*.466f,front-.13f,1.15f},{side*width*.466f,front-1.48f,1.15f},{side*width*.408f,front-1.48f,roof-.19f},{side*width*.408f,upper-.08f,roof-.19f},glass);
    }else{
        for(float side:{-1.f,1.f}){
            for(float end:{-1.f,1.f})bar(out,{side*width*.464f,cy+end*(depth/2-.12f),.97f},{side*width*.409f,cy+end*(depth-rake)/2-rake*.12f,roof-.08f},.045f,.035f,paint(),4);
            bar(out,{side*width*.467f,cy,.97f},{side*width*.407f,cy-rake*.12f,roof-.08f},.036f,.032f,paint(),4);
        }
    }
    for(float side:{-1.f,1.f}){
        const float axle=length*.32f;wheel(out,side*(width/2-.035f),axle,van?.37f:.35f);wheel(out,side*(width/2-.035f),-axle,van?.37f:.35f);
        box(out,{side*(width/2+.055f),cy+depth*.29f,1.14f},{.17f,.28f,.15f},paint());
        box(out,{side*(width*.47f),cy,.94f},{.035f,.22f,.04f},metal);
        const float front=length/2+.012f,rear=-length/2-.012f,cx=side*width*.34f,half=width*.105f;
        quad(out,{cx-half,front,.715f},{cx-half,front,.865f},{cx+half,front,.865f},{cx+half,front,.715f},fixed(0xe2e7db,.24f));
        quad(out,{side*width*.36f-.09f,rear,.665f},{side*width*.36f+.09f,rear,.665f},{side*width*.36f+.09f,rear,.895f},{side*width*.36f-.09f,rear,.895f},fixed(0x9f2520,.30f));
    }
    box(out,{0,length/2+.018f,.60f},{width*.53f,.04f,.19f},trim);box(out,{0,-length/2-.021f,.58f},{.43f,.035f,.15f},fixed(0xd4d2c4,.7f));
    if(pickup){const float rear=-length/2,front=cy-depth/2;
        box(out,{0,(rear+front)/2,.98f},{width*.80f,front-rear-.10f,.08f},fixed(0x31383a));
        for(float side:{-1.f,1.f})roundedBox(out,{side*(width/2-.08f),(rear+front)/2,1.04f},{.16f,front-rear,.33f},.05f,paint());
        if(kind==5)roundedBox(out,{0,rear-.12f,.91f},{width*.85f,.26f,.075f},.035f,paint());
        else roundedBox(out,{0,rear+.06f,1.04f},{width*.85f,.12f,.33f},.04f,paint());
    }
    return out;
}
Model person(unsigned pose){Model out;const Finish shirt{Color(1,1,1),0,.88f,0},skin{Color(1,1,1),2,.9f,0},pants{Color(1,1,1),3,.9f,0},hair{Color(1,1,1),4,.9f,0};
    const auto shoes=fixed(0x34383b,.95f);
    // Tapered chest, rounded pelvis and an oval head: adult proportions, with bent elbows/knees instead of blocks.
    Quaternion chest;chest.setFromUnitVectors({0,1,0},{0,0,1});piece(out,CylinderGeometry::create(.205f,.16f,.47f,8,1,false),{0,.008f,1.145f},{1,1,.62f},shirt,chest);ellipsoid(out,{0,0,.91f},{.17f,.105f,.12f},pants);
    bar(out,{0,.015f,1.38f},{0,.025f,1.47f},.065f,.055f,skin);
    ellipsoid(out,{0,.025f,1.59f},{.105f,.096f,.142f},skin,8);ellipsoid(out,{0,.005f,1.674f},{.108f,.093f,.064f},hair,8);
    ellipsoid(out,{0,.119f,1.59f},{.025f,.035f,.035f},skin);
    for(float side:{-1.f,1.f}){
        const Vector3 hip(side*.095f,0,.91f),knee(side*.105f,pose==2?.43f:.025f,pose==2?.88f:.50f),ankle(side*.11f,pose==2?.45f:0,pose==2?.38f:.12f);
        bar(out,hip,knee,.085f,.064f,pants);bar(out,knee,ankle,.062f,.042f,pants);
        ellipsoid(out,Vector3(ankle).add({0,.045f,-.045f}),{.067f,.13f,.065f},shoes);
        const Vector3 shoulder(side*.205f,.01f,1.36f),elbow(side*(pose==1?.255f:.245f),pose==1?.18f:pose==2?.20f:.02f,pose==1?1.54f:1.08f),
            wrist(side*(pose==1?.085f:pose==2?.13f:.245f),pose==1?.34f:pose==2?.41f:.055f,pose==1?1.67f:pose==2?.92f:.88f);
        bar(out,shoulder,elbow,.066f,.052f,shirt);bar(out,elbow,wrist,.048f,.033f,skin);ellipsoid(out,wrist,{.040f,.046f,.057f},skin);
    }
    if(pose==1)box(out,{0,.365f,1.69f},{.19f,.027f,.10f},fixed(0x252b30,.3f));
    return out;
}
std::shared_ptr<DataTexture> atlas(const std::vector<Model>& models,unsigned& vertices,unsigned& rows){
    for(const auto& model:models)vertices=std::max(vertices,unsigned(model.size()));
    rows=(vertices+255)/256;const unsigned width=768,height=rows*models.size();
    std::vector<float> data(width*height*4,0.f);
    for(unsigned model=0;model<models.size();++model)for(unsigned i=0;i<vertices;++i){const unsigned at=((model*rows+i/256)*width+(i%256)*3)*4;
        data[at+6]=1; // Degenerate padding has a valid normal, so no NaNs reach rasterisation.
        if(i>=models[model].size())continue;
        const auto& v=models[model][i];const auto& c=v.finish.colour;
        const std::array<float,12> packed{v.position.x,v.position.y,v.position.z,v.finish.code,v.normal.x,v.normal.y,v.normal.z,v.finish.rough,c.r,c.g,c.b,v.finish.metal};
        std::copy(packed.begin(),packed.end(),data.begin()+at);
    }
    auto texture=DataTexture::create(ImageData(std::move(data)),width,height);texture->type=Type::Float;texture->minFilter=texture->magFilter=Filter::Nearest;texture->generateMipmaps=false;return texture;
}
void program(const char* name,unsigned vertices,bool people){gpu::Program p;p.points=true;p.instanceVertices=vertices;p.attributes={{"forward",3},{"up",3},{"color",3},{"kind",1}};
    p.uniforms={{"eye",gpu::Uniform::vec3},{"geometryInfo",gpu::Uniform::vec4}};p.textures={"nearGeometry"};p.exactTextures=p.textures;
    p.wgsl=std::string("const people = ")+(people?"true;\n":"false;\n")+R"(
struct Material { eye: vec3f, geometryInfo: vec4f };
@group(2) @binding(0) var<uniform> material: Material;
@group(2) @binding(1) var nearGeometry: texture_2d<f32>;
@group(2) @binding(2) var nearSampler: sampler;
struct Varyings { @builtin(position) clip: vec4f, @location(0) world: vec3f, @location(1) normal: vec3f, @location(2) colour: vec3f, @location(3) finish: vec2f };
@vertex fn vs_main(@location(0) position: vec3f,@location(1) forward: vec3f,@location(2) up: vec3f,@location(3) colour: vec3f,@location(4) kind: f32,@builtin(vertex_index) vertex: u32) -> Varyings {
    let model=select(u32(kind),u32(kind)/4u,people);let variant=f32(u32(kind)%4u);
    let at=vec2i(i32(vertex%256u)*3,i32(model)*i32(material.geometryInfo.y)+i32(vertex/256u));
    let point=textureLoad(nearGeometry,at,0);let normal=textureLoad(nearGeometry,at+vec2i(1,0),0);let ink=textureLoad(nearGeometry,at+vec2i(2,0),0);
    var local=point.xyz;var albedo=ink.rgb;
    if (point.w<0.5) { albedo*=colour; }
    if (point.w>1.5 && point.w<2.5) { albedo=mix(vec3f(0.29,0.16,0.10),vec3f(0.72,0.49,0.33),variant/3.0); }
    if (point.w>2.5 && point.w<3.5) { albedo=mix(vec3f(0.055,0.075,0.105),vec3f(0.16,0.17,0.15),variant/3.0); }
    if (point.w>3.5) { albedo=mix(vec3f(0.035,0.026,0.02),vec3f(0.16,0.10,0.055),variant/3.0); }
    if (people) { local*=0.96+variant*0.025; }
    let basis=mat3x3f(normalize(cross(forward,up)),forward,up);let world=object.model*vec4f(position+basis*local,1);
    var o: Varyings;o.world=world.xyz;o.clip=frame.projection*frame.view*world;
    o.normal=normalize(mat3x3f(object.model[0].xyz,object.model[1].xyz,object.model[2].xyz)*basis*normal.xyz);o.colour=albedo;o.finish=vec2f(normal.w,ink.w);return o;
}
@fragment fn fs_main(v: Varyings,@builtin(front_facing) front: bool) -> @location(0) vec4f {
    let n=normalize(v.normal)*select(-1.0,1.0,front);let viewNormal=normalize((frame.view*vec4f(n,0)).xyz);let light=airLight(v.world,n);
    let colour=standardLight(v.colour,v.finish.x,v.finish.y,viewNormal,viewNormal,(frame.view*vec4f(v.world,1)).xyz,light.sky*frame.sunColor.rgb,shadowFactor(v.world,n)*light.sun);
    return vec4f(finish(airApply(v.world,colour)),1);
}
)";gpu::define(name,std::move(p));
}
}
CrowdDetail makeCrowdDetail(){CrowdDetail result;std::vector<Model> cars,people;for(unsigned i=0;i<6;++i)cars.push_back(car(i));for(unsigned i=0;i<3;++i)people.push_back(person(i));
    result.cars=atlas(cars,result.carVertices,result.carRows);result.people=atlas(people,result.personVertices,result.personRows);
    program("albert:crowd-car-near",result.carVertices,false);program("albert:crowd-person-near",result.personVertices,true);return result;
}
}
