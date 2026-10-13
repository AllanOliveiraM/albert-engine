#include "scene.hpp"
#include "bodies.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace albert::core {
namespace {
bool finite(Vector v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
double length(Vector v){return std::hypot(v.x,v.y,v.z);}
Quaternion blend(Quaternion a,Quaternion b,double t){
    double dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0){b={-b.x,-b.y,-b.z,-b.w};dot=-dot;}
    double p=1-t,q=t;
    if(dot<.9995){const double angle=std::acos(std::clamp(dot,-1.,1.)),s=std::sin(angle);p=std::sin((1-t)*angle)/s;q=std::sin(t*angle)/s;}
    Quaternion r{p*a.x+q*b.x,p*a.y+q*b.y,p*a.z+q*b.z,p*a.w+q*b.w};
    const double n=std::hypot(std::hypot(r.x,r.y),std::hypot(r.z,r.w));
    return {r.x/n,r.y/n,r.z/n,r.w/n};
}
}
void validate(Sample& s){
    if(!std::isfinite(s.utcSeconds)||s.utcSeconds<0||s.utcSeconds>1e11||!finite(s.positionMeters)||length(s.positionMeters)>1e15||
       !finite(s.velocityMetersPerSecond)||length(s.velocityMetersPerSecond)>3e8) throw std::invalid_argument("Invalid timestamp, position or velocity");
    if(s.attitude){auto& q=*s.attitude;const double n=std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w));
        if(!std::isfinite(n)||n<1e-12) throw std::invalid_argument("Invalid quaternion");q={q.x/n,q.y/n,q.z/n,q.w/n};}
}
Scene::Scene(){
    for(auto o:std::array<Object,3>{Object{"earth","Terra","NASA/JPL Horizons"},Object{"moon","Lua","NASA/JPL Horizons"},Object{"sun","Sol","NASA/JPL Horizons"}}){
        o.builtin=true;o.appearance.shape=Shape::Builtin;
        const bool earth=o.id=="earth",moon=o.id=="moon";
        o.radiusMeters=earth ? earthRadiusMeters : moon ? 1737400. : 695700000.;
        o.appearance.sizeMeters={o.radiusMeters*2,o.radiusMeters*2,o.radiusMeters*2};
        o.defaultDistanceMeters=(earth ? 3.5 : moon ? 1.1 : 400.)*earthRadiusMeters;
        o.minDistanceMeters=(earth ? 1.05 : moon ? .30 : 150.)*earthRadiusMeters;
        const auto id=o.id;objects_.emplace(id,std::move(o));
    }
}
void Scene::include(const NaturalBody& body){
    if(naturalBody(body.id)!=&body)throw std::invalid_argument("Unknown built-in body definition");
    if(objects_.size()>=maxObjects)throw std::length_error("Scene object limit reached");
    if(objects_.contains(std::string(body.id)))throw std::invalid_argument("Built-in body already registered");
    Object o;o.id=body.id;o.name=body.name;o.source="NASA/JPL Horizons; NASA/USGS MESSENGER; IAU/NAIF pck00011";
    o.builtin=true;o.appearance.shape=Shape::Builtin;o.radiusMeters=body.radiusMeters;
    o.appearance.sizeMeters={2*body.radiusMeters,2*body.radiusMeters,2*body.radiusMeters};
    o.defaultDistanceMeters=body.radiusMeters*4;o.minDistanceMeters=body.radiusMeters*1.05;
    objects_.emplace(o.id,std::move(o));++revision_;
}
const Object& Scene::get(const ObjectId& id) const{
    const auto it=objects_.find(id);if(it==objects_.end())throw std::out_of_range("Unknown object: "+id);return it->second;
}
void Scene::create(Object o){
    if(o.id.empty()||o.id.size()>96||o.id.front()=='@'||!std::all_of(o.id.begin(),o.id.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';}))throw std::invalid_argument("Invalid object ID");
    if(objects_.contains(o.id))throw std::invalid_argument("Object ID already exists");
    if(objects_.size()>=maxObjects)throw std::length_error("Scene object limit reached");
    if(o.name.empty()||o.name.size()>256||o.source.size()>512||o.builtin||o.appearance.shape==Shape::Builtin||
       o.appearance.shape<Shape::Sphere||o.appearance.shape>Shape::Cylinder||!finite(o.appearance.sizeMeters)||std::min({o.appearance.sizeMeters.x,o.appearance.sizeMeters.y,o.appearance.sizeMeters.z})<.1||
       std::max({o.appearance.sizeMeters.x,o.appearance.sizeMeters.y,o.appearance.sizeMeters.z})>1e10||o.appearance.color>0xffffff)throw std::invalid_argument("Invalid object presentation");
    if(o.appearance.shape==Shape::Sphere)o.radiusMeters=.5*std::max({o.appearance.sizeMeters.x,o.appearance.sizeMeters.y,o.appearance.sizeMeters.z});
    else o.radiusMeters=.5*length(o.appearance.sizeMeters);
    o.defaultDistanceMeters=o.radiusMeters*4;o.minDistanceMeters=o.radiusMeters*1.05;
    if(o.samples.empty()||o.samples.size()>maxSamples)throw std::invalid_argument("Initial samples required");
    for(auto& s:o.samples)validate(s);
    for(size_t i=1;i<o.samples.size();++i)if(o.samples[i].utcSeconds<=o.samples[i-1].utcSeconds)throw std::invalid_argument("Samples must have increasing timestamps");
    const auto id=o.id;objects_.emplace(id,std::move(o));++revision_;
}
void Scene::remove(const ObjectId& id){if(get(id).builtin)throw std::invalid_argument("Built-in objects cannot be removed");objects_.erase(id);++revision_;}
void Scene::visible(const ObjectId& id,bool enabled){if(get(id).builtin)throw std::invalid_argument("Built-in visibility is managed by the renderer");objects_.at(id).visible=enabled;++revision_;}
void Scene::samples(const ObjectId& id,std::vector<Sample> values){
    if(get(id).builtin)throw std::invalid_argument("Built-in state is managed by astronomy providers");
    if(values.empty()||values.size()>maxSamples)throw std::invalid_argument("Invalid sample batch size");
    for(auto& v:values)validate(v);
    for(size_t i=1;i<values.size();++i)if(values[i].utcSeconds<=values[i-1].utcSeconds)throw std::invalid_argument("Samples must be ordered");
    auto& target=objects_.at(id).samples;
    // Reject stale batches atomically; retained history is bounded per object.
    if(!target.empty()&&values.front().utcSeconds<=target.back().utcSeconds)throw std::invalid_argument("Out-of-order sample");
    target.insert(target.end(),values.begin(),values.end());
    if(target.size()>maxSamples)target.erase(target.begin(),target.end()-maxSamples);
    ++revision_;
}
void Scene::builtinState(const ObjectId& id,Sample s){validate(s);auto& o=objects_.at(id);if(!o.builtin)throw std::invalid_argument("Object is not built in");o.samples.assign(1,std::move(s));}
Evaluated Scene::evaluate(const ObjectId& id,double now) const{
    if(!std::isfinite(now))throw std::invalid_argument("Invalid evaluation time");
    const auto& o=get(id);if(o.samples.empty())return {{},true};const auto& v=o.samples;
    if(now<=v.front().utcSeconds)return {v.front(),now<v.front().utcSeconds};
    if(now>=v.back().utcSeconds)return {v.back(),!o.builtin&&now-v.back().utcSeconds>1.};
    const auto b=std::upper_bound(v.begin(),v.end(),now,[](double t,const Sample& s){return t<s.utcSeconds;});const auto& a=*(b-1);
    const double dt=b->utcSeconds-a.utcSeconds,t=(now-a.utcSeconds)/dt;
    Sample r=a;r.utcSeconds=now;
    if(a.hasVelocity&&b->hasVelocity){const double t2=t*t,t3=t2*t;r.positionMeters=a.positionMeters*(2*t3-3*t2+1)+a.velocityMetersPerSecond*((t3-2*t2+t)*dt)+b->positionMeters*(-2*t3+3*t2)+b->velocityMetersPerSecond*((t3-t2)*dt);}
    else r.positionMeters=a.positionMeters*(1-t)+b->positionMeters*t;
    r.velocityMetersPerSecond=a.velocityMetersPerSecond*(1-t)+b->velocityMetersPerSecond*t;
    r.hasVelocity=a.hasVelocity&&b->hasVelocity;
    if(a.attitude&&b->attitude)r.attitude=blend(*a.attitude,*b->attitude,t);
    return {r,false};
}
Snapshot Scene::snapshot(double now,const CameraState& camera) const{Snapshot result{revision_,now,{},camera,{}};result.clock.utcSeconds=now;for(const auto& [id,o]:objects_){(void)id;result.objects.push_back(o);result.objects.back().samples.assign(1,evaluate(id,now).state);}return result;}
}
