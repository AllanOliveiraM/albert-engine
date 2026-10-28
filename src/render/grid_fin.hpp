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
    // V3's fin as built, a shield of seven sides: narrow at its root at the hinge (a short straight side), two long sides opening
    // out from it, and the broad far end closed by four short sides; closed all round by a rim of arcs (one a boundary cell)
    // whose crown makes it read round. Inside the declared envelope with the rim's bulge (8 cm at most) and the walls' cut ends'
    // gauge and bow.
    const float bottom=-span/2+.08f,top=span/2-.14f,root=.12f*width,lean=10*pi/180,radius=std::min((width/2-.14f)/std::cos(lean),.6f*(top-bottom)),centre=top-radius;
    std::vector<Vector2> outline{{-root,bottom},{root,bottom}};
    for(int k=0;k<=4;++k){const float t=-lean+k*(pi+2*lean)/4;outline.push_back({radius*std::cos(t),centre+radius*std::sin(t)});}
    const int corners=static_cast<int>(outline.size());
    auto distance=[&](const Vector3& p,int e){const auto& u=outline[e];const auto& v=outline[(e+1)%corners];const float dx=v.x-u.x,dz=v.y-u.y;
        return (dx*(p.z-u.y)-dz*(p.x-u.x))/std::hypot(dx,dz);}; // Inside > 0 (counterclockwise).
    auto clearance=[&](const Vector3& p){float d=1e6f;for(int e=0;e<corners;++e)d=std::min(d,distance(p,e));return d;};
    // The rim's length from the root's left end: where a point on the outline lies on it, and the point at a length.
    std::vector<float> perimeter{0};
    for(int e=0;e<corners;++e){const auto& u=outline[e];const auto& v=outline[(e+1)%corners];perimeter.push_back(perimeter.back()+std::hypot(v.x-u.x,v.y-u.y));}
    auto onRim=[&](const Vector3& p){int best=0;float d=1e9f;for(int e=0;e<corners;++e){const float x=std::abs(distance(p,e));if(x<d){d=x;best=e;}}
        const auto& u=outline[best];return perimeter[best]+std::hypot(p.x-u.x,p.z-u.y);};
    auto rimAt=[&](float s){s=std::fmod(s,perimeter.back());if(s<0)s+=perimeter.back();
        const int e=std::clamp(static_cast<int>(std::upper_bound(perimeter.begin(),perimeter.end(),s)-perimeter.begin())-1,0,corners-1);
        const auto& u=outline[e];const auto& v=outline[(e+1)%corners];const float f=(s-perimeter[e])/std::max(perimeter[e+1]-perimeter[e],1e-6f);
        return Vector3(u.x+(v.x-u.x)*f,0,u.y+(v.y-u.y)*f);};
    struct Vertex {Vector3 p,n;float shade;};std::vector<float> positions,normals,colours;
    auto triangle=[&](const Vertex& a,Vertex b,Vertex c){const auto cross=Vector3(b.p).sub(a.p).cross(Vector3(c.p).sub(a.p));if(cross.lengthSq()<1e-16f)return;
        if(cross.dot(Vector3(a.n).add(b.n).add(c.n))<0)std::swap(b,c);
        for(const auto& v:{a,b,c}){positions.insert(positions.end(),{v.p.x,v.p.y,v.p.z});normals.insert(normals.end(),{v.n.x,v.n.y,v.n.z});colours.insert(colours.end(),{v.shade,v.shade,v.shade});}};
    GridFin result;result.segments=segments;const int count=int(std::ceil((width+span)*rt/pitch))+1;
    std::map<std::pair<int,int>,unsigned> welds;std::vector<float> junctions(perimeter.begin(),perimeter.end()-1); // The corners, then where walls meet the rim.
    auto point=[](float u,float v){return Vector3((u+v)*rt,0,(u-v)*rt);};
    auto well=[](float v){return .24f+1.56f*std::clamp(1-std::min(v,1-v)/.14f,0.f,1.f);};
    for(int i=-count;i<=count;++i)for(int j=-count;j<count;++j){
        if(clearance(point((i+.5f)*pitch,(j+.5f)*pitch))>.34f)++result.cells;
        for(int family=0;family<2;++family){const auto a=family==0?point(i*pitch,j*pitch):point(j*pitch,i*pitch);
            const auto b=family==0?point(i*pitch,(j+1)*pitch):point((j+1)*pitch,i*pitch);const Vector3 along=Vector3(b).sub(a).normalize(),across(-along.z,0,along.x);
            float lo=0,hi=1; // Clipped at the outline itself: the rim closes every cell it cuts.
            for(int e=0;e<corners;++e){const float da=distance(a,e),db=distance(b,e);
                if(da<0&&db<0){hi=-1;break;}if(da<0)lo=std::max(lo,da/(da-db));if(db<0)hi=std::min(hi,da/(da-db));}
            if((hi-lo)*pitch<.03f)continue;
            const bool cutA=lo>.0001f,cutB=hi<.9999f;
            if(!cutA)++welds[family==0?std::pair{i,j}:std::pair{j,i}];else{junctions.push_back(onRim(Vector3(a).lerp(b,lo)));++result.ends;}
            if(!cutB)++welds[family==0?std::pair{i,j+1}:std::pair{j+1,i}];else{junctions.push_back(onRim(Vector3(a).lerp(b,hi)));++result.ends;}
            auto section=[&](float s){const float q=2*s-1,arc=tier==0?(std::sqrt(std::max(0.f,1-.84f*q*q))-.4f)/.6f:0.f;
                const float h=thickness/2-.13f*arc,bow=.055f*arc;const auto p=Vector3(a).lerp(b,s);std::array<Vector3,4> v;int k=0;
                for(const auto d:std::array<Vector2,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})v[k++]=Vector3(p).addScaledVector(across,d.x*gauge/2+d.y*bow).setY(d.y*h);return v;};
            // Split the depth, not the draw: bright narrow lips surround dark well faces in the same PBR batch.
            for(int face=0;face<4;++face){const bool inner=face%2;const std::array<float,4> depth{0,.14f,.86f,1};
                for(int band=0;band<(inner?3:1);++band){const float v0=inner?depth[band]:0,v1=inner?depth[band+1]:1;std::vector<Vertex> strip;
                    for(int k=0;k<=segments;++k){const float s=std::lerp(lo,hi,float(k)/segments);const auto v=section(s),prev=section(std::max(lo,s-.001f)),next=section(std::min(hi,s+.001f));
                        for(float f:{v0,v1}){const int at=(face+1)%4;const auto p=Vector3(v[face]).lerp(v[at],f),p0=Vector3(prev[face]).lerp(prev[at],f),p1=Vector3(next[face]).lerp(next[at],f);
                            auto n=Vector3(p1).sub(p0).cross(Vector3(v[at]).sub(v[face])).normalize();strip.push_back({p,n,inner?well(f):1.8f});}}
                    for(int k=0;k<segments;++k){triangle(strip[2*k],strip[2*k+2],strip[2*k+3]);triangle(strip[2*k],strip[2*k+3],strip[2*k+1]);}}}
            for(int end=0;end<2;++end){const auto v=section(end?hi:lo);const auto n=Vector3(along).multiplyScalar(end?1.f:-1.f);
                triangle({v[0],n,1},{v[1],n,1},{v[2],n,1});triangle({v[0],n,1},{v[2],n,1},{v[3],n,1});}
            ++result.walls;
        }
    }
    // The rim: from each junction to the next an arc bowed out of the fin by a sixth of its chord (the crown's scallops), straight
    // along the root; as deep as the walls, its edges dipping between junctions as theirs do.
    std::sort(junctions.begin(),junctions.end());
    {std::vector<float> kept;for(const float s:junctions)if(kept.empty()||s-kept.back()>.02f)kept.push_back(s);junctions.swap(kept);}
    {const float rim=.035f;const int steps=tier==0?8:2;
        for(std::size_t k=0;k<junctions.size();++k){const float s0=junctions[k],s1=k+1<junctions.size()?junctions[k+1]:junctions.front()+perimeter.back();
            const bool root=s1<=perimeter[1]+1e-4f;const Vector3 p0=rimAt(s0),p1=rimAt(s1);Vector3 chord=Vector3(p1).sub(p0);const float length=chord.length();
            if(length<1e-4f)continue;chord.divideScalar(length);const Vector3 out(chord.z,0,-chord.x);const float bulge=root?0.f:std::min(length/6,.08f);
            auto at=[&](float t,Vector3& centre,Vector3& normal,float& h){centre=Vector3(p0).lerp(p1,t).addScaledVector(out,bulge*std::sin(pi*t));
                const Vector3 tangent=Vector3(chord).multiplyScalar(length).addScaledVector(out,bulge*pi*std::cos(pi*t)).normalize();normal.set(tangent.z,0,-tangent.x);
                h=thickness/2-(tier==0&&!root?.10f*std::sin(pi*t):0.f);};
            for(int step=0;step<steps;++step){Vector3 c0,n0,c1,n1;float h0,h1;at(float(step)/steps,c0,n0,h0);at(float(step+1)/steps,c1,n1,h1);
                for(const float side:{1.f,-1.f}){ // The outer face, and the inner a well's dark.
                    const Vector3 o0=Vector3(c0).addScaledVector(n0,side*rim/2),o1=Vector3(c1).addScaledVector(n1,side*rim/2),m0=Vector3(n0).multiplyScalar(side),m1=Vector3(n1).multiplyScalar(side);
                    const float shade=side>0?1.f:.5f;
                    triangle({Vector3(o0).setY(-h0),m0,shade},{Vector3(o1).setY(-h1),m1,shade},{Vector3(o1).setY(h1),m1,shade});
                    triangle({Vector3(o0).setY(-h0),m0,shade},{Vector3(o1).setY(h1),m1,shade},{Vector3(o0).setY(h0),m0,shade});}
                for(const float y:{1.f,-1.f}){const Vector3 up(0,y,0); // The lips.
                    const Vector3 a0=Vector3(c0).addScaledVector(n0,rim/2).setY(y*h0),b0=Vector3(c0).addScaledVector(n0,-rim/2).setY(y*h0);
                    const Vector3 a1=Vector3(c1).addScaledVector(n1,rim/2).setY(y*h1),b1=Vector3(c1).addScaledVector(n1,-rim/2).setY(y*h1);
                    triangle({a0,up,1.8f},{a1,up,1.8f},{b1,up,1.8f});triangle({a0,up,1.8f},{b1,up,1.8f},{b0,up,1.8f});}}}}
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
