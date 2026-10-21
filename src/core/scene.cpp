#include "scene.hpp"
#include "bodies.hpp"
#include "model.hpp"
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
Quaternion product(Quaternion a,Quaternion b){return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};}
Quaternion rotation(const Vector c[3]){ // From a rotation's columns (Shepperd).
    const double m00=c[0].x,m11=c[1].y,m22=c[2].z,t=m00+m11+m22;Quaternion q;
    if(t>m00&&t>m11&&t>m22){const double s=2*std::sqrt(1+t);q={(c[1].z-c[2].y)/s,(c[2].x-c[0].z)/s,(c[0].y-c[1].x)/s,s/4};}
    else if(m00>m11&&m00>m22){const double s=2*std::sqrt(1+m00-m11-m22);q={s/4,(c[1].x+c[0].y)/s,(c[2].x+c[0].z)/s,(c[1].z-c[2].y)/s};}
    else if(m11>m22){const double s=2*std::sqrt(1+m11-m00-m22);q={(c[1].x+c[0].y)/s,s/4,(c[2].y+c[1].z)/s,(c[2].x-c[0].z)/s};}
    else{const double s=2*std::sqrt(1+m22-m00-m11);q={(c[2].x+c[0].z)/s,(c[2].y+c[1].z)/s,s/4,(c[0].y-c[1].x)/s};}
    const double n=std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w));return {q.x/n,q.y/n,q.z/n,q.w/n};
}
}
void validate(Sample& s){
    if(!std::isfinite(s.utcSeconds)||s.utcSeconds< -1e10||s.utcSeconds>1e11||!finite(s.positionMeters)||length(s.positionMeters)>1e15||
       !finite(s.velocityMetersPerSecond)||length(s.velocityMetersPerSecond)>3e8) throw std::invalid_argument("Invalid timestamp, position or velocity");
    if(s.attitude){auto& q=*s.attitude;const double n=std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w));
        if(!std::isfinite(n)||n<1e-12) throw std::invalid_argument("Invalid quaternion");q={q.x/n,q.y/n,q.z/n,q.w/n};}
    if(s.parts&&(s.parts->size()>1024||!std::all_of(s.parts->begin(),s.parts->end(),[](float x){return std::isfinite(x);})))throw std::invalid_argument("Invalid vehicle parts");
}
Scene::Scene(){
    for(auto o:std::array<Object,3>{Object{"earth","Terra","NASA/JPL Horizons"},Object{"moon","Lua","NASA/JPL Horizons"},Object{"sun","Sol","NASA/JPL Horizons"}}){
        o.builtin=true;o.appearance.shape=Shape::Builtin;
        const bool earth=o.id=="earth",moon=o.id=="moon";
        o.radiusMeters=earth ? earthRadiusMeters : moon ? 1737400. : 695700000.;
        o.appearance.sizeMeters={o.radiusMeters*2,o.radiusMeters*2,o.radiusMeters*2};
        o.defaultDistanceMeters=(earth ? 3.5 : moon ? 1.1 : 400.)*earthRadiusMeters;
        o.minDistanceMeters=earth ? earthRadiusMeters*1.01 : moon ? 1737400.+20000. : 150.*earthRadiusMeters;
        const auto id=o.id;objects_.emplace(id,std::move(o));
    }
}
void Scene::include(const NaturalBody& body){
    if(naturalBody(body.id)!=&body)throw std::invalid_argument("Unknown built-in body definition");
    if(objects_.size()>=maxObjects)throw std::length_error("Scene object limit reached");
    if(objects_.contains(std::string(body.id)))throw std::invalid_argument("Built-in body already registered");
    Object o;o.id=body.id;o.name=body.name;o.source="NASA/JPL Horizons; NASA bundled imagery; IAU/NAIF pck00011";
    o.builtin=true;o.appearance.shape=Shape::Builtin;o.radiusMeters=body.radiusMeters;
    o.appearance.sizeMeters={2*body.radiusMeters,2*body.radiusMeters,2*body.radiusMeters};
    o.defaultDistanceMeters=body.radiusMeters*4;o.minDistanceMeters=body.radiusMeters*1.05;
    objects_.emplace(o.id,std::move(o));++revision_;
}
const Object& Scene::get(const ObjectId& id) const{
    const auto it=objects_.find(id);if(it==objects_.end())throw std::out_of_range("Unknown object: "+id);return it->second;
}
bool validObjectId(std::string_view id){return !id.empty()&&id.size()<=96&&id.front()!='@'&&std::all_of(id.begin(),id.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';});}
void Scene::create(Object o,bool replace){
    if(!validObjectId(o.id))throw std::invalid_argument("Invalid object ID");
    const auto old=objects_.find(o.id);
    if(old!=objects_.end()&&(!replace||old->second.builtin))throw std::invalid_argument("Object ID already exists");
    if(old==objects_.end()&&objects_.size()>=maxObjects)throw std::length_error("Scene object limit reached");
    if(o.appearance.shape==Shape::Model){
        const auto& model=*models_.at(o.appearance.modelId);const double scale=o.appearance.metersPerUnit;
        if(!std::isfinite(scale)||scale<=0||scale>1e10||model.radiusUnits*scale<.05||model.radiusUnits*scale>1e10)throw std::invalid_argument("Invalid model physical scale");
        o.appearance.sizeMeters=model.sizeUnits*scale;o.radiusMeters=model.radiusUnits*scale;
    }else if(!o.appearance.modelId.empty())throw std::invalid_argument("Model ID requires model geometry");
    if(o.name.empty()||o.name.size()>256||o.source.size()>512||o.builtin||o.appearance.shape==Shape::Builtin||
       o.appearance.shape<Shape::Sphere||o.appearance.shape>Shape::Model||!finite(o.appearance.sizeMeters)||(o.appearance.shape!=Shape::Model&&std::min({o.appearance.sizeMeters.x,o.appearance.sizeMeters.y,o.appearance.sizeMeters.z})<.1)||
       std::max({o.appearance.sizeMeters.x,o.appearance.sizeMeters.y,o.appearance.sizeMeters.z})>1e10||o.appearance.color>0xffffff)throw std::invalid_argument("Invalid object presentation");
    if(o.appearance.shape==Shape::Sphere)o.radiusMeters=.5*std::max({o.appearance.sizeMeters.x,o.appearance.sizeMeters.y,o.appearance.sizeMeters.z});
    else if(o.appearance.shape!=Shape::Model)o.radiusMeters=.5*length(o.appearance.sizeMeters);
    o.defaultDistanceMeters=o.radiusMeters*4;o.minDistanceMeters=o.radiusMeters*1.05;
    if(o.samples.empty()||o.samples.size()>maxSamples)throw std::invalid_argument("Initial samples required");
    for(auto& s:o.samples)validate(s);
    if(o.anchor){const auto& a=*o.anchor;const auto b=objects_.find(a.body);const double n=std::hypot(std::hypot(a.attitude.x,a.attitude.y),std::hypot(a.attitude.z,a.attitude.w));
        if(b==objects_.end()||!b->second.builtin||!finite(a.position)||length(a.position)>1e10||!std::isfinite(n)||std::abs(n-1)>1e-6)throw std::invalid_argument("Invalid anchor");}
    for(size_t i=1;i<o.samples.size();++i)if(o.samples[i].utcSeconds<=o.samples[i-1].utcSeconds)throw std::invalid_argument("Samples must have increasing timestamps");
    o.instance=revision_+1;const auto id=o.id;objects_.insert_or_assign(id,std::move(o));++revision_;
}
void Scene::createLayer(Layer layer){
    if(!validObjectId(layer.id)||layer.name.empty()||layer.name.size()>256||layer.source.size()>512||!layer.data||layer.data->positions.empty()||layer.data->positions.size()>maxLayerPoints||layer.color>0xffffff||!std::isfinite(layer.widthPixels)||layer.widthPixels<1||layer.widthPixels>32||!std::isfinite(layer.clearanceMeters)||layer.clearanceMeters<0||layer.clearanceMeters>1e6)throw std::invalid_argument("Invalid layer");
    if(layer.kind!=LayerKind::Points&&layer.kind!=LayerKind::Polyline)throw std::invalid_argument("Invalid layer geometry");
    if(layer.frame!=LayerFrame::GeocentricICRF&&layer.frame!=LayerFrame::ObjectLocal&&layer.frame!=LayerFrame::EarthSurface)throw std::invalid_argument("Invalid layer frame");
    if(layer.kind==LayerKind::Polyline&&layer.data->positions.size()<2)throw std::invalid_argument("Polyline requires two points");
    if(layer.frame==LayerFrame::GeocentricICRF){if(!layer.anchor.empty())throw std::invalid_argument("Geocentric layer must not have an anchor");}else{get(layer.anchor);if(layer.frame==LayerFrame::EarthSurface&&layer.anchor!="earth")throw std::invalid_argument("Surface layers currently support Earth only");}
    if(layers_.contains(layer.id))throw std::invalid_argument("Layer ID already exists");std::size_t count=layer.data->positions.size();for(const auto& [id,l]:layers_){(void)id;count+=l.data->positions.size();}
    if(layers_.size()>=maxLayers||count>maxSceneLayerPoints)throw std::length_error("Layer resource limit reached");
    for(const auto& p:layer.data->positions)if(!finite(p)||length(p)>1e15)throw std::invalid_argument("Invalid layer vertex");
    layer.instance=revision_+1;const auto id=layer.id;layers_.emplace(id,std::move(layer));++revision_;
}
void Scene::removeLayer(const std::string& id){if(!layers_.erase(id))throw std::out_of_range("Unknown layer");++revision_;}
void Scene::layerVisible(const std::string& id,bool visible){layers_.at(id).visible=visible;++revision_;}
void Scene::installModel(std::shared_ptr<const Model> model){
    if(!model||!validObjectId(model->id)||model->parts.empty()||model->sourceBytes>maxModelBytes||model->vertices>maxModelVertices||model->triangles>maxModelTriangles)throw std::invalid_argument("Invalid decoded model");
    if(models_.contains(model->id))throw std::invalid_argument("Model ID already exists");if(models_.size()>=32)throw std::length_error("Model asset limit reached");models_.emplace(model->id,std::move(model));++revision_;
}
void Scene::removeModel(const std::string& id){
    if(!models_.contains(id))throw std::out_of_range("Unknown model");for(const auto& [objectId,o]:objects_){(void)objectId;if(o.appearance.modelId==id)throw std::invalid_argument("Model is still used by a scene object");}models_.erase(id);++revision_;
}
void Scene::remove(const ObjectId& id){if(get(id).builtin)throw std::invalid_argument("Built-in objects cannot be removed");objects_.erase(id);for(auto i=layers_.begin();i!=layers_.end();)if(i->second.anchor==id)i=layers_.erase(i);else ++i;++revision_;}
void Scene::visible(const ObjectId& id,bool enabled){if(get(id).builtin)throw std::invalid_argument("Built-in visibility is managed by the renderer");objects_.at(id).visible=enabled;++revision_;}
void Scene::samples(const ObjectId& id,std::vector<Sample> values){
    if(get(id).builtin||get(id).providerManaged)throw std::invalid_argument("Object state is managed by an astronomy provider");
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
void Scene::replace(const ObjectId& id,std::vector<Sample> values){
    const auto& o=get(id);if(o.builtin||!o.providerManaged)throw std::invalid_argument("Only provider-managed objects accept provider replacement");
    if(values.empty()||values.size()>maxSamples)throw std::invalid_argument("Invalid provider sample batch");
    for(auto& s:values)validate(s);for(size_t i=1;i<values.size();++i)if(values[i].utcSeconds<=values[i-1].utcSeconds)throw std::invalid_argument("Provider samples must be ordered");
    objects_.at(id).samples=std::move(values);++revision_;
}
void Scene::builtinFrame(const ObjectId& id,double utc,const std::array<double,9>& m,double spin){
    if(!get(id).builtin||!std::isfinite(utc)||!std::isfinite(spin)||!std::all_of(m.begin(),m.end(),[](double x){return std::isfinite(x);}))throw std::invalid_argument("Invalid body frame");
    frames_[id]={utc,m,spin};
}
void Scene::builtinState(const ObjectId& id,Sample s){validate(s);auto& o=objects_.at(id);if(!o.builtin)throw std::invalid_argument("Object is not built in");o.samples.assign(1,std::move(s));}
namespace {
Evaluated sampled(const Object& o,double now){ // Between its samples.
    if(o.samples.empty())return {{},true};const auto& v=o.samples;
    if(now<=v.front().utcSeconds)return {v.front(),now<v.front().utcSeconds};
    if(now>=v.back().utcSeconds){ // Past the last sample: on along its velocity for a second (a flight's samples come every ~33 ms of its own
        // clock; frozen in between, a vehicle riding the Earth would jump against what turns with it, the ground and its tower).
        Sample r=v.back();const double ahead=std::min(now-r.utcSeconds,1.);if(r.hasVelocity)r.positionMeters=r.positionMeters+r.velocityMetersPerSecond*ahead;
        r.utcSeconds=now;return {r,now-v.back().utcSeconds>1.};}
    const auto b=std::upper_bound(v.begin(),v.end(),now,[](double t,const Sample& s){return t<s.utcSeconds;});const auto& a=*(b-1);
    const double dt=b->utcSeconds-a.utcSeconds,t=(now-a.utcSeconds)/dt;
    Sample r=a;r.utcSeconds=now;
    if(a.hasVelocity&&b->hasVelocity){const double t2=t*t,t3=t2*t;r.positionMeters=a.positionMeters*(2*t3-3*t2+1)+a.velocityMetersPerSecond*((t3-2*t2+t)*dt)+b->positionMeters*(-2*t3+3*t2)+b->velocityMetersPerSecond*((t3-t2)*dt);}
    else r.positionMeters=a.positionMeters*(1-t)+b->positionMeters*t;
    r.velocityMetersPerSecond=a.velocityMetersPerSecond*(1-t)+b->velocityMetersPerSecond*t;
    r.hasVelocity=a.hasVelocity&&b->hasVelocity;
    if(a.attitude&&b->attitude)r.attitude=blend(*a.attitude,*b->attitude,t);
    if(a.parts&&b->parts&&a.parts->size()==b->parts->size()){auto p=std::make_shared<std::vector<float>>(*a.parts);
        for(std::size_t i=0;i<p->size();++i)(*p)[i]+=static_cast<float>(t)*((*b->parts)[i]-(*p)[i]);r.parts=std::move(p);}
    return {r,false};
}
}
Evaluated Scene::evaluate(const ObjectId& id,double now) const{
    if(!std::isfinite(now))throw std::invalid_argument("Invalid evaluation time");
    const auto& o=get(id);Evaluated e=sampled(o,now);
    if(o.anchor)if(const auto f=frames_.find(o.anchor->body);f!=frames_.end()){ // Fixed on its body: the body's frame now (its samples: the moving parts).
        const auto& a=*o.anchor;const auto& m=f->second.m;const double th=f->second.spin*(now-f->second.utc),c=std::cos(th),s=std::sin(th);
        auto out=[&](Vector v){return Vector{m[0]*v.x+m[3]*v.y+m[6]*v.z,m[1]*v.x+m[4]*v.y+m[7]*v.z,m[2]*v.x+m[5]*v.y+m[8]*v.z};}; // Fixed then to celestial.
        const Vector axes[3]{out({c,s,0}),out({-s,c,0}),out({0,0,1})}; // Fixed now to celestial: the frame then, turned on by θ about z.
        const Vector r=axes[0]*a.position.x+axes[1]*a.position.y+axes[2]*a.position.z,w=axes[2]*f->second.spin;const Evaluated b=evaluate(a.body,now);
        Sample x{now,b.state.positionMeters+r,b.state.velocityMetersPerSecond+Vector{w.y*r.z-w.z*r.y,w.z*r.x-w.x*r.z,w.x*r.y-w.y*r.x},product(rotation(axes),a.attitude),true};
        x.parts=std::move(e.state.parts);return {x,b.stale};
    }
    return e;
}
Snapshot Scene::snapshot(double now,const CameraState& camera) const{Snapshot result{revision_,now,{},camera,{},{},{},{},false};result.clock.utcSeconds=now;result.simulation.utcSeconds=now;for(const auto& [id,l]:layers_)result.layers.push_back({id,l.name,l.source,l.anchor,l.kind,l.frame,l.data->positions.size(),l.color,l.widthPixels,l.clearanceMeters,l.visible});for(const auto& [id,m]:models_)result.models.push_back({id,m->source,m->sourceBytes,m->vertices,m->triangles,m->sizeUnits,m->radiusUnits});for(const auto& [id,o]:objects_){(void)id;result.objects.push_back(o);result.objects.back().samples.assign(1,evaluate(id,now).state);}return result;}
}
