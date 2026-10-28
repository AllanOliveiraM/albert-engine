#include "launch_crowd.hpp"
#include "launch_crowd_shader.hpp"
#include "starbase_cover.hpp"
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
using namespace threepp;
namespace albert {
namespace {
using Instance=LaunchCrowd::Instance;
float distance(Vector3 p,Vector3 a,Vector3 b){const Vector3 d=Vector3(b).sub(a);const float t=std::clamp(Vector3(p).sub(a).dot(d)/std::max(d.lengthSq(),.001f),0.f,1.f);return p.distanceTo(a.addScaledVector(d,t));}
Vector3 vec(core::Vector p){return {float(p.x),float(p.y),float(p.z)};}
float radius(unsigned kind){return kind==4?4.8f:kind==3?3.6f:3.1f;}
}
LaunchCrowd::LaunchCrowd(const core::Scenery& scenery){source_.roads=scenery.roads;source_.buildings=scenery.buildings;root_=Object3D::create();root_->name="launch day crowd";root_->visible=false;}
LaunchCrowd::~LaunchCrowd(){for(const auto& mesh:{cars_,figures_,nearCars_,nearFigures_})if(mesh){mesh->geometry()->dispose();mesh->material()->dispose();}for(const auto& texture:{detail_.cars,detail_.people})if(texture)texture->dispose();}
void LaunchCrowd::enabled(bool value,int quality){root_->visible=value&&quality>0;if(root_->visible&&!built_)build();}
void LaunchCrowd::build(){
    built_=true;defineLaunchCrowd();
    int w=0,h=0,c=0;auto* pixels=stbi_load_from_memory(boca::png,sizeof(boca::png),&w,&h,&c,3);
    if(!pixels||w!=boca::size||h!=boca::size){stbi_image_free(pixels);throw std::runtime_error("Invalid crowd land mask");}
    auto cover=[&](Vector3 p){const int x=int(std::floor((p.x-boca::west)/boca::cell)),y=int(std::floor((p.y-boca::south)/boca::cell));
        if(x<0||y<0||x>=w||y>=h)return std::pair{0,1e6f};
        const auto* v=pixels+3*(y*w+x);return std::pair{int(v[0]),float(int(v[1])*256+v[2]-32768)/128.f};};
    // Conservative artistic closure: 2 km from BOTH mounts, and no public parking inside the industrial campus.
    auto publicSpot=[&](Vector3 p,float r){if(std::hypot(p.x,p.y)<2000+r||std::hypot(p.x+353.2f,p.y-70.7f)<2000+r)return false;
        const auto [land,coast]=cover(p);return land!=1&&land!=2&&land!=3&&land!=6&&coast<-12-r;};
    struct Segment {Vector3 a,b;float half;};std::vector<Segment> segments;
    for(const auto& r:source_.roads)for(std::size_t i=1;i<r.points.size();++i)segments.push_back({vec(r.points[i-1]),vec(r.points[i]),float(r.width/2)});
    auto clear=[&](Vector3 p,float r){if(!publicSpot(p,r))return false;
        for(const auto& s:segments)if(distance(p,s.a,s.b)<s.half+r+.45f)return false;
        for(const auto& b:source_.buildings){ // Oriented envelopes retain driveways and keep people out of buildings.
            const float sn=std::sin(b.heading),cs=std::cos(b.heading),x=p.x-float(b.centre.x),y=p.y-float(b.centre.y);
            if(std::abs(x*cs-y*sn)<b.size.x/2+r+2&&std::abs(x*sn+y*cs)<b.size.y/2+r+2)return false;}
        return true;};
    std::uint32_t seed=0xabc70139;auto random=[&](){seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
    constexpr std::uint32_t shirts[]{0xe3dfd4,0x3b8193,0xb7503c,0x4268a3,0xb69a43,0x758250,0x37434d,0xd0b19a};
    constexpr std::uint32_t colours[]{0xe4e3dc,0xb7bdc3,0x293038,0x40607e,0x8a3230,0xc7bda3,0x576454,0x343c50,0xddd6c7,0x92989b};
    // A small spatial hash rejects overlap at curves, duplicate OSM ways and street junctions.
    std::map<std::pair<int,int>,std::vector<unsigned>> occupied;
    auto park=[&](Vector3 p,Vector3 forward,unsigned kind,std::uint32_t colour){const float r=radius(kind);if(!clear(p,r))return false;
        const int x=int(std::floor(p.x/16)),y=int(std::floor(p.y/16));for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const auto it=occupied.find({x+dx,y+dy});if(it==occupied.end())continue;
            for(auto i:it->second)if(p.distanceTo(vehicles_[i].position)<r+radius(vehicles_[i].kind)+.7f)return false;}
        occupied[{x,y}].push_back(vehicles_.size());vehicles_.push_back({p,forward,{0,0,1},colour,kind});return true;};
    auto group=[&](Vector3 p,unsigned count){for(unsigned i=0;i<count;++i){Vector3 at=p;at.x+=(random()-.5f)*7;at.y+=(random()-.5f)*7;
        if(!clear(at,.45f))continue;
        bool free=true;for(const auto& car:vehicles_)if(at.distanceTo(car.position)<radius(car.kind)+.7f){free=false;break;}
        if(free)people_.push_back({at,Vector3(-at.x,-at.y,0).normalize(),{0,0,1},shirts[unsigned(random()*8)],unsigned(random()*4)});}};
    for(const auto& road:source_.roads){
        // Saved village residential streets only; the factory's service roads are private.
        const bool village=!road.highway&&road.surface==core::SceneryRoad::Surface::Asphalt&&road.width>=5;
        if(!road.highway&&!village)continue;
        float along=0,next=6;for(std::size_t i=1;i<road.points.size();++i){const Vector3 a=vec(road.points[i-1]),b=vec(road.points[i]),d=Vector3(b).sub(a);const float length=d.length();if(length<.01f)continue;const Vector3 forward=Vector3(d).divideScalar(length);
            for(;next<along+length;next+=8.2f+random()*3.8f){const Vector3 centre=Vector3(a).addScaledVector(forward,next-along);
                if(village&&(centre.x<-3400||centre.x>-2200||centre.y<-1150||centre.y>-200))continue;
                for(float side:{-1.f,1.f}){const float pick=random();const unsigned kind=pick<.34f?0:pick<.65f?1:pick<.91f?2:pick<.98f?3:4;
                    const float width=kind==4?2.6f:kind==3?2.3f:2.f;
                    // Full vehicle's circumscribed envelope clears every paved edge, including bends.
                    Vector3 p=centre;p.add(Vector3(forward.y,-forward.x,0).multiplyScalar(side*(road.width/2+radius(kind)+.7f)));
                    if(park(p,Vector3(forward).multiplyScalar(side),kind,colours[unsigned(random()*10)])){
                        const Vector3 by=Vector3(p).add(Vector3(forward.y,-forward.x,0).multiplyScalar(side*(width/2+4)));
                        group(by,2+unsigned(random()*4));}}}
            along+=length;}}
    // Beach and dunes south of the closure. The bundled shoreline mask, rather than a straight invented coast,
    // selects dry sand/grass; the beach immediately east of the launch pads deliberately stays empty.
    for(float y=-4200;y<-2100;y+=27)for(float x=0;x<2200;x+=16){Vector3 p(x+(random()-.5f)*12,y+(random()-.5f)*16,0);const auto [land,coast]=cover(p);
        if((land==0||land==4||land==5)&&coast<-18&&coast>-150&&publicSpot(p,4)&&random()<.22f)group(p,3+unsigned(random()*6));}
    // Viewers around the public broadcast tripod, on dry sand well south of the closure.
    group({491,-2455,0},9);group({480,-2415,0},7);
    // A small press pull-off at the western public closure, clear of the village and all traffic lanes.
    std::vector<Instance> candidates=vehicles_;std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.position.lengthSq()<b.position.lengthSq();});
    unsigned press=0;Vector3 pressCentre;
    for(const auto& car:candidates){if(car.position.x>-2200||car.position.y>0||car.position.y<-1200)continue;
        Vector3 right(car.forward.y,-car.forward.x,0),p=Vector3(car.position).addScaledVector(right,14);
        if(press&&p.distanceTo(pressCentre)>100)continue;
        if(!park(p,car.forward,3,0xe4e3dc))continue;
        if(!press)pressCentre=p;
        group(Vector3(p).addScaledVector(right,6),4);
        for(unsigned j=0;j<3;++j){Vector3 at=Vector3(p).addScaledVector(right,7).addScaledVector(car.forward,float(j)*3-3);
            if(clear(at,.7f))vehicles_.push_back({at,Vector3(-at.x,-at.y,0).normalize(),{0,0,1},0x34383b,5});}
        if(++press==4)break;
    }
    std::erase_if(people_,[&](const auto& p){for(const auto& car:vehicles_)if(car.kind!=5&&p.position.distanceTo(car.position)<radius(car.kind)+.7f)return true;return false;});
    bool firstSeat=true;for(unsigned i=0;i<vehicles_.size();++i)if(vehicles_[i].kind==0&&(firstSeat||i%11==0)){
        const auto& car=vehicles_[i];const Vector3 feet=Vector3(car.position).addScaledVector(car.forward,-3.1f);
        if(clear(feet,.6f)&&feet.length()>2050){seats_.push_back(i);firstSeat=false;}
    }
    carNear_.resize(vehicles_.size());personNear_.resize(people_.size());
    stbi_image_free(pixels);source_={};
    auto batch=[&](const char* name,const char* shader){auto material=ShaderMaterial::create();material->name=shader;material->lights=true;
        material->uniforms={{"eye",Uniform(Vector3())}};auto p=Points::create(BufferGeometry::create(),material);p->name=name;p->frustumCulled=false;root_->add(p);return p;};
    cars_=batch("launch parked cars and press","albert:crowd-cars");figures_=batch("launch nearby spectators","albert:crowd-people");
    upload(cars_,vehicles_);upload(figures_,{});
}
void LaunchCrowd::detail(){if(nearCars_)return;
    detail_=makeCrowdDetail();
    auto batch=[&](const char* name,const char* program,const std::shared_ptr<DataTexture>& texture,unsigned vertices,unsigned rows){
        auto material=ShaderMaterial::create();material->name=program;material->lights=true;
        material->uniforms={{"eye",Uniform(Vector3())},{"geometryInfo",Uniform(Vector4(768.f,float(rows),float(vertices),0.f))},{"nearGeometry",Uniform(texture.get())}};
        auto mesh=Points::create(BufferGeometry::create(),material);mesh->name=name;mesh->frustumCulled=false;mesh->visible=false;root_->add(mesh);return mesh;
    };
    nearCars_=batch("launch near cars","albert:crowd-car-near",detail_.cars,detail_.carVertices,detail_.carRows);
    nearFigures_=batch("launch near people","albert:crowd-person-near",detail_.people,detail_.personVertices,detail_.personRows);
}
void LaunchCrowd::upload(const std::shared_ptr<Points>& mesh,const std::vector<Instance>& instances){
    auto g=mesh->geometry();const unsigned capacity=mesh==cars_?unsigned(vehicles_.size()):mesh==nearCars_?nearCarLimit:mesh==nearFigures_?nearPersonLimit:800;
    if(!g->hasAttribute("position")){for(const char* name:{"position","forward","up","color"})g->setAttribute(name,FloatBufferAttribute::create(std::vector<float>(capacity*3),3));g->setAttribute("kind",FloatBufferAttribute::create(std::vector<float>(capacity),1));}
    unsigned i=0;for(const auto& p:instances){g->getAttribute<float>("position")->setXYZ(i,p.position.x,p.position.y,p.position.z);g->getAttribute<float>("forward")->setXYZ(i,p.forward.x,p.forward.y,p.forward.z);g->getAttribute<float>("up")->setXYZ(i,p.up.x,p.up.y,p.up.z);
        const Color ink(p.colour);g->getAttribute<float>("color")->setXYZ(i,ink.r,ink.g,ink.b);g->getAttribute<float>("kind")->setX(i,float(p.kind));++i;}
    for(const char* name:{"position","forward","up","color","kind"})g->getAttribute<float>(name)->needsUpdate();
    g->setDrawRange(0,int(instances.size()));mesh->visible=!instances.empty();
}
void LaunchCrowd::view(Camera& camera,const Ground& ground){if(!root_->visible||!built_)return;
    root_->updateWorldMatrix(true,false);Vector3 eye;camera.getWorldPosition(eye);Matrix4 inverse;inverse.copy(*root_->matrixWorld).invert();eye.applyMatrix4(inverse);
    cars_->visible=eye.distanceTo(Vector3(-2000,-1200,0))<16000;
    if(!cars_->visible){figures_->visible=false;if(nearCars_)nearCars_->visible=nearFigures_->visible=false;nearby_=detailedCars_=detailedPeople_=0;return;}
    if(ground&&!vehicles_.empty()){ // Bounded refresh follows incoming DEM/LOD changes without resampling kilometres each frame.
        const unsigned count=cursor_==0?unsigned(vehicles_.size()):64;
        for(unsigned i=0;i<count;++i){auto& p=vehicles_[(cursor_+i)%vehicles_.size()];Vector3 a=p.position,b=p.position,c=p.position;a.z=b.z=c.z=0;
            b.addScaledVector(p.forward,2);c.add({p.forward.y*1.0f,-p.forward.x*1.0f,0});const auto z=ground(a),front=ground(b),side=ground(c);
            if(z&&front&&side){p.position.z=*z+.06f;p.forward.z=(*front-*z)/2;p.forward.normalize();const Vector3 right(p.forward.y,-p.forward.x,*side-*z);p.up=Vector3(right).cross(p.forward).normalize();}}
        cursor_=(cursor_+count)%vehicles_.size();if(cursor_==0)cursor_=1;}
    std::vector<std::pair<float,unsigned>> selected;for(unsigned i=0;i<people_.size();++i){const float d=eye.distanceToSquared(people_[i].position);if(d<800*800)selected.emplace_back(d,i);}
    if(selected.size()>800){std::nth_element(selected.begin(),selected.begin()+800,selected.end());selected.resize(800);}std::sort(selected.begin(),selected.end(),[](auto a,auto b){return a.second<b.second;});
    // Hysteresis retains a selected near mesh through 150 m. Overflow stays in the existing far draw.
    std::vector<std::pair<float,unsigned>> closeCars;for(unsigned i=0;i<vehicles_.size();++i){const float d=eye.distanceToSquared(vehicles_[i].position),limit=carNear_[i]?nearLeave:nearEnter;
        if(vehicles_[i].kind<5&&d<limit*limit)closeCars.emplace_back(d,i);}
    std::sort(closeCars.begin(),closeCars.end());if(closeCars.size()>nearCarLimit)closeCars.resize(nearCarLimit);
    std::fill(carNear_.begin(),carNear_.end(),false);for(const auto& [d,i]:closeCars){(void)d;carNear_[i]=true;}
    std::vector<Instance> farCars,closeVehicles;for(unsigned i=0;i<vehicles_.size();++i){auto car=vehicles_[i];
        if(carNear_[i]){if(std::find(seats_.begin(),seats_.end(),i)!=seats_.end())car.kind=5;closeVehicles.push_back(car);}else farCars.push_back(car);}
    std::vector<std::pair<float,unsigned>> closePeople;for(const auto& [d,i]:selected){const float limit=personNear_[i]?nearLeave:nearEnter;if(d<limit*limit)closePeople.emplace_back(d,i);}
    std::sort(closePeople.begin(),closePeople.end());if(closePeople.size()>nearPersonLimit)closePeople.resize(nearPersonLimit);
    std::fill(personNear_.begin(),personNear_.end(),false);for(const auto& [d,i]:closePeople){(void)d;personNear_[i]=true;}
    std::vector<Instance> nearby,peopleNear;nearby.reserve(selected.size());
    for(const auto& [d,i]:selected){(void)d;auto p=people_[i];if(ground)if(const auto z=ground(p.position))p.position.z=*z+.04f;
        if(personNear_[i]){p.kind+=i%4==1?4:0;peopleNear.push_back(p);}else nearby.push_back(p);}
    // A few extra near-only occupants sit on actual pickup tailgates. They share the people batch and their car's ground frame.
    for(unsigned i:seats_)if(carNear_[i]&&peopleNear.size()<nearPersonLimit){const auto& car=vehicles_[i];Instance seated=car;
        seated.position.addScaledVector(car.forward,-2.6f).addScaledVector(car.up,.12f);seated.forward.multiplyScalar(-1);seated.kind=8+i%4;
        constexpr std::uint32_t shirts[]{0xe3dfd4,0x3b8193,0xb7503c,0x4268a3,0xb69a43,0x758250,0x37434d,0xd0b19a};seated.colour=shirts[i%8];peopleNear.push_back(seated);}
    nearby_=selected.size();detailedCars_=closeVehicles.size();detailedPeople_=peopleNear.size();
    upload(cars_,farCars);upload(figures_,nearby);figures_->material()->as<ShaderMaterial>()->uniforms.at("eye").setValue(eye);
    if(!closeVehicles.empty()||!peopleNear.empty())detail();
    if(nearCars_){upload(nearCars_,closeVehicles);upload(nearFigures_,peopleNear);}
}
}
