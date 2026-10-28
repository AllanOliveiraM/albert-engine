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
    std::shared_ptr<threepp::BufferGeometry> mount; // The bearing plate inside the V, below the fin: drawn beside it, outside its envelope.
    unsigned walls=0,cells=0,ends=0,joints=0;int segments=0;
};
inline GridFin gridFin(float width,float span,float thickness,int tier){
    using namespace threepp;constexpr float rt=.70710678118f,pi=3.14159265359f;
    const float pitch=.42f,gauge=.022f;const int segments=tier==0?10:1;
    // V3's fin: an octagon, a square stretched a little out from the hull (4.7 m out, 4 m across) with long parallel sides and a
    // long straight outer edge, its outer corners cut small; the side at the hinge short (a seventh of the width), reached by
    // longer, steeper cuts from the parallel sides. Inside the declared envelope with the walls' cut ends' gauge and bow; a rim
    // straight from wall to wall closes every cell it cuts.
    const float bottom=-span/2+.08f,top=span/2-.08f,half=width/2-.08f,root=.07f*width,outer=.22f*width,low=1.1f*(half-root);
    const std::vector<Vector2> outline{{-root,bottom},{root,bottom},{half,bottom+low},{half,top-outer},{half-outer,top},{-half+outer,top},{-half,top-outer},{-half,bottom+low}};
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
    // At the hinge, a box of thick steel from the root out over half way to the base beam, and a V from its far corners to the
    // beam's middle: no lattice inside either, the walls stopping at them.
    const float rootHalf=outline[1].x,beamZ=outline[2].y,boxTop=bottom+.55f*(beamZ-bottom),boxHalf=.8f*rootHalf;
    const Vector3 vTip(0,0,beamZ);
    const std::array<Vector3,5> hole{{{-rootHalf,0,bottom},{rootHalf,0,bottom},{boxHalf,0,boxTop},vTip,{-boxHalf,0,boxTop}}};
    auto holeSide=[&](const Vector3& p,int e){const auto& u=hole[e];const auto& v=hole[(e+1)%5];const float dx=v.x-u.x,dz=v.z-u.z;return (dx*(p.z-u.z)-dz*(p.x-u.x))/std::hypot(dx,dz);};
    for(int i=-count;i<=count;++i)for(int j=-count;j<count;++j){
        if(clearance(point((i+.5f)*pitch,(j+.5f)*pitch))>.34f)++result.cells;
        for(int family=0;family<2;++family){const auto a=family==0?point(i*pitch,j*pitch):point(j*pitch,i*pitch);
            const auto b=family==0?point(i*pitch,(j+1)*pitch):point((j+1)*pitch,i*pitch);const Vector3 along=Vector3(b).sub(a).normalize(),across(-along.z,0,along.x);
            float lo=0,hi=1; // Clipped at the outline itself: the rim closes every cell it cuts.
            for(int e=0;e<corners;++e){const float da=distance(a,e),db=distance(b,e);
                if(da<0&&db<0){hi=-1;break;}if(da<0)lo=std::max(lo,da/(da-db));if(db<0)hi=std::min(hi,da/(da-db));}
            if((hi-lo)*pitch<.03f)continue;
            float in0=0,in1=1; // The part inside the V.
            for(int e=0;e<5;++e){const float da=holeSide(a,e),db=holeSide(b,e);
                if(da<0&&db<0){in1=-1;break;}if(da<0)in0=std::max(in0,da/(da-db));if(db<0)in1=std::min(in1,da/(da-db));}
            std::vector<std::array<float,2>> pieces;
            if(in1<=in0||in1<=lo||in0>=hi)pieces.push_back({lo,hi});else{if(in0-lo>.03f/pitch)pieces.push_back({lo,in0});if(hi-in1>.03f/pitch)pieces.push_back({in1,hi});}
            for(const auto& [from,to]:pieces){
            const bool cutA=from>.0001f,cutB=to<.9999f,rimA=lo>.0001f&&from==lo,rimB=hi<.9999f&&to==hi;
            if(!cutA)++welds[family==0?std::pair{i,j}:std::pair{j,i}];else if(rimA){junctions.push_back(onRim(Vector3(a).lerp(b,from)));++result.ends;}
            if(!cutB)++welds[family==0?std::pair{i,j+1}:std::pair{j+1,i}];else if(rimB){junctions.push_back(onRim(Vector3(a).lerp(b,to)));++result.ends;}
            // Top edges in shallow concave arcs; the bottom ones rise from a sharp convex point at each crossing (the spikes the fin
            // shows underneath).
            auto section=[&](float s){const float q=2*s-1,arc=tier==0?(std::sqrt(std::max(0.f,1-.84f*q*q))-.4f)/.6f:0.f,spike=tier==0?std::sqrt(std::max(0.f,std::sin(pi*s))):0.f;
                const float top=thickness/2-.13f*arc,under=thickness/2-.22f*spike,bow=.055f*arc;const auto p=Vector3(a).lerp(b,s);std::array<Vector3,4> v;int k=0;
                for(const auto d:std::array<Vector2,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})v[k++]=Vector3(p).addScaledVector(across,d.x*gauge/2+d.y*bow).setY(d.y>0?top:-under);return v;};
            // Split the depth, not the draw: bright narrow lips surround dark well faces in the same PBR batch.
            for(int face=0;face<4;++face){const bool inner=face%2;const std::array<float,4> depth{0,.14f,.86f,1};
                for(int band=0;band<(inner?3:1);++band){const float v0=inner?depth[band]:0,v1=inner?depth[band+1]:1;std::vector<Vertex> strip;
                    for(int k=0;k<=segments;++k){const float s=std::lerp(from,to,float(k)/segments);const auto v=section(s),prev=section(std::max(from,s-.001f)),next=section(std::min(to,s+.001f));
                        for(float f:{v0,v1}){const int at=(face+1)%4;const auto p=Vector3(v[face]).lerp(v[at],f),p0=Vector3(prev[face]).lerp(prev[at],f),p1=Vector3(next[face]).lerp(next[at],f);
                            auto n=Vector3(p1).sub(p0).cross(Vector3(v[at]).sub(v[face])).normalize();strip.push_back({p,n,inner?well(f):1.8f});}}
                    for(int k=0;k<segments;++k){triangle(strip[2*k],strip[2*k+2],strip[2*k+3]);triangle(strip[2*k],strip[2*k+3],strip[2*k+1]);}}}
            for(int end=0;end<2;++end){const auto v=section(end?to:from);const auto n=Vector3(along).multiplyScalar(end?1.f:-1.f);
                triangle({v[0],n,1},{v[1],n,1},{v[2],n,1});triangle({v[0],n,1},{v[2],n,1},{v[3],n,1});}
            ++result.walls;}
        }
    }
    // The rim: straight from each junction to the next, as deep as the walls, flat on top and in triangular teeth underneath.
    std::sort(junctions.begin(),junctions.end());
    {std::vector<float> kept;for(const float s:junctions)if(kept.empty()||s-kept.back()>.02f)kept.push_back(s);junctions.swap(kept);}
    // The base, where V3 rests on the tower's arms, reinforced: its rim (the root and the two cuts from it) twice as thick and
    // straight through its depth, and a steel beam across between the cuts' outer corners.
    {const int steps=tier==0?8:2;
        for(std::size_t k=0;k<junctions.size();++k){const float s0=junctions[k],s1=k+1<junctions.size()?junctions[k+1]:junctions.front()+perimeter.back();
            const bool root=s1<=perimeter[2]+1e-4f||s0>=perimeter[corners-1]-1e-4f;const float rim=root?.08f:.035f;const Vector3 p0=rimAt(s0),p1=rimAt(s1);Vector3 chord=Vector3(p1).sub(p0);const float length=chord.length();
            if(length<1e-4f)continue;chord.divideScalar(length);const Vector3 out(chord.z,0,-chord.x);const float bulge=0; // Straight in the plane: the crown is the depth's scallops, not the outline's.
            auto at=[&](float t,Vector3& centre,Vector3& normal,float& h,float& under){centre=Vector3(p0).lerp(p1,t).addScaledVector(out,bulge*std::sin(pi*t));
                const Vector3 tangent=Vector3(chord).multiplyScalar(length).addScaledVector(out,bulge*pi*std::cos(pi*t)).normalize();normal.set(tangent.z,0,-tangent.x);
                h=thickness/2;under=thickness/2-(!root?.18f*(1-std::abs(2*t-1)):0.f);}; // Triangular teeth underneath, pointing down at each wall; the base straight.
            for(int step=0;step<steps;++step){Vector3 c0,n0,c1,n1;float h0,h1,u0,u1;at(float(step)/steps,c0,n0,h0,u0);at(float(step+1)/steps,c1,n1,h1,u1);
                for(const float side:{1.f,-1.f}){ // The outer face, and the inner a well's dark.
                    const Vector3 o0=Vector3(c0).addScaledVector(n0,side*rim/2),o1=Vector3(c1).addScaledVector(n1,side*rim/2),m0=Vector3(n0).multiplyScalar(side),m1=Vector3(n1).multiplyScalar(side);
                    const float shade=side>0?1.f:.5f;
                    triangle({Vector3(o0).setY(-u0),m0,shade},{Vector3(o1).setY(-u1),m1,shade},{Vector3(o1).setY(h1),m1,shade});
                    triangle({Vector3(o0).setY(-u0),m0,shade},{Vector3(o1).setY(h1),m1,shade},{Vector3(o0).setY(h0),m0,shade});}
                for(const float y:{1.f,-1.f}){const Vector3 up(0,y,0); // The lips.
                    const float e0=y>0?h0:u0,e1=y>0?h1:u1;
                    const Vector3 a0=Vector3(c0).addScaledVector(n0,rim/2).setY(y*e0),b0=Vector3(c0).addScaledVector(n0,-rim/2).setY(y*e0);
                    const Vector3 a1=Vector3(c1).addScaledVector(n1,rim/2).setY(y*e1),b1=Vector3(c1).addScaledVector(n1,-rim/2).setY(y*e1);
                    triangle({a0,up,1.8f},{a1,up,1.8f},{b1,up,1.8f});triangle({a0,up,1.8f},{b1,up,1.8f},{b0,up,1.8f});}}}}
    // Straight steel bars through the fin's depth: the beam across the base, the V from its middle, the hinge box.
    auto bar=[&](const Vector3& p0,const Vector3& p1,float gauge){const Vector3 along=Vector3(p1).sub(p0).normalize(),side(along.z,0,-along.x);const float h=thickness/2;
        for(const float d:{1.f,-1.f}){const Vector3 n=Vector3(side).multiplyScalar(d),a=Vector3(p0).addScaledVector(side,d*gauge/2),b=Vector3(p1).addScaledVector(side,d*gauge/2);
            triangle({Vector3(a).setY(-h),n,1.f},{Vector3(b).setY(-h),n,1.f},{Vector3(b).setY(h),n,1.f});triangle({Vector3(a).setY(-h),n,1.f},{Vector3(b).setY(h),n,1.f},{Vector3(a).setY(h),n,1.f});}
        for(const float y:{1.f,-1.f}){const Vector3 up(0,y,0),a0=Vector3(p0).addScaledVector(side,gauge/2).setY(y*h),b0=Vector3(p0).addScaledVector(side,-gauge/2).setY(y*h);
            const Vector3 a1=Vector3(p1).addScaledVector(side,gauge/2).setY(y*h),b1=Vector3(p1).addScaledVector(side,-gauge/2).setY(y*h);
            triangle({a0,up,1.8f},{a1,up,1.8f},{b1,up,1.8f});triangle({a0,up,1.8f},{b1,up,1.8f},{b0,up,1.8f});}};
    {const Vector3 left(outline[corners-1].x,0,outline[corners-1].y),right(outline[2].x,0,outline[2].y);bar(left,right,.09f);
        for(const float x:{-1.f,1.f}){bar(vTip,{x*boxHalf,0,boxTop},.07f);bar({x*rootHalf,0,bottom},{x*boxHalf,0,boxTop},.08f);}bar({-boxHalf,0,boxTop},{boxHalf,0,boxTop},.08f);}
    // The bearing plate inside the hinge box, nearer the root: across the box from side to side, through the fin's depth and half a metre
    // out below it (where V3 rests on the tower's arms), bored on the spindle for the hinge's pin from the booster.
    {std::vector<float> pp,pn,pc;auto put=[&](const Vector3& a,Vector3 b,Vector3 c,const Vector3& n){const auto cross=Vector3(b).sub(a).cross(Vector3(c).sub(a));if(cross.dot(n)<0)std::swap(b,c);
            for(const auto& v:{a,b,c}){pp.insert(pp.end(),{v.x,v.y,v.z});pn.insert(pn.end(),{n.x,n.y,n.z});pc.insert(pc.end(),{1,1,1});}};
        const float z=bottom+.35f*(boxTop-bottom),half=rootHalf+(boxHalf-rootHalf)*.35f-.04f,up=thickness/2,down=-thickness/2-.5f,bore=.12f,plate=.08f;
        std::vector<float> angles;const int sides=tier==0?24:12;for(int k=0;k<sides;++k)angles.push_back(2*pi*k/sides);
        for(const auto& c:std::array<Vector2,4>{{{half,up},{-half,up},{-half,down},{half,down}}})angles.push_back(std::atan2(c.y,c.x)<0?std::atan2(c.y,c.x)+2*pi:std::atan2(c.y,c.x));
        std::sort(angles.begin(),angles.end());
        auto edge=[&](float t){const float dx=std::cos(t),dy=std::sin(t);float r=1e9f;if(std::abs(dx)>1e-6f)r=std::min(r,(dx>0?half:-half)/dx);if(std::abs(dy)>1e-6f)r=std::min(r,(dy>0?up:down)/dy);return Vector2(r*dx,r*dy);};
        for(std::size_t k=0;k<angles.size();++k){const float t0=angles[k],t1=angles[(k+1)%angles.size()];const Vector2 o0=edge(t0),o1=edge(t1),i0(bore*std::cos(t0),bore*std::sin(t0)),i1(bore*std::cos(t1),bore*std::sin(t1));
            for(const float d:{1.f,-1.f}){const float zz=z+d*plate/2;const Vector3 n(0,0,d); // Its two faces.
                put({i0.x,i0.y,zz},{i1.x,i1.y,zz},{o1.x,o1.y,zz},n);put({i0.x,i0.y,zz},{o1.x,o1.y,zz},{o0.x,o0.y,zz},n);}
            const Vector3 in(-std::cos((t0+t1)/2),-std::sin((t0+t1)/2),0); // The bore.
            put({i0.x,i0.y,z-plate/2},{i1.x,i1.y,z-plate/2},{i1.x,i1.y,z+plate/2},in);put({i0.x,i0.y,z-plate/2},{i1.x,i1.y,z+plate/2},{i0.x,i0.y,z+plate/2},in);
            const Vector3 rim=Vector3(o1.y-o0.y,-(o1.x-o0.x),0).normalize(),outward=Vector3(rim).dot(Vector3((o0.x+o1.x)/2,(o0.y+o1.y)/2,0))<0?Vector3(rim).negate():rim; // Its edges.
            put({o0.x,o0.y,z-plate/2},{o1.x,o1.y,z-plate/2},{o1.x,o1.y,z+plate/2},outward);put({o0.x,o0.y,z-plate/2},{o1.x,o1.y,z+plate/2},{o0.x,o0.y,z+plate/2},outward);}
        auto m=BufferGeometry::create();m->setAttribute("position",FloatBufferAttribute::create(std::move(pp),3));m->setAttribute("normal",FloatBufferAttribute::create(std::move(pn),3));
        m->setAttribute("color",FloatBufferAttribute::create(std::move(pc),3));result.mount=m;}
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
