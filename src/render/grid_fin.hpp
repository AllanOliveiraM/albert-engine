#pragma once
// Drawing metres: x across the fin, y through the flow, z along the unchanged radial spindle.
#include "threepp/threepp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>
namespace albert {
struct GridFin {
    std::shared_ptr<threepp::BufferGeometry> geometry;
    unsigned walls=0,cells=0,ends=0,joints=0;int segments=0;
};
inline GridFin gridFin(float width,float span,float thickness,int tier){
    using namespace threepp;constexpr float rt=.70710678118f,pi=3.14159265359f;
    const float pitch=.48f,gauge=.022f;const int segments=tier==0?10:1;
    // V3's rounded fin: an ellipse across the whole width and out to the tip, cut straight at the root (the hinge's quarter of the
    // width each side). Its arc in edges about a diagonal cell long, each one scallop of the crown.
    const float a=width/2,root=.25f*width,q=std::sqrt(1-root*root/(a*a)),centre=-span/2*(1-q)/(1+q),b=span/2-centre;
    const float from=std::asin((-span/2-centre)/b),to=pi-from;std::vector<Vector2> outline{{-root,-span/2},{root,-span/2}};
    const int arcs=std::max(8,int(std::round(pi*(a+b)*(to-from)/(2*pi)/(pitch*1.41421356f))));
    for(int k=1;k<arcs;++k){const float t=from+(to-from)*k/arcs;outline.push_back({a*std::cos(t),centre+b*std::sin(t)});}
    auto distance=[&](const Vector3& p,int edge,bool scallop=true){const auto& a=outline[edge];const auto& b=outline[(edge+1)%outline.size()];
        const float dx=b.x-a.x,dz=b.y-a.y,len=std::hypot(dx,dz),s=((p.x-a.x)*dx+(p.z-a.y)*dz)/len;
        const int teeth=std::max(1,int(std::round(len/(pitch*1.41421356f))));const float step=len/teeth;
        const float phase=std::clamp(s/step,0.f,float(teeth));
        // A cosine has a rounded crest, unlike the old intersecting arcs' knife point.
        const float bite=edge==0||!scallop?0.f:.065f*.5f*(1-std::cos(2*pi*phase));
        return (dx*(p.z-a.y)-dz*(p.x-a.x))/len-bite;};
    auto clearance=[&](const Vector3& p){float d=1e6f;for(int e=0;e<int(outline.size());++e)d=std::min(d,distance(p,e));return d;};
    struct Vertex {Vector3 p,n;float shade;};std::vector<float> positions,normals,colours;
    auto triangle=[&](const Vertex& a,Vertex b,Vertex c){const auto cross=Vector3(b.p).sub(a.p).cross(Vector3(c.p).sub(a.p));if(cross.lengthSq()<1e-16f)return;
        if(cross.dot(Vector3(a.n).add(b.n).add(c.n))<0)std::swap(b,c);
        for(const auto& v:{a,b,c}){positions.insert(positions.end(),{v.p.x,v.p.y,v.p.z});normals.insert(normals.end(),{v.n.x,v.n.y,v.n.z});colours.insert(colours.end(),{v.shade,v.shade,v.shade});}};
    GridFin result;result.segments=segments;const int count=int(std::ceil((width+span)*rt/pitch))+1;
    std::map<std::pair<int,int>,unsigned> welds;
    auto point=[](float u,float v){return Vector3((u+v)*rt,0,(u-v)*rt);};
    auto well=[](float v){return .24f+1.56f*std::clamp(1-std::min(v,1-v)/.14f,0.f,1.f);};
    for(int i=-count;i<=count;++i)for(int j=-count;j<count;++j){
        if(clearance(point((i+.5f)*pitch,(j+.5f)*pitch))>.34f)++result.cells;
        for(int family=0;family<2;++family){const auto a=family==0?point(i*pitch,j*pitch):point(j*pitch,i*pitch);
            const auto b=family==0?point(i*pitch,(j+1)*pitch):point((j+1)*pitch,i*pitch);const Vector3 along=Vector3(b).sub(a).normalize(),across(-along.z,0,along.x);
            float lo=0,hi=1;
            // Leave room for real 8 cm round end caps; do not clip the sheet down to a needle afterwards.
            for(int e=0;e<int(outline.size());++e){const float da=distance(a,e,false)-.105f,db=distance(b,e,false)-.105f;
                if(da<0&&db<0){hi=-1;break;}if(da<0)lo=std::max(lo,da/(da-db));if(db<0)hi=std::min(hi,da/(da-db));}
            if(hi<=lo)continue;
            auto inside=[&](float s){return clearance(Vector3(a).lerp(b,s))>=.105f;};
            const float first=lo,last=hi;int start=-1,stop=-1;for(int k=0;k<=24;++k)if(inside(std::lerp(first,last,float(k)/24))){if(start<0)start=k;stop=k;}
            if(start<0)continue;
            if(start>0){float l=std::lerp(first,last,float(start-1)/24),h=std::lerp(first,last,float(start)/24);for(int k=0;k<18;++k){const float m=(l+h)/2;if(inside(m))h=m;else l=m;}lo=h;}
            if(stop<24){float l=std::lerp(first,last,float(stop)/24),h=std::lerp(first,last,float(stop+1)/24);for(int k=0;k<18;++k){const float m=(l+h)/2;if(inside(m))l=m;else h=m;}hi=l;}
            const bool cutA=lo>.0001f,cutB=hi<.9999f;
            if((cutA||cutB)&&(hi-lo)*pitch<.18f)continue; // No detached slivers or short prongs outside the last useful crossing.
            if(cutA&&cutB)continue;
            if(!cutA)++welds[family==0?std::pair{i,j}:std::pair{j,i}];
            if(!cutB)++welds[family==0?std::pair{i,j+1}:std::pair{j+1,i}];
            const bool edgeA=cutA||clearance(a)<.32f,edgeB=cutB||clearance(b)<.32f;
            auto section=[&](float s){const float q=2*s-1,arc=tier==0?(std::sqrt(std::max(0.f,1-.84f*q*q))-.4f)/.6f:0.f;
                const float end=std::max(edgeA?std::clamp(1-(s-lo)*pitch/.12f,0.f,1.f):0.f,edgeB?std::clamp(1-(hi-s)*pitch/.12f,0.f,1.f):0.f);
                const float h=std::min(thickness/2-.13f*arc,thickness/2-.055f*end),bow=.055f*arc,halfGauge=std::lerp(gauge/2,.04f,end);
                const auto p=Vector3(a).lerp(b,s);std::array<Vector3,4> v;int k=0;
                for(const auto d:std::array<Vector2,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})v[k++]=Vector3(p).addScaledVector(across,d.x*halfGauge+d.y*bow).setY(d.y*h);return v;};
            // Split the depth, not the draw: bright narrow lips surround dark well faces in the same PBR batch.
            for(int face=0;face<4;++face){const bool inner=face%2;const std::array<float,4> depth{0,.14f,.86f,1};
                for(int band=0;band<(inner?3:1);++band){const float v0=inner?depth[band]:0,v1=inner?depth[band+1]:1;std::vector<Vertex> strip;
                    for(int k=0;k<=segments;++k){const float s=std::lerp(lo,hi,float(k)/segments);const auto v=section(s),prev=section(std::max(lo,s-.001f)),next=section(std::min(hi,s+.001f));
                        for(float f:{v0,v1}){const int at=(face+1)%4;const auto p=Vector3(v[face]).lerp(v[at],f),p0=Vector3(prev[face]).lerp(prev[at],f),p1=Vector3(next[face]).lerp(next[at],f);
                            auto n=Vector3(p1).sub(p0).cross(Vector3(v[at]).sub(v[face])).normalize();strip.push_back({p,n,inner?well(f):1.8f});}}
                    for(int k=0;k<segments;++k){triangle(strip[2*k],strip[2*k+2],strip[2*k+3]);triangle(strip[2*k],strip[2*k+3],strip[2*k+1]);}}}
            for(int end=0;end<2;++end){const auto v=section(end?hi:lo);const bool cut=end?cutB:cutA;const auto n=Vector3(along).multiplyScalar(end?1.f:-1.f);
                if(!cut){triangle({v[0],n,1},{v[1],n,1},{v[2],n,1});triangle({v[0],n,1},{v[2],n,1},{v[3],n,1});continue;}
                ++result.ends;const int caps=tier==0?8:4;const auto bottom=Vector3(v[0]).lerp(v[1],.5f),top=Vector3(v[3]).lerp(v[2],.5f);
                for(int k=0;k<caps;++k){const float aa=pi*k/caps,bb=pi*(k+1)/caps;
                    const auto na=Vector3(across).multiplyScalar(std::cos(aa)).addScaledVector(n,std::sin(aa)),nb=Vector3(across).multiplyScalar(std::cos(bb)).addScaledVector(n,std::sin(bb));
                    const auto ba=Vector3(bottom).addScaledVector(na,.04f),bt=Vector3(top).addScaledVector(na,.04f),ca=Vector3(bottom).addScaledVector(nb,.04f),ct=Vector3(top).addScaledVector(nb,.04f);
                    triangle({ba,na,.5f},{ca,nb,.5f},{ct,nb,.5f});triangle({ba,na,.5f},{ct,nb,.5f},{bt,na,.5f});
                    triangle({bottom,{0,-1,0},1.8f},{ca,{0,-1,0},1.8f},{ba,{0,-1,0},1.8f});triangle({top,{0,1,0},1.8f},{bt,{0,1,0},1.8f},{ct,{0,1,0},1.8f});}}
            ++result.walls;
        }
    }
    // The last welded V also needs a fillet: rounding only cut ends leaves needle-shaped full-cell crests.
    for(const auto& [node,degree]:welds)if(degree<4){++result.joints;const auto centre=point(node.first*pitch,node.second*pitch);const float h=thickness/2-.055f;
        const int sides=tier==0?16:8;struct Ring {float r,y,ny,shade;};
        const std::array<Ring,8> rings{{{.045f,-h,-1,1.8f},{.052071f,-h+.002929f,-rt,1.8f},{.055f,-h+.01f,0,1.8f},{.055f,-h+.07f,0,.24f},
            {.055f,h-.07f,0,.24f},{.055f,h-.01f,0,1.8f},{.052071f,h-.002929f,rt,1.8f},{.045f,h,1,1.8f}}};
        auto vertex=[&](const Ring& r,float angle){const float x=std::cos(angle),z=std::sin(angle),radial=std::sqrt(std::max(0.f,1-r.ny*r.ny));return Vertex{Vector3(centre).add(Vector3(r.r*x,r.y,r.r*z)),{radial*x,r.ny,radial*z},r.shade};};
        for(int k=0;k<sides;++k){const float a=2*pi*k/sides,b=2*pi*(k+1)/sides;
            for(int r=0;r<7;++r){const auto v0=vertex(rings[r],a),v1=vertex(rings[r],b),v2=vertex(rings[r+1],b),v3=vertex(rings[r+1],a);triangle(v0,v1,v2);triangle(v0,v2,v3);}
            triangle({Vector3(centre).setY(-h),{0,-1,0},1.8f},vertex(rings.front(),b),vertex(rings.front(),a));
            triangle({Vector3(centre).setY(h),{0,1,0},1.8f},vertex(rings.back(),a),vertex(rings.back(),b));}}
    auto g=BufferGeometry::create();g->setAttribute("position",FloatBufferAttribute::create(std::move(positions),3));g->setAttribute("normal",FloatBufferAttribute::create(std::move(normals),3));g->setAttribute("color",FloatBufferAttribute::create(std::move(colours),3));result.geometry=g;return result;
}
}
