// Quadtree terrain without a GPU: the selection tiles the sphere exactly (4π, no overlaps), refines
// to the finest level under the camera, culls the far side, and nodes carry the source's heights.
#include "terrain.hpp"
#include "threepp/core/Object3D.hpp"
#include "threepp/math/Frustum.hpp"
#include "threepp/math/Plane.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/textures/Image.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>
#include <thread>
using namespace threepp;
namespace {
int failures=0;
void check(bool ok,const char* what){std::printf("%s %s\n",ok?"ok  ":"FAIL",what);if(!ok)++failures;}
// Updates until nothing is pending and the selection is stable.
void settle(albert::Terrain& t,const Vector3& camera,bool cull){
    std::size_t last=~0u;int stable=0;
    for(int i=0;i<20000&&stable<5;++i){t.update(camera,nullptr,cull);const auto n=t.drawn();stable=(n==last&&t.pending()==0)?stable+1:0;last=n;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
}
int main(){
    constexpr double radius=6378137,height=1000;
    Object3D parent;auto material=MeshBasicMaterial::create();
    albert::TerrainBody body;body.radius=radius;body.maxLevel=9;body.morphSeconds=0; // Final shapes at once (the morph has its own check).
    body.heights=[](const std::vector<double>& lat,const std::vector<double>&,int,std::vector<float>& out){for(std::size_t i=0;i<lat.size();++i)out[i]=static_cast<float>(height);return true;};
    albert::Terrain terrain(parent,material,body);
    const double sphere=4*std::numbers::pi;
    settle(terrain,Vector3(0,0,10),false);
    std::printf("far: %zu nodes, finest level %d, %.12f sr\n",terrain.drawn(),terrain.finestLevel(),terrain.coveredSolidAngle());
    check(terrain.drawn()==6&&std::abs(terrain.coveredSolidAngle()-sphere)<1e-9,"Far camera: the six faces cover the sphere");
    const Vector3 near(0,0,static_cast<float>(1+100/radius));
    settle(terrain,near,false);
    std::printf("near: %zu nodes, finest level %d, %.12f sr\n",terrain.drawn(),terrain.finestLevel(),terrain.coveredSolidAngle());
    check(terrain.finestLevel()==body.maxLevel,"Refined to the finest level under the camera");
    check(std::abs(terrain.coveredSolidAngle()-sphere)<1e-8,"Selected nodes tile the sphere exactly (no gaps, no overlaps)");
    double worst=0;std::size_t meshes=0;
    for(const auto& child:parent.children){auto* mesh=dynamic_cast<Mesh*>(child);if(!mesh||!mesh->visible)continue;++meshes;
        const auto* p=mesh->geometry()->getAttribute<float>("position");
        for(int i=0;i<albert::Terrain::posts*albert::Terrain::posts;++i){const double x=p->getX(i)+mesh->position.x,y=p->getY(i)+mesh->position.y,z=p->getZ(i)+mesh->position.z;
            worst=std::max(worst,std::abs(std::sqrt(x*x+y*y+z*z)-(1+height/radius))*radius);}}
    std::printf("vertex radius error %.3f m over %zu visible nodes\n",worst,meshes);
    check(meshes==terrain.drawn()&&worst<1.,"Visible nodes are the selection, at the source's height (float placement)");
    settle(terrain,near,true);
    std::printf("culled: %zu nodes, %.6f sr\n",terrain.drawn(),terrain.coveredSolidAngle());
    check(terrain.coveredSolidAngle()<sphere/2&&terrain.coveredSolidAngle()>.01,"Horizon culling keeps the near side only");
    { // Provisional nodes (finer data downloading) are rebuilt once the source's generation advances.
        std::atomic<std::uint64_t> generation{0};Object3D other;albert::TerrainBody b=body;b.maxLevel=2;
        b.heights=[&generation](const std::vector<double>& lat,const std::vector<double>&,int,std::vector<float>& out){
            const bool ready=generation.load()>0;for(std::size_t i=0;i<lat.size();++i)out[i]=ready?2000.f:500.f;return ready;};
        b.generation=[&generation]{return generation.load();};
        albert::Terrain provisional(other,material,b);
        auto heightOf=[&]{for(const auto& child:other.children)if(auto* m=dynamic_cast<Mesh*>(child);m&&m->visible){const auto* p=m->geometry()->getAttribute<float>("position");
            const double x=p->getX(0)+m->position.x,y=p->getY(0)+m->position.y,z=p->getZ(0)+m->position.z;return (std::sqrt(x*x+y*y+z*z)-1)*radius;}return -1.;};
        settle(provisional,Vector3(0,0,10),false);const double before=heightOf();generation=1;settle(provisional,Vector3(0,0,10),false);const double after=heightOf();
        std::printf("provisional %.1f m, rebuilt %.1f m\n",before,after);
        check(std::abs(before-500)<1&&std::abs(after-2000)<1,"Provisional nodes rebuilt when finer data arrives");
    }
    { // The ground under the camera and elsewhere: the drawn relief's radius, on every face.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=6;b.scale=.25;
        auto relief=[](double lat,double lon){return 1500+1200*std::sin(2*lat)*std::cos(3*lon);};
        b.heights=[&relief](const std::vector<double>& lat,const std::vector<double>& lon,int,std::vector<float>& out){for(std::size_t i=0;i<lat.size();++i)out[i]=static_cast<float>(relief(lat[i],lon[i]));return true;};
        albert::Terrain varied(other,material,b);check(!varied.ground(Vector3(0,0,1)),"No ground before anything is drawn");
        const Vector3 eye(.1f,.05f,.25f*static_cast<float>(1+300/radius));settle(varied,eye,false);double worst=0;
        for(const Vector3 d:{Vector3(.1f,.05f,1),Vector3(1,.3f,-.2f),Vector3(-.7f,-.6f,.2f),Vector3(.2f,1,.4f),Vector3(-.3f,-1,-.5f),Vector3(.4f,.1f,-1),Vector3(0,0,1)}){
            const Vector3 u=d.clone().normalize();const double lat=std::asin(u.y),lon=std::remainder(std::atan2(u.z,-u.x)-std::numbers::pi,2*std::numbers::pi);
            const auto g=varied.ground(d);if(!g){worst=1e9;break;}worst=std::max(worst,std::abs(*g/.25-(1+relief(lat,lon)/radius))*radius);}
        std::printf("ground error %.3f m (finest level %d)\n",worst,varied.finestLevel());
        check(worst<2,"Ground lookup follows the drawn relief on every face");
    }
    { // Regional imagery: nodes wholly inside the bounds take its material and coordinates over it (uv2
      // keeps the global ones); the others keep the body's; clearing restores every node.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=6;albert::Terrain t(other,material,b);auto detail=MeshBasicMaterial::create();
        const Vector3 eye(0,0,static_cast<float>(1+300/radius));settle(t,eye,false); // Toward +z: u 0.25, v 0.5.
        const Vector4 bounds(.2f,.45f,.1f,.1f);t.detail(detail,bounds);t.update(eye,nullptr,false);
        auto audit=[&](bool regional){std::size_t inside=0,wrong=0;
            for(const auto& child:other.children){auto* m=dynamic_cast<Mesh*>(child);if(!m||!m->visible)continue;
                const auto* uv=m->geometry()->getAttribute<float>("uv");const auto* global=m->geometry()->getAttribute<float>("uv2");const bool mine=m->material()==detail;inside+=mine;
                if(mine!=regional&&mine)++wrong;
                for(int i=0;i<uv->count();++i){const float u=mine?bounds.x+uv->getX(i)*bounds.z:uv->getX(i),v=mine?bounds.y+uv->getY(i)*bounds.w:uv->getY(i);
                    if(std::abs(u-global->getX(i))>1e-5f||std::abs(v-global->getY(i))>1e-5f||(mine&&(uv->getX(i)<-1e-4f||uv->getX(i)>1.0001f||uv->getY(i)<-1e-4f||uv->getY(i)>1.0001f))){++wrong;break;}}
                if(!mine&&m->material()!=material)++wrong;}
            return std::pair{inside,wrong};};
        const auto [inside,wrong]=audit(true);std::printf("regional imagery: %zu of %zu drawn nodes, %zu wrong\n",inside,t.drawn(),wrong);
        check(inside>0&&inside==t.detailed()&&inside<t.drawn()&&wrong==0,"Regional imagery on the nodes inside its bounds, over its coordinates");
        // Quality changes can release the region after update(), before this frame is drawn.
        t.detail(nullptr,std::nullopt);const auto [left,stale]=audit(false);
        check(left==0&&stale==0&&t.detailed()==0,"Clearing the region restores visible nodes before another update");
    }
    { // Morph: children replacing a drawn parent start on its surface (their even posts are the parent's)
      // and rise to their own heights (here 500 m per level) once the morph has run.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=1;b.morphSeconds=.2;
        b.heights=[](const std::vector<double>& lat,const std::vector<double>&,int level,std::vector<float>& out){for(std::size_t i=0;i<lat.size();++i)out[i]=static_cast<float>(level*500);return true;};
        albert::Terrain t(other,material,b);settle(t,Vector3(0,0,10),false);
        auto heights=[&]{std::vector<double> h; // Per drawn node: the height of its even posts (-1 if they differ).
            for(const auto& child:other.children){auto* m=dynamic_cast<Mesh*>(child);if(!m||!m->visible)continue;const auto* p=m->geometry()->getAttribute<float>("position");double lo=1e9,hi=-1e9;
                for(int j=0;j<albert::Terrain::posts;j+=2)for(int i=0;i<albert::Terrain::posts;i+=2){const int v=j*albert::Terrain::posts+i;
                    const double r=std::sqrt(std::pow(double(p->getX(v))+m->position.x,2)+std::pow(double(p->getY(v))+m->position.y,2)+std::pow(double(p->getZ(v))+m->position.z,2));lo=std::min(lo,(r-1)*radius);hi=std::max(hi,(r-1)*radius);}
                h.push_back(hi-lo<2?(hi+lo)/2:-1);}
            return h;};
        for(int i=0;i<20000&&t.finestLevel()==0;++i){t.update(Vector3(0,0,1.3f),nullptr,false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        const auto first=heights();std::this_thread::sleep_for(std::chrono::milliseconds(300));t.update(Vector3(0,0,1.3f),nullptr,false);const auto last=heights();
        const bool started=t.finestLevel()==1&&std::all_of(first.begin(),first.end(),[](double h){return std::abs(h)<1;});
        const auto risen=std::count_if(last.begin(),last.end(),[](double h){return std::abs(h-500)<1;});
        const bool done=risen>=4&&std::all_of(last.begin(),last.end(),[](double h){return std::abs(h)<1||std::abs(h-500)<1;});
        std::printf("morph: %zu nodes on the parents' surface when shown, %ld risen to their own after it\n",first.size(),static_cast<long>(risen));
        check(started&&done,"Split children morph from the parent's surface to their own heights");
    }
    { // Review: frustum culling against each node's true bound: a root face's corner lies 0.919 from its
      // centre, beyond sin(angular radius) = 0.857. A view bounded by a plane just past the cube corner,
      // facing away from the +x face's centre, still shows that face's corner, so a +x node is drawn.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=3;albert::Terrain t(other,material,b);
        const Vector3 corner=Vector3(1,1,1).normalize(),eye=corner.clone().multiplyScalar(1.0001f),n=corner.clone().sub(Vector3(1,0,0)).normalize();
        Frustum frustum(Plane(n,-n.dot(corner)+.02f),Plane(Vector3(1,0,0),10),Plane(Vector3(-1,0,0),10),Plane(Vector3(0,1,0),10),Plane(Vector3(0,-1,0),10),Plane(Vector3(0,0,1),10));
        std::size_t last=~0u;int stable=0;for(int i=0;i<20000&&stable<5;++i){t.update(eye,&frustum,true);stable=(t.drawn()==last&&t.pending()==0)?stable+1:0;last=t.drawn();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        bool face=false;for(const auto& child:other.children)if(auto* m=dynamic_cast<Mesh*>(child);m&&m->visible&&m->position.x>std::max(m->position.y,m->position.z))face=true;
        std::printf("past a cube corner: %zu nodes drawn, +x face %s\n",t.drawn(),face?"drawn":"culled");
        check(face,"A face whose corner is in view is not culled (conservative node bounds)");
    }
    { // Review: a height source that throws (a corrupt cache piece) must not terminate the worker: the
      // nodes are built without its heights, provisional.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=1;
        b.heights=[](const std::vector<double>&,const std::vector<double>&,int,std::vector<float>&)->bool{throw std::runtime_error("corrupt piece");};
        albert::Terrain t(other,material,b);settle(t,Vector3(0,0,10),false);
        check(t.drawn()==6&&std::abs(t.coveredSolidAngle()-sphere)<1e-9,"A throwing height source leaves the body covered");
    }
    { // Mars nodes straddling 88°: MOLA stops there and the grid alone completes them (review: only the
      // first point was checked, so such nodes stayed provisional and were rebuilt on every download).
        constexpr double deg=std::numbers::pi/180;const auto heights=albert::marsTerrainHeights(Image(ImageData(std::vector<float>(4*64*32,-1000.f)),64,32));
        std::vector<double> lat{87*deg,89*deg},lon{0,0};std::vector<float> out(2);const bool complete=heights(lat,lon,7,out);
        std::printf("polar node: complete %d, %.3f %.3f m\n",complete,out[0],out[1]);check(complete&&std::abs(out[0]+1000)<.01&&std::abs(out[1]+1000)<.01,"Mars node across 88 degrees: grid heights, complete");
    }
    std::printf(failures?"%d failure(s)\n":"terrain lod ok\n",failures);return failures?1:0;
}
