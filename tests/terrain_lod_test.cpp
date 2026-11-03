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
#include <condition_variable>
#include <cstdio>
#include <mutex>
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
    { // Move back and forth across the refinement boundary after the children
      // have appeared. Inspect the real selected meshes, not a separate policy:
      // camera jitter must not swap them or create overlaps/gaps in coverage.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=1;
        b.heights=[](const std::vector<double>&,const std::vector<double>&,int,std::vector<float>& out){std::fill(out.begin(),out.end(),0.f);return true;};
        albert::Terrain t(other,material,b);
        auto front=[&]{unsigned count=0;for(auto* child:other.children)if(auto* m=dynamic_cast<Mesh*>(child);m&&m->visible&&m->position.z>.8f)++count;return count;};
        // A cube face has corner direction (1,1,1)/sqrt(3). Its chord diameter
        // is known independently of the quadtree's cached shapes.
        const double chord=2*std::sqrt(2./3.),entry=1+2.25*chord,exit=1+2.4*chord;
        settle(t,Vector3(0,0,6),false);check(front()==1,"The distant front face starts with one coarse mesh");
        settle(t,Vector3(0,0,float(entry-.02)),false);check(front()==4,"Approaching the front face selects its four children");
        bool stable=true;
        for(int i=0;i<60;++i){
            t.update(Vector3(0,0,float(entry+(i%2?.01:-.01))),nullptr,false);
            stable=stable&&front()==4&&std::abs(t.coveredSolidAngle()-sphere)<1e-9;
        }
        check(stable,"Jitter across the split boundary retains the children and exact sphere coverage");
        t.update(Vector3(0,0,float(exit-.02)),nullptr,false);
        check(front()==4,"Retreating inside the hysteresis band retains fine geometry");
        t.update(Vector3(0,0,float(exit+.02)),nullptr,false);
        check(front()==1,"Retreating beyond the outer threshold restores the parent");
        t.update(Vector3(0,0,float(entry+.01)),nullptr,false);
        check(front()==1,"Approaching inside the band does not immediately split the parent again");
        settle(t,Vector3(0,0,float(entry-.02)),false);t.refinement(0);t.update(Vector3(0,0,1.001f),nullptr,false);
        check(front()==1&&t.finestLevel()==0&&std::abs(t.coveredSolidAngle()-sphere)<1e-9,"An explicit detail cap overrides hysteresis and keeps a complete surface");
    }
    { // A blocked height source leaves several old-view jobs waiting. Moving
      // to the opposite face must discard those jobs, not spend the worker on
      // terrain that is now behind the horizon. At most the active job finishes.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=1;
        std::mutex mutex;std::condition_variable wake;bool started=false,released=false;std::atomic<int> oldLoads{0};
        b.heights=[&](const std::vector<double>&,const std::vector<double>& lon,int level,std::vector<float>& out){
            if(level>0&&std::sin(lon[lon.size()/2])<0){
                ++oldLoads;std::unique_lock lock(mutex);started=true;wake.notify_all();wake.wait(lock,[&]{return released;});
            }
            std::fill(out.begin(),out.end(),0.f);return true;
        };
        albert::Terrain t(other,material,b);t.refinement(0);settle(t,Vector3(0,0,10),false);
        t.refinement(1);t.update(Vector3(0,0,1.001f));
        bool blocked=false;{std::unique_lock lock(mutex);blocked=wake.wait_for(lock,std::chrono::seconds(2),[&]{return started;});}
        const auto before=t.pending();t.update(Vector3(0,0,-1.001f));const auto after=t.pending();
        check(blocked&&before>1&&after<=5,"A camera jump replaces waiting old-view terrain with the new view's jobs");
        {std::lock_guard lock(mutex);released=true;}wake.notify_all();
        settle(t,Vector3(0,0,-1.001f),true);
        check(oldLoads==1&&t.finestLevel()==1&&t.drawn()>0,"Only the old view's active job finishes; the new ground still refines");
    }
    { // An isolated odd-post spike is absent from every decimated tier, so all
      // three have nearly the same measured error. This exposes retention
      // checks that mistakenly reject an intermediate tier before reaching the
      // one already drawn. Exaggerated height isolates the decision from curvature.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=0;
        constexpr double spike=1e6;
        b.heights=[](const std::vector<double>&,const std::vector<double>&,int,std::vector<float>& out){
            std::fill(out.begin(),out.end(),0.f);constexpr int ring=albert::Terrain::posts+2;
            out[(ring/2+1)*ring+ring/2+1]=float(spike);return true;
        };
        albert::Terrain t(other,material,b);const Vector3 eye(0,0,10);settle(t,eye,false);
        Mesh* front=nullptr;for(auto* child:other.children)if(auto* m=dynamic_cast<Mesh*>(child);m&&m->position.z>.99f)front=m;
        if(front){
            const double range=eye.distanceTo(front->position)-front->geometry()->boundingSphere->radius;
            const float pixels=float(range/(spike/radius)); // About one pixel of peak error.
            auto indices=[&]{return front->geometry()->getIndex()->count();};
            t.screenSpace(pixels*.5f);t.update(eye,nullptr,false);
            check(indices()==576,"A subpixel diagnostic peak allows the coarsest index tier");
            t.screenSpace(pixels);t.update(eye,nullptr,false);
            check(indices()==576,"An existing coarse index tier survives intermediate-tier retention checks");
            t.screenSpace(pixels*1.5f);t.update(eye,nullptr,false);
            check(indices()>576,"Exceeding the retention tolerance restores finer indices");
            const auto fine=indices();t.screenSpace(pixels);t.update(eye,nullptr,false);
            check(indices()==fine,"A retained finer tier does not immediately coarsen inside the band");
            t.screenSpace(pixels*.5f);t.update(eye,nullptr,false);
            check(indices()==576,"Falling below the entry tolerance restores the coarse indices");
        }else check(false,"The diagnostic terrain's front face exists");
    }
    { // Hiding a body's terrain also drops waiting jobs, keeping the cache and
      // only the one build already running. Showing it again must refill safely.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=1;
        std::mutex mutex;std::condition_variable wake;bool started=false,released=false;std::atomic<int> fineLoads{0};
        b.heights=[&](const std::vector<double>&,const std::vector<double>&,int level,std::vector<float>& out){
            if(level>0){++fineLoads;std::unique_lock lock(mutex);started=true;wake.notify_all();wake.wait(lock,[&]{return released;});}
            std::fill(out.begin(),out.end(),0.f);return true;
        };
        albert::Terrain t(other,material,b);t.refinement(0);settle(t,Vector3(0,0,10),false);
        t.refinement(1);t.update(Vector3(0,0,1.001f));
        bool blocked=false;{std::unique_lock lock(mutex);blocked=wake.wait_for(lock,std::chrono::seconds(2),[&]{return started;});}
        const auto before=t.pending();t.hide();const auto hidden=t.pending();
        check(blocked&&before>1&&hidden==1&&t.drawn()==0,"Hiding terrain discards waiting work and leaves only its active build");
        {std::lock_guard lock(mutex);released=true;}wake.notify_all();
        // Wait for that build to end without requesting any more terrain.
        bool finished=false;for(int i=0;i<2000&&!finished;++i){
            t.refinement(0);t.update(Vector3(0,0,10),nullptr,false);finished=t.pending()==0;
            if(!finished)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(finished&&fineLoads==1&&t.drawn()==6,"Hidden work is not rebuilt or duplicated when the cached globe returns");
        t.refinement(1);settle(t,Vector3(0,0,1.001f),true);
        check(t.finestLevel()==1&&t.pending()==0,"Returning to the ground can request previously discarded children");
    }
    { // A simplified parent is actually drawn with stride 8. Its children must
      // begin on those 128 triangles, not on the unused full-resolution posts.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=1;b.morphSeconds=.4;
        albert::Terrain t(other,material,b);t.screenSpace(1,1);t.refinement(0);settle(t,Vector3(0,0,10),false);
        Mesh* parent=nullptr;for(const auto& child:other.children)if(auto* m=dynamic_cast<Mesh*>(child);m&&m->position.z>.99f)parent=m;
        check(parent&&parent->geometry()->getIndex()->count()==576,"Distant smooth terrain uses the reduced index buffer");
        struct Plane {Vector3 n;double d;};std::vector<Plane> planes;
        if(parent){const auto* p=parent->geometry()->getAttribute<float>("position");const auto* indices=parent->geometry()->getIndex();
            auto point=[&](int v){return Vector3(p->getX(v),p->getY(v),p->getZ(v)).add(parent->position);};
            for(int i=0;i<384;i+=3){const auto a=point(indices->getX(i)),c=point(indices->getX(i+1)),d=point(indices->getX(i+2));
                const auto n=Vector3(c).sub(a).cross(Vector3(d).sub(a)).normalize();planes.push_back({n,n.dot(a)});}}
        // Hold that decimation while children are built, even after moving the
        // eye near the parent (otherwise the pixel budget refines it first).
        t.screenSpace(1,1e12f);t.refinement(1);
        for(int i=0;i<20000&&t.finestLevel()==0;++i){t.update(Vector3(0,0,1.3f),nullptr,false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        double worst=0;unsigned children=0;
        for(const auto& child:other.children)if(auto* m=dynamic_cast<Mesh*>(child);m&&m->visible&&m->position.z>.6f){++children;const auto* p=m->geometry()->getAttribute<float>("position");
            for(int j=0;j<albert::Terrain::posts;j+=7)for(int i=0;i<albert::Terrain::posts;i+=7){const int at=j*albert::Terrain::posts+i;const auto q=Vector3(p->getX(at),p->getY(at),p->getZ(at)).add(m->position);
                double error=1e9;for(const auto& plane:planes)error=std::min(error,std::abs(plane.n.dot(q)-plane.d)*radius);worst=std::max(worst,error);}}
        std::printf("decimated parent morph: %u children, plane error %.3f m\n",children,worst);
        check(children==4&&worst<1,"Children start on the triangles that their decimated parent actually drew");
        t.refinement(0);settle(t,Vector3(0,0,1.3f),false);
        check(t.finestLevel()==0&&t.drawn()==6&&std::abs(t.coveredSolidAngle()-sphere)<1e-9,"Reducing detail keeps a complete surface and reuses the cache");
    }
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
    { // New DEM data must not change the lighting before its geometry starts
      // moving. A sloped diagnostic surface makes a normal discontinuity visible.
        std::atomic<std::uint64_t> generation{0};Object3D other;albert::TerrainBody b=body;b.maxLevel=0;b.morphSeconds=.05;
        b.generation=[&]{return generation.load();};
        b.heights=[&](const std::vector<double>& lat,const std::vector<double>& lon,int,std::vector<float>& out){
            const bool ready=generation.load()>0;for(std::size_t i=0;i<lat.size();++i)out[i]=ready?float(2000+30000*std::sin(4*lat[i])*std::cos(2*lon[i])):0.f;return ready;};
        albert::Terrain t(other,material,b);const Vector3 eye(0,0,10);settle(t,eye,false);
        auto front=[&]()->Mesh*{for(auto* child:other.children)if(auto* m=dynamic_cast<Mesh*>(child);m&&m->visible&&m->position.z>.9f)return m;return nullptr;};
        auto normal=[](const Mesh& mesh){const auto* n=mesh.geometry()->getAttribute<float>("normal");const int i=albert::Terrain::posts/2*(albert::Terrain::posts+1);return Vector3(n->getX(i),n->getY(i),n->getZ(i));};
        auto* old=front();auto previous=old?old->geometry():nullptr;const auto before=old?normal(*old):Vector3();generation=1;
        bool replaced=false;for(int i=0;i<20000;++i){t.update(eye,nullptr,false);if(front()&&front()->geometry()!=previous){replaced=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        const auto first=front()?normal(*front()):Vector3();
        check(replaced&&Vector3(first).sub(before).length()<1e-6f,"A newly adopted DEM starts with the previous lighting normal");
        std::this_thread::sleep_for(std::chrono::milliseconds(80));t.update(eye,nullptr,false);
        const auto after=front()?normal(*front()):Vector3();
        check(std::abs(after.length()-1)<1e-6f&&Vector3(after).sub(before).length()>.01f,"Lighting normals finish normalized on the new relief");
    }
    { // Disabling fine relief keeps cached children, but camera/scenery grounding
      // must follow the visible parent rather than those hidden fine heights.
        Object3D other;albert::TerrainBody b=body;b.maxLevel=1;
        b.heights=[](const std::vector<double>& lat,const std::vector<double>&,int level,std::vector<float>& out){
            for(std::size_t i=0;i<lat.size();++i)out[i]=500.f+1000.f*level;return true;};
        albert::Terrain t(other,material,b);const Vector3 eye(0,0,1.0003f);settle(t,eye,false);
        const auto fine=t.ground(Vector3(0,0,1));t.refinement(0);t.update(eye,nullptr,false);const auto coarse=t.ground(Vector3(0,0,1));
        check(fine&&coarse&&std::abs((*fine-1)*radius-1500)<1&&std::abs((*coarse-1)*radius-500)<1,"Ground lookup ignores hidden finer cached nodes");
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
