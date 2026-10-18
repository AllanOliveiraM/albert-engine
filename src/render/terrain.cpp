#include "terrain.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/math/Sphere.hpp"
#include "threepp/math/Frustum.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <numbers>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace threepp;
namespace albert {
namespace {
constexpr int posts=Terrain::posts;
constexpr double pi=std::numbers::pi;
struct Vec {double x=0,y=0,z=0;
    Vec operator+(const Vec& o)const{return{x+o.x,y+o.y,z+o.z};}Vec operator-(const Vec& o)const{return{x-o.x,y-o.y,z-o.z};}Vec operator*(double k)const{return{x*k,y*k,z*k};}
    [[nodiscard]] double dot(const Vec& o)const{return x*o.x+y*o.y+z*o.z;}[[nodiscard]] Vec cross(const Vec& o)const{return{y*o.z-z*o.y,z*o.x-x*o.z,x*o.y-y*o.x};}
    [[nodiscard]] double length()const{return std::sqrt(dot(*this));}[[nodiscard]] Vec unit()const{return *this*(1/length());}};
// Cube faces: normal, then the axes of a and b, with a × b = normal (counter-clockwise from outside).
constexpr std::array<std::array<Vec,3>,6> faces{{{Vec{1,0,0},Vec{0,0,-1},Vec{0,1,0}},{Vec{-1,0,0},Vec{0,0,1},Vec{0,1,0}},{Vec{0,1,0},Vec{1,0,0},Vec{0,0,-1}},
    {Vec{0,-1,0},Vec{1,0,0},Vec{0,0,1}},{Vec{0,0,1},Vec{1,0,0},Vec{0,1,0}},{Vec{0,0,-1},Vec{-1,0,0},Vec{0,1,0}}}};
struct Key {int face=0,level=0;std::uint32_t x=0,y=0;auto operator<=>(const Key&)const=default;};
// Unit direction of face coordinates a, b in [-1, 1] (equal-angle: tan warp).
Vec direction(int face,double a,double b){const auto& f=faces[static_cast<std::size_t>(face)];return (f[0]+f[1]*std::tan(a*pi/4)+f[2]*std::tan(b*pi/4)).unit();}
struct Extent {double a0,b0,size;};
Extent extent(const Key& k){const double size=2./static_cast<double>(1u<<k.level);return{-1+k.x*size,-1+k.y*size,size};}
// Solid angle of the spherical triangle a, b, c (Van Oosterom and Strackee).
double triangle(const Vec& a,const Vec& b,const Vec& c){return 2*std::atan2(std::abs(a.dot(b.cross(c))),1+a.dot(b)+b.dot(c)+c.dot(a));}
struct Built {Key key;Vec centre;float radius=0;std::vector<float> positions,normals,uvs;std::vector<unsigned> index;bool complete=true;std::uint64_t generation=0;};
std::uint64_t pack(const Key& k){return static_cast<std::uint64_t>(k.face)<<61|static_cast<std::uint64_t>(k.level)<<56|static_cast<std::uint64_t>(k.x)<<28|k.y;} // Levels ≤ 27.
struct Shape {Vec centre;double radius=0;std::array<Vec,4> corners;}; // Angular radius (radians).
Shape shapeOf(const Key& k){
    const auto e=extent(k);Shape s;s.centre=direction(k.face,e.a0+e.size/2,e.b0+e.size/2);
    const double a[4]{e.a0,e.a0+e.size,e.a0+e.size,e.a0},b[4]{e.b0,e.b0,e.b0+e.size,e.b0+e.size};
    for(std::size_t i=0;i<4;++i){s.corners[i]=direction(k.face,a[i],b[i]);s.radius=std::max(s.radius,std::acos(std::clamp(s.centre.dot(s.corners[i]),-1.,1.)));}
    return s;
}
}
struct Terrain::Impl {
    // Provisional: from coarser data, at that generation. Detail: the region's imagery applied for that version.
    struct Node {std::shared_ptr<Mesh> mesh;unsigned lastUsed=0;bool provisional=false;std::uint64_t generation=0;unsigned detailVersion=0;bool detailed=false;};
    Object3D& parent;std::shared_ptr<Material> material;TerrainBody body;
    std::unordered_map<std::uint64_t,Node> nodes;std::unordered_map<std::uint64_t,Shape> shapes;std::unordered_set<std::uint64_t> queued;
    std::vector<std::pair<double,Key>> queue; // (priority, key): lower first.
    std::vector<Built> done;mutable std::mutex lock;std::condition_variable wake;bool stop=false;std::thread worker;
    std::vector<Key> selected;unsigned frame=0;int finest=0;double solidAngle=0;
    std::shared_ptr<Material> detailMaterial;std::optional<Vector4> detailBounds;unsigned detailVersion=1;
    // The region's material and coordinates on a node wholly inside it (the date line: its u unwrapped
    // by whole turns), else the body's; uv2 holds the global coordinates.
    void applyDetail(Node& n){
        auto* uv=n.mesh->geometry()->getAttribute<float>("uv");const auto* global=n.mesh->geometry()->getAttribute<float>("uv2");const int count=global->count();
        bool inside=false;float shift=0;
        if(detailMaterial&&detailBounds){const auto& b=*detailBounds;float u0=1e9f,u1=-1e9f,v0=1e9f,v1=-1e9f;
            for(int i=0;i<count;++i){u0=std::min(u0,global->getX(i));u1=std::max(u1,global->getX(i));v0=std::min(v0,global->getY(i));v1=std::max(v1,global->getY(i));}
            shift=std::round((u0+u1)/2-(b.x+b.z/2));inside=u0-shift>=b.x&&u1-shift<=b.x+b.z&&v0>=b.y&&v1<=b.y+b.w;
            if(inside)for(int i=0;i<count;++i)uv->setXY(i,(global->getX(i)-shift-b.x)/b.z,(global->getY(i)-b.y)/b.w);}
        if(!inside&&n.detailed)for(int i=0;i<count;++i)uv->setXY(i,global->getX(i),global->getY(i));
        if(inside||n.detailed)uv->needsUpdate();
        n.mesh->setMaterial(inside?detailMaterial:material);n.detailed=inside;n.detailVersion=detailVersion;
    }
    Impl(Object3D& p,std::shared_ptr<Material> m,TerrainBody b):parent(p),material(std::move(m)),body(std::move(b)){worker=std::thread([this]{run();});}
    ~Impl(){{std::lock_guard l(lock);stop=true;}wake.notify_all();worker.join();for(auto& [k,n]:nodes)parent.remove(*n.mesh);}
    void run(){
        for(;;){
            Key key;
            {std::unique_lock l(lock);wake.wait(l,[this]{return stop||!queue.empty();});if(stop)return;
             const auto best=std::min_element(queue.begin(),queue.end());key=best->second;queue.erase(best);}
            Built b=build(key);
            std::lock_guard l(lock);done.push_back(std::move(b)); // Still queued until adopted: no second build.
        }
    }
    // Heights on (posts + 2)² points (a ring beyond the node for its edge normals), the node's
    // vertices relative to its centre, normals, texture coordinates and skirts.
    Built build(const Key& k)const{
        const auto e=extent(k);constexpr int m=posts+2;const double step=e.size/(posts-1);
        std::vector<Vec> dirs(m*m);std::vector<double> lat(m*m),lon(m*m),phi(m*m);
        for(int j=0;j<m;++j)for(int i=0;i<m;++i){
            const auto at=static_cast<std::size_t>(j*m+i);const Vec d=direction(k.face,e.a0+(i-1)*step,e.b0+(j-1)*step);dirs[at]=d;
            const double parametric=std::asin(std::clamp(d.y,-1.,1.));phi[at]=std::atan2(d.z,-d.x); // The globe's u = φ / 2π.
            lat[at]=std::atan2(std::sin(parametric),body.flattening*std::cos(parametric));lon[at]=std::remainder(phi[at]-pi,2*pi);
        }
        Built b;b.key=k;b.generation=body.generation?body.generation():0;
        std::vector<float> h(m*m,0.f);if(body.heights)b.complete=body.heights(lat,lon,k.level,h);
        std::vector<Vec> p(m*m);for(std::size_t i=0;i<p.size();++i)p[i]=dirs[i]*(1+h[i]/body.radius);
        const auto mid=static_cast<std::size_t>((posts/2+1)*m+posts/2+1);b.centre=p[mid];const double centrePhi=phi[mid];
        const double skirt=std::max(e.size*.03,2e3/body.radius); // Below any step between levels.
        const std::size_t count=static_cast<std::size_t>(posts*posts+4*posts);b.positions.reserve(3*count);b.normals.reserve(3*count);b.uvs.reserve(2*count);
        auto vertex=[&](int i,int j,double drop){
            const auto at=static_cast<std::size_t>(j*m+i);const Vec q=(p[at]-dirs[at]*drop-b.centre)*body.scale;
            for(double v:{q.x,q.y,q.z})b.positions.push_back(static_cast<float>(v));
            const Vec n=(p[at+1]-p[at-1]).cross(p[at+m]-p[at-m]).unit();for(double v:{n.x,n.y,n.z})b.normals.push_back(static_cast<float>(v));
            const double u=(centrePhi+std::remainder(phi[at]-centrePhi,2*pi))/(2*pi); // Unwrapped across the date line (textures repeat).
            b.uvs.push_back(static_cast<float>(u));b.uvs.push_back(static_cast<float>(lat[at]/pi+.5));
            b.radius=std::max(b.radius,static_cast<float>(q.length()));
        };
        for(int j=1;j<=posts;++j)for(int i=1;i<=posts;++i)vertex(i,j,0);
        auto grid=[](int i,int j){return static_cast<unsigned>((j-1)*posts+(i-1));};
        b.index.reserve(static_cast<std::size_t>(6*(posts-1)*(posts+3)));
        for(int j=1;j<posts;++j)for(int i=1;i<posts;++i){const unsigned a=grid(i,j),c=grid(i+1,j),d=grid(i+1,j+1),f=grid(i,j+1);b.index.insert(b.index.end(),{a,c,d,a,d,f});}
        // Skirts: each edge dropped, wound to face outward like the edge they hang from.
        const std::array<std::array<int,4>,4> edges{{{1,1,1,0},{posts,1,0,1},{posts,posts,-1,0},{1,posts,0,-1}}}; // Start, step: counter-clockwise.
        for(const auto& [i0,j0,di,dj]:edges){
            const unsigned first=static_cast<unsigned>(b.positions.size()/3);
            for(int s=0;s<posts;++s)vertex(i0+di*s,j0+dj*s,skirt);
            for(int s=0;s+1<posts;++s){const unsigned top0=grid(i0+di*s,j0+dj*s),top1=grid(i0+di*(s+1),j0+dj*(s+1)),low0=first+static_cast<unsigned>(s),low1=low0+1;
                b.index.insert(b.index.end(),{top0,low0,low1,top0,low1,top1});}
        }
        return b;
    }
    void adopt(){
        std::vector<Built> ready;{std::lock_guard l(lock);ready.swap(done);for(const auto& b:ready)queued.erase(pack(b.key));}
        for(auto& b:ready){
            auto g=BufferGeometry::create();
            g->setAttribute("position",FloatBufferAttribute::create(std::move(b.positions),3));g->setAttribute("normal",FloatBufferAttribute::create(std::move(b.normals),3));
            g->setAttribute("uv",FloatBufferAttribute::create(b.uvs,2));g->setAttribute("uv2",FloatBufferAttribute::create(std::move(b.uvs),2));g->setIndex(std::move(b.index));
            g->boundingSphere=Sphere(Vector3(),b.radius);const Vec at=b.centre*body.scale;
            auto mesh=Mesh::create(g,material);mesh->position.set(static_cast<float>(at.x),static_cast<float>(at.y),static_cast<float>(at.z));
            mesh->castShadow=false;mesh->receiveShadow=true;mesh->renderOrder=0;
            auto& node=nodes[pack(b.key)];
            if(node.mesh){mesh->visible=node.mesh->visible;parent.remove(*node.mesh);node.mesh->geometry()->dispose();}else mesh->visible=false;
            parent.add(mesh);node.mesh=mesh;node.lastUsed=frame;node.provisional=!b.complete;node.generation=b.generation;node.detailVersion=0;node.detailed=false;
        }
    }
    const Shape& shape(const Key& k){const auto key=pack(k);if(const auto it=shapes.find(key);it!=shapes.end())return it->second;return shapes.emplace(key,shapeOf(k)).first->second;}
    Node* node(const Key& k){const auto it=nodes.find(pack(k));return it==nodes.end()?nullptr:&it->second;}
    void request(const Key& k,double priority,bool rebuild=false){
        std::lock_guard l(lock);if((!rebuild&&nodes.count(pack(k)))||!queued.insert(pack(k)).second)return;queue.emplace_back(priority,k);wake.notify_one();
    }
    // Hidden behind the body's horizon from the camera (mountains up to body.relief), or outside the
    // view frustum (in the parent's frame), which then neither draws nor refines it.
    bool culled(const Shape& s,const Vec& camera,double distance,const Frustum* frustum)const{
        if(frustum){const double r=(std::sin(std::min(s.radius,pi/2))*1.05+body.relief/body.radius)*body.scale;const Vec at=s.centre*body.scale;
            if(!frustum->intersectsSphere(Sphere(Vector3(static_cast<float>(at.x),static_cast<float>(at.y),static_cast<float>(at.z)),static_cast<float>(r))))return true;}
        if(distance<=1.)return false;
        const double horizon=std::acos(1/distance)+std::acos(1/(1+body.relief/body.radius));
        return std::acos(std::clamp(camera.dot(s.centre)/distance,-1.,1.))-s.radius>horizon;
    }
    // Children replace a node closer than 1.25 chords past its own (CDLOD-style), once all their
    // visible ones are built; a node evicted earlier is drawn by its children meanwhile.
    void visit(const Key& k,const Vec& camera,double distance,bool cull,const Frustum* frustum){
        const Shape& s=shape(k);if(cull&&culled(s,camera,distance,frustum))return;
        const double size=2*std::sin(s.radius); // Chord across the node.
        const bool split=k.level<body.maxLevel&&std::max(0.,(camera-s.centre).length()-size)<1.25*size;
        std::array<Key,4> children;int n=0;for(std::uint32_t dy:{0u,1u})for(std::uint32_t dx:{0u,1u})children[static_cast<std::size_t>(n++)]={k.face,k.level+1,k.x*2+dx,k.y*2+dy};
        bool ready=k.level<body.maxLevel;
        if(ready||split)for(const auto& c:children){const Shape& cs=shape(c);if(cull&&culled(cs,camera,distance,frustum))continue;
            if(!node(c)){ready=false;if(split)request(c,c.level*1e3+(camera-cs.centre).length());}}
        const bool have=node(k)!=nullptr;
        if(!have)request(k,k.level*1e3+(camera-s.centre).length());
        if(ready&&(split||!have)){for(const auto& c:children)visit(c,camera,distance,cull,frustum);return;}
        if(have)selected.push_back(k);
    }
};
Terrain::Terrain(Object3D& parent,std::shared_ptr<Material> material,TerrainBody body):impl_(std::make_unique<Impl>(parent,std::move(material),std::move(body))){}
Terrain::~Terrain()=default;
bool Terrain::update(const Vector3& camera,const Frustum* frustum,bool horizonCulling){
    const auto started=std::chrono::steady_clock::now();
    auto& g=*impl_;++g.frame;g.adopt();
    for(const auto& k:g.selected)if(auto* n=g.node(k))n->mesh->visible=false;
    g.selected.clear();const Vec c=Vec{camera.x,camera.y,camera.z}*(1/g.body.scale);const double distance=c.length(); // In reference radii.
    bool covered=true;
    for(int f=0;f<6;++f){const Key root{f,0,0,0};if(!g.node(root)){g.request(root,0);covered=false;}}
    if(covered)for(int f=0;f<6;++f)g.visit({f,0,0,0},c,distance,horizonCulling,horizonCulling?frustum:nullptr);
    g.finest=0;g.solidAngle=0;
    const std::uint64_t generation=g.body.generation?g.body.generation():0;
    for(const auto& k:g.selected){auto& n=*g.node(k);n.mesh->visible=true;g.finest=std::max(g.finest,k.level);if(n.detailVersion!=g.detailVersion)g.applyDetail(n);
        for(Key a=k;;){if(auto* up=g.node(a))up->lastUsed=g.frame;if(!a.level)break;a={a.face,a.level-1,a.x/2,a.y/2};} // Ancestors too: the selection descends through them.
        if(n.provisional&&n.generation<generation)g.request(k,k.level*1e3,true); // Finer data arrived.
        const auto& s=g.shape(k);g.solidAngle+=triangle(s.corners[0],s.corners[1],s.corners[2])+triangle(s.corners[0],s.corners[2],s.corners[3]);}
    static const bool debug=std::getenv("ALBERT_TERRAIN_DEBUG")!=nullptr;
    if(debug&&g.frame%60==0)std::fprintf(stderr,"terrain: %zu drawn (%zu with regional imagery), finest %d, %zu built, %zu pending, camera %.6f, update %.2f ms\n",g.selected.size(),detailed(),g.finest,g.nodes.size(),g.queued.size(),distance,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
    if(g.frame%120==0){ // Unused for ~10 s: released (the faces stay); shapes of unbuilt nodes forgotten.
        for(auto it=g.nodes.begin();it!=g.nodes.end();)if(((it->first>>56)&31)>0&&g.frame-it->second.lastUsed>600){g.parent.remove(*it->second.mesh);it->second.mesh->geometry()->dispose();it=g.nodes.erase(it);}else ++it;
        if(g.shapes.size()>20000)g.shapes.clear();
    }
    return covered;
}
void Terrain::hide(){for(const auto& k:impl_->selected)if(auto* n=impl_->node(k))n->mesh->visible=false;impl_->selected.clear();}
std::size_t Terrain::drawn()const{return impl_->selected.size();}
std::size_t Terrain::pending()const{std::lock_guard l(impl_->lock);return impl_->queued.size()+impl_->done.size();}
int Terrain::finestLevel()const{return impl_->finest;}
double Terrain::coveredSolidAngle()const{return impl_->solidAngle;}
void Terrain::detail(std::shared_ptr<Material> material,std::optional<Vector4> bounds){
    auto& g=*impl_;if(material==g.detailMaterial&&bounds==g.detailBounds)return;g.detailMaterial=std::move(material);g.detailBounds=bounds;++g.detailVersion;}
std::size_t Terrain::detailed()const{std::size_t n=0;for(const auto& k:impl_->selected)if(const auto* node=impl_->node(k);node&&node->detailed)++n;return n;}
std::optional<double> Terrain::ground(const Vector3& local)const{
    const auto& g=*impl_;const Vec d{local.x,local.y,local.z};const double length=d.length();if(g.selected.empty()||!(length>0))return std::nullopt;
    const Vec u=d*(1/length);std::size_t face=0;for(std::size_t f=1;f<6;++f)if(u.dot(faces[f][0])>u.dot(faces[face][0]))face=f;
    const auto& f=faces[face];const double n=u.dot(f[0]),a=std::atan(u.dot(f[1])/n)*4/pi,b=std::atan(u.dot(f[2])/n)*4/pi; // direction() inverted.
    const Impl::Node* found=nullptr;Key key;
    for(int level=0;level<=g.body.maxLevel;++level){ // Down to the finest built node there.
        const std::uint32_t cells=1u<<level;auto cell=[cells](double c){return std::min(cells-1,static_cast<std::uint32_t>(std::max(0.,(c+1)/2*cells)));};
        const Key k{static_cast<int>(face),level,cell(a),cell(b)};const auto it=g.nodes.find(pack(k));if(it==g.nodes.end())break;found=&it->second;key=k;}
    if(!found)return std::nullopt;
    const auto e=extent(key);const double step=e.size/(posts-1),x=std::clamp((a-e.a0)/step,0.,posts-1.),y=std::clamp((b-e.b0)/step,0.,posts-1.);
    const int i=std::min(static_cast<int>(x),posts-2),j=std::min(static_cast<int>(y),posts-2);const double s=x-i,t=y-j;
    const auto* p=found->mesh->geometry()->getAttribute<float>("position");const auto& at=found->mesh->position;
    auto radius=[&](int ii,int jj){const int v=jj*posts+ii;return Vec{double(p->getX(v))+at.x,double(p->getY(v))+at.y,double(p->getZ(v))+at.z}.length();};
    return (radius(i,j)*(1-s)+radius(i+1,j)*s)*(1-t)+(radius(i,j+1)*(1-s)+radius(i+1,j+1)*s)*t;
}
}
