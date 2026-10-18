#include "rocks.hpp"
#include "lighting.hpp"
#include "threepp/threepp.hpp"
#include <cmath>
#include <numbers>
using namespace threepp;
namespace {
std::uint32_t mix(std::uint32_t v){v^=v>>16;v*=0x7feb352du;v^=v>>15;v*=0x846ca68bu;return v^(v>>16);}
float random(std::uint32_t& state){state=mix(state+0x9e3779b9u);return float(state>>8)/16777216.f;}
float noise(Vector3 p,std::uint32_t seed){
    const int x=int(std::floor(p.x)),y=int(std::floor(p.y)),z=int(std::floor(p.z));
    auto smooth=[](float t){return t*t*(3-2*t);};
    const float a=smooth(p.x-x),b=smooth(p.y-y),c=smooth(p.z-z);
    auto h=[&](int i,int j,int k){return float(mix(seed^std::uint32_t(i)*73856093u^std::uint32_t(j)*19349663u^std::uint32_t(k)*83492791u)>>8)/16777216.f*2-1;};
    return std::lerp(std::lerp(std::lerp(h(x,y,z),h(x+1,y,z),a),std::lerp(h(x,y+1,z),h(x+1,y+1,z),a),b),
                     std::lerp(std::lerp(h(x,y,z+1),h(x+1,y,z+1),a),std::lerp(h(x,y+1,z+1),h(x+1,y+1,z+1),a),b),c);
}
struct Crater {Vector3 axis;float radius,depth;};
}
bool albert::artisticRock(std::string_view id){
    const auto* body=core::localOrbit(id);
    return body&&(body->category==core::Category::Asteroid||body->category==core::Category::Comet||
                 (body->category==core::Category::Moon&&body->radius<150000.));
}
std::shared_ptr<Mesh> albert::makeArtisticRock(const core::LocalOrbit& body){
    std::uint32_t seed=2166136261u;for(unsigned char c:body.id){seed^=c;seed*=16777619u;}
    auto state=seed;std::vector<Crater> craters;
    for(int i=0;i<26;++i){Vector3 axis(random(state)*2-1,random(state)*2-1,random(state)*2-1);axis.normalize();craters.push_back({axis,.07f+random(state)*.29f,.035f+random(state)*.08f});}
    const bool comet=body.category==core::Category::Comet,bilobed=body.id=="67p";
    Vector3 axes=comet?Vector3(1.65f,.77f,.78f):Vector3(1.16f,.91f,.98f);
    if(bilobed)axes.set(1.48f,.95f,.84f);
    if(body.id=="vesta")axes.set(1.12f,1.04f,.91f);
    auto surface=[&](Vector3 n){
        n.normalize();float radius=1.f+.11f*noise(n.clone().multiplyScalar(3.f),seed)+.025f*noise(n.clone().multiplyScalar(13.f),seed);
        for(const auto& crater:craters){const float r=std::sqrt(std::max(0.f,2.f*(1.f-n.dot(crater.axis))))/crater.radius;
            if(r<1.5f)radius+=crater.depth*(.27f*std::exp(-std::pow((r-1.f)/.18f,2.f))-.8f*std::exp(-3.5f*r*r));}
        Vector3 p(n.x*axes.x,n.y*axes.y,n.z*axes.z);p.multiplyScalar(radius);
        if(bilobed){const float neck=1.f-.43f*std::exp(-std::pow((n.x+.12f)/.28f,2.f));p.y*=neck;p.z*=neck;p.x+=.10f*n.x*n.x;}
        return p;
    };
    auto geometry=IcosahedronGeometry::create(1.f,31);
    auto* position=geometry->getAttribute<float>("position");auto* normal=geometry->getAttribute<float>("normal");
    auto& vertices=position->array();auto& normals=normal->array();
    for(size_t i=0;i<vertices.size();i+=3){
        Vector3 n(vertices[i],vertices[i+1],vertices[i+2]);n.normalize();
        Vector3 tangent(0,1,0);if(std::abs(n.y)>.9f)tangent.set(1,0,0);tangent.cross(n).normalize();
        const auto bitangent=n.clone().cross(tangent);constexpr float step=.0005f;
        auto du=surface(n.clone().add(tangent.clone().multiplyScalar(step)));du.sub(surface(n.clone().add(tangent.clone().multiplyScalar(-step))));
        auto dv=surface(n.clone().add(bitangent.clone().multiplyScalar(step)));dv.sub(surface(n.clone().add(bitangent.clone().multiplyScalar(-step))));
        auto direction=du.cross(dv).normalize();if(direction.dot(n)<0)direction.negate();
        const auto p=surface(n);vertices[i]=p.x;vertices[i+1]=p.y;vertices[i+2]=p.z;
        normals[i]=direction.x;normals[i+1]=direction.y;normals[i+2]=direction.z;
    }
    // Preserve the nominal volume-equivalent radius, centered on mesh volume.
    double volume=0;Vector3 centroid;
    for(size_t i=0;i<vertices.size();i+=9){Vector3 a(vertices[i],vertices[i+1],vertices[i+2]),b(vertices[i+3],vertices[i+4],vertices[i+5]),c(vertices[i+6],vertices[i+7],vertices[i+8]);
        const double v=a.dot(b.clone().cross(c))/6.;volume+=v;centroid.add(a.add(b).add(c).multiplyScalar(float(v/4.)));}
    centroid.divideScalar(float(volume));const float scale=.5f*float(std::cbrt((4.*std::numbers::pi/3.)/std::abs(volume)));
    for(size_t i=0;i<vertices.size();i+=3){vertices[i]=(vertices[i]-centroid.x)*scale;vertices[i+1]=(vertices[i+1]-centroid.y)*scale;vertices[i+2]=(vertices[i+2]-centroid.z)*scale;}
    geometry->computeBoundingSphere();geometry->computeBoundingBox();
    auto material=assistedMaterial(comet?Color(.24f,.22f,.20f):Color(.40f,.37f,.32f),0.f,.97f);
    material->name="albert:rock";
    material->uniforms.emplace("rockSeed",Uniform(float(seed%8192)));
    material->uniforms.emplace("rockScale",Uniform(float(body.radius*2/core::earthRadiusMeters)));
    material->vertexShader="varying vec3 rockPoint;\n"+material->vertexShader;
    const std::string begin="#include <begin_vertex>";material->vertexShader.replace(material->vertexShader.find(begin),begin.size(),begin+"\nrockPoint=position;\n");
    material->fragmentShader=R"(
      varying vec3 rockPoint;uniform float rockSeed,rockScale;
      float rockHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
      float rockNoise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);
        return mix(mix(mix(rockHash(i),rockHash(i+vec3(1,0,0)),f.x),mix(rockHash(i+vec3(0,1,0)),rockHash(i+vec3(1,1,0)),f.x),f.y),
                   mix(mix(rockHash(i+vec3(0,0,1)),rockHash(i+vec3(1,0,1)),f.x),mix(rockHash(i+vec3(0,1,1)),rockHash(i+vec3(1,1,1)),f.x),f.y),f.z);}
      float rockGrain(vec3 p){float value=0.,weight=.53;for(int i=0;i<4;++i){float footprint=max(max(fwidth(p.x),fwidth(p.y)),fwidth(p.z));value+=weight*mix(.5,rockNoise(p),1.-smoothstep(.3,1.,footprint));p=p*2.13+vec3(7.3,2.6,9.1);weight*=.47;}return value;}
    )"+material->fragmentShader;
    const std::string colors="#include <color_fragment>";
    material->fragmentShader.replace(material->fragmentShader.find(colors),colors.size(),colors+R"(
      float mineral=rockGrain(rockPoint*17.+vec3(rockSeed));
      float grain=rockGrain(rockPoint*155.+vec3(rockSeed));
      diffuseColor.rgb*=mix(.42,1.25,mineral)*mix(.70,1.12,grain);
    )");
    const std::string maps="#include <normal_fragment_maps>";
    material->fragmentShader.replace(material->fragmentShader.find(maps),maps.size(),maps+R"(
      vec3 q0=dFdx(-vViewPosition),q1=dFdy(-vViewPosition);
      vec3 r0=cross(q1,normal),r1=cross(normal,q0);float determinant=dot(q0,r0);
      vec3 gradient=r0*dFdx(grain)+r1*dFdy(grain);
      normal=normalize(normal-sign(determinant)*gradient*(rockScale*.0012)/max(abs(determinant),1.e-20));
    )");
    auto mesh=Mesh::create(geometry,material);mesh->castShadow=mesh->receiveShadow=true;return mesh;
}
