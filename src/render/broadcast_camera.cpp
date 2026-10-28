#include "broadcast_camera.hpp"
#include <cmath>
#include <limits>
using namespace threepp;
namespace albert {
namespace {
constexpr float pi=3.14159265358979f,metres=6378137.f;
Vector3 vec(core::Vector p){return {float(p.x),float(p.y),float(p.z)};}
// A conservative solid envelope, including the current hinge pose. Cameras stay outside open lattice too.
void outsideBox(Vector3& eye,Vector3 centre,Vector3 half,Quaternion rotation,float margin){
    Vector3 p=Vector3(eye).sub(centre).applyQuaternion(rotation.clone().invert());half.addScalar(margin);
    if(std::abs(p.x)>=half.x||std::abs(p.y)>=half.y||std::abs(p.z)>=half.z)return;
    const float dx=half.x-std::abs(p.x),dy=half.y-std::abs(p.y),dz=half.z-std::abs(p.z);
    if(dx<=dy&&dx<=dz)p.x=std::copysign(half.x,p.x);else if(dy<=dz)p.y=std::copysign(half.y,p.y);else p.z=std::copysign(half.z,p.z);
    eye.copy(p.applyQuaternion(rotation).add(centre));
}
Quaternion partRotation(double heading,double tilt){
    Quaternion q;q.setFromAxisAngle(Vector3(0,0,1),float(-heading));Quaternion t;t.setFromAxisAngle(Vector3(1,0,0),float(-tilt));return q.multiply(t);
}
void clearStructures(Vector3& eye,const core::Scenery& scenery,const std::vector<float>* angles,float margin){
    for(int pass=0;pass<4;++pass){
        for(const auto& p:scenery.parts){
            if(p.name.find("vent")!=std::string::npos||p.name.find("light")!=std::string::npos||p.name.find("beacon")!=std::string::npos)continue;
            Vector3 centre=vec(p.centre);Quaternion q=partRotation(p.heading,p.tilt);
            if(p.structure&&p.structure<=scenery.bars.size()){
                const auto& b=scenery.bars[p.structure-1];if(b.hinged){Quaternion h;h.setFromAxisAngle(vec(b.axis).normalize(),angles&&p.structure<=angles->size()?(*angles)[p.structure-1]:0.f);
                    centre.sub(vec(b.pivot)).applyQuaternion(h).add(vec(b.pivot));q.premultiply(h);}}
            outsideBox(eye,centre,vec(p.size).multiplyScalar(.5f),q,margin);
        }
        for(const auto& b:scenery.buildings)outsideBox(eye,vec(b.centre),vec(b.size).multiplyScalar(.5f),partRotation(b.heading,0),margin);
        for(std::size_t i=0;i<scenery.bars.size();++i){const auto& b=scenery.bars[i];if(b.radius<=0)continue;
            Vector3 a=vec(b.from),z=vec(b.to);if(b.hinged){Quaternion h;h.setFromAxisAngle(vec(b.axis).normalize(),angles&&i<angles->size()?(*angles)[i]:0.f);
                a.sub(vec(b.pivot)).applyQuaternion(h).add(vec(b.pivot));z.sub(vec(b.pivot)).applyQuaternion(h).add(vec(b.pivot));}
            Vector3 d=Vector3(z).sub(a);const float t=std::clamp(Vector3(eye).sub(a).dot(d)/std::max(d.lengthSq(),1e-6f),0.f,1.f);
            Vector3 nearest=Vector3(a).addScaledVector(d,t),away=Vector3(eye).sub(nearest);const float r=float(b.radius)+margin;
            if(away.length()<r){if(away.lengthSq()<1e-6f)away.set(1,0,0);eye.copy(nearest).add(away.normalize().multiplyScalar(r));}
        }
        eye.z=std::max(eye.z,margin+2.f);
    }
}
Vector3 saneUp(Vector3 up,const Vector3& eye,const Vector3& aim,Vector3 fallback){
    const Vector3 forward=Vector3(aim).sub(eye).normalize();up.addScaledVector(forward,-up.dot(forward));
    if(up.lengthSq()<.0025f){up=fallback;up.addScaledVector(forward,-up.dot(forward));}
    if(up.lengthSq()<.0001f)up=Vector3(1,0,0).addScaledVector(forward,-forward.x);
    return up.normalize();
}
}
std::optional<core::ObjectId> BroadcastCamera::nextFocus(const core::Scene& registry,const SceneObjects& objects,const core::ObjectId& current,double utc){
    if(mode_!=core::CameraMode::Auto)return {};
    const auto target=registry.objects().find(current);
    if(target==registry.objects().end()||!target->second.look)return {};
    if(primary_.empty()||utc<directorUtc_||(current!=primary_&&current!=booster_)){
        primary_=current;booster_.clear();directorDone_=false;caughtAt_=-1;
    }
    directorUtc_=utc;
    // A new stack on the pad starts a new programme even when the same scene IDs are reused.
    if(target->second.look->length>100&&directorDone_){booster_.clear();directorDone_=false;caughtAt_=-1;}
    if(!registry.objects().contains(primary_)||!registry.get(primary_).look){primary_=current;return {};}
    if(booster_.empty()&&!directorDone_&&registry.get(primary_).look->engines.size()<=10){
        for(const auto& [id,o]:registry.objects())if(id!=primary_&&o.look&&o.look->engines.size()>10&&o.look->length<100&&o.visible){
            booster_=id;separatedAt_=utc;break;}
    }
    if(booster_.empty()||directorDone_)return current==primary_?std::nullopt:std::optional<core::ObjectId>(primary_);
    if(!registry.objects().contains(booster_)||!objects.nodes().contains(booster_)){directorDone_=true;return primary_;}
    const auto state=registry.evaluate(booster_,utc).state;
    if(state.parts&&!state.parts->empty()&&state.parts->back()>=0){directorDone_=true;return primary_;}
    // Observe the catch from existing render telemetry; never write to a flight or its controller.
    bool caught=false;
    for(const auto& [id,o]:registry.objects())if(o.scenery&&objects.nodes().contains(id)){
        const auto& site=*objects.nodes().at(id);Vector3 p=Vector3(objects.nodes().at(booster_)->position).sub(site.position).multiplyScalar(metres).applyQuaternion(site.quaternion.clone().invert());
        const auto ground=registry.evaluate(id,utc).state.velocityMetersPerSecond;
        const auto v=state.velocityMetersPerSecond-ground;
        const double speed=std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);
        if(std::hypot(p.x,p.y)<100&&p.z>30&&p.z<220&&speed<3)caught=true;
    }
    if(caught){if(caughtAt_<0)caughtAt_=utc;}else caughtAt_=-1;
    if(caughtAt_>=0&&utc-caughtAt_>=6){directorDone_=true;return primary_;}
    if(utc-separatedAt_<8)return current==primary_?std::nullopt:std::optional<core::ObjectId>(primary_);
    return current==booster_?std::nullopt:std::optional<core::ObjectId>(booster_);
}
std::optional<BroadcastView> BroadcastCamera::update(const core::Scene& registry,const SceneObjects& objects,const core::ObjectId& id,double utc,float seconds){
    using M=core::CameraMode;
    if(mode_==M::Orbit)return {};
    const auto found=registry.objects().find(id);const auto node=objects.nodes().find(id);
    if(found==registry.objects().end()||!found->second.look||node==objects.nodes().end()||!node->second->visible){reset();return {};}
    const auto& object=found->second;const auto& look=*object.look;const auto& hull=*node->second;
    // Prefer the flight's own site; a separated stage shares its parent's scenery.
    std::string siteId;float nearest=std::numeric_limits<float>::max();
    const std::string own=id=="flight"?"site":id.rfind("flight.",0)==0?"site"+id.substr(6):"";
    for(const auto& [key,o]:registry.objects())if(o.scenery&&objects.nodes().contains(key)){
        const float range=hull.position.distanceTo(objects.nodes().at(key)->position);
        if(key==own){siteId=key;break;}if(range<nearest){nearest=range;siteId=key;}}
    if(siteId.empty()){reset();return {};}
    auto& site=*objects.nodes().at(siteId);const auto inverse=site.quaternion.clone().invert();
    const auto& scenery=*registry.get(siteId).scenery;const auto angles=registry.evaluate(siteId,utc).state.parts;
    const Vector3 position=Vector3(hull.position).sub(site.position).multiplyScalar(metres).applyQuaternion(inverse);
    const auto state=registry.evaluate(id,utc).state;
    Vector3 velocity=Vector3(float(state.velocityMetersPerSecond.x),float(state.velocityMetersPerSecond.z),float(-state.velocityMetersPerSecond.y));
    // Remove the ground frame's velocity (Earth rotation) before choosing a chase direction or detecting descent.
    const auto groundVelocity=registry.evaluate(siteId,utc).state.velocityMetersPerSecond;
    velocity.sub(Vector3(float(groundVelocity.x),float(groundVelocity.z),float(-groundVelocity.y))).applyQuaternion(inverse);
    Quaternion attitude=inverse;attitude.multiply(hull.quaternion);
    const bool booster=look.engines.size()>10;
    const bool fresh=!ready_||subject_!=id||site_!=siteId||instance_!=object.instance||utc<utc_||utc-utc_>30;
    const float dt=std::clamp(seconds,0.f,.25f);
    if(fresh){age_=0;returning_=false;staged_=false;stagingUntil_=0;travel_.set(1,0,0);}
    else age_+=dt;
    bool staging=false;
    for(const auto& [other,o]:registry.objects())if(other!=id&&o.look&&objects.nodes().contains(other)&&objects.nodes().at(other)->visible){
        const float gap=hull.position.distanceTo(objects.nodes().at(other)->position)*metres;
        if(position.z>1000&&gap<look.length/2+float(o.look->length)/2+250)staging=true;}
    if(staging&&!staged_){staged_=true;stagingUntil_=utc+8;}
    if(velocity.z< -15||(position.z<330&&velocity.z< -.5f))returning_=true;
    float heat=0;if(state.parts&&state.parts->size()>=7)heat=(*state.parts)[state.parts->size()-6];
    M shot=mode_;bool fisheye=shot==M::TowerFisheye||shot==M::OnboardFisheye||shot==M::EngineFisheye||shot==M::HotstageFisheye||shot==M::FlapFisheye;
    if(shot==M::Auto){
        const float horizontal=std::hypot(position.x,position.y);
        // Catch is latched until the next flight/rewind, so near-zero touchdown speed cannot send the director back to ascent.
        if(returning_&&horizontal<180&&position.z<330)shot=M::Tower;
        else if(!booster&&heat>30000)shot=M::Flap;
        else if(staged_&&utc<stagingUntil_)shot=booster?M::Hotstage:M::Engine;
        else if(returning_)shot=horizontal<(shot_==M::Drone?1800:1500)&&position.z<(shot_==M::Drone?2100:1800)?M::Drone:M::Tracking;
        else shot=position.z<(shot_==M::Tower?280:240)?M::Tower:M::Tracking;
    }
    if(shot==M::TowerFisheye)shot=M::Tower;
    if(shot==M::Onboard||shot==M::OnboardFisheye)shot=booster?M::Engine:M::Flap;
    if(shot==M::EngineFisheye)shot=M::Engine;
    if(shot==M::HotstageFisheye)shot=M::Hotstage;
    if(shot==M::FlapFisheye)shot=M::Flap;
    const bool cut=fresh||shot!=shot_||Vector3(position).sub(previous_).length()>2000;
    Vector3 eye,aim=position,up(0,0,1);float fov=45;
    const float length=float(look.length),radius=float(look.diameter)*.5f;
    auto projectedSize=[&](const Vector3& eye){
        const Vector3 axis=Vector3(0,1,0).applyQuaternion(attitude),line=Vector3(position).sub(eye).normalize();
        const float along=std::clamp(std::abs(axis.dot(line)),0.f,1.f);
        return std::max(radius*3.f,length*std::sqrt(1-along*along)+radius*2*along);
    };
    if(shot==M::Tracking){
        eye.set(500,-2500,1.85f); // Public dry beach, beyond the 2 km closure; the saved coastline is ~620 m east here.
        const float range=eye.distanceTo(position),extent=projectedSize(eye);
        fov=std::clamp(2*std::atan2(extent*1.65f,std::max(range,1.f))*180/pi,.035f,42.f);
        // Critically damped mount approximation, with a range-bounded pointing error (never shakes the vehicle off screen).
        const float alpha=1-std::exp(-dt/ .16f);
        if(cut)aim_=position;else aim_.lerp(position,alpha);
        Vector3 error=Vector3(aim_).sub(position);const float maxError=extent*.28f;
        if(error.length()>maxError)aim_.copy(position).add(error.normalize().multiplyScalar(maxError));
        aim=aim_;const float jitter=extent*.002f; // Presentation time, deterministic and bounded, no random walk.
        aim.x+=jitter*(std::sin(age_*7.1f)+.3f*std::sin(age_*17));aim.z+=jitter*.6f*std::sin(age_*9.3f);
        if(!cut)fov=std::lerp(fov_,fov,1-std::exp(-dt*6));
    }else if(shot==M::Drone){
        Vector3 horizontal=velocity;horizontal.z=0;
        if(horizontal.length()>8){
            const float wanted=std::atan2(horizontal.y,horizontal.x),heading=std::atan2(travel_.y,travel_.x);
            const float eased=cut?wanted:heading+std::remainder(wanted-heading,2*pi)*(1-std::exp(-dt*2));
            travel_.set(std::cos(eased),std::sin(eased),0);
        }
        const float away=std::max(150.f,length*2.3f);
        eye=Vector3(position).addScaledVector(travel_,-away);eye.z+=std::max(80.f,length*.8f);eye.y-=away*.45f;
        if(!cut)eye=Vector3(eye_).add(Vector3(position).sub(previous_)).lerp(eye,1-std::exp(-dt*2.8f));
        fov=42;
    }else if(shot==M::Tower){
        const core::SceneryPart* crown=nullptr;
        for(const auto& p:scenery.parts)if(p.name=="tower crane crown"&&(!crown||vec(p.centre).lengthSq()<vec(crown->centre).lengthSq()))crown=&p;
        if(!crown){reset();return {};}
        eye=Vector3(float(crown->size.x)*.5f+2.8f,-float(crown->size.y)*.5f-3.f,2.f).applyQuaternion(partRotation(crown->heading,0)).add(vec(crown->centre));
        // Fixed crown mount; its pan/tilt follows the approach without relocating the eye.
        fov=returning_?74.f:96.f;
        Vector3 pins;unsigned pinCount=0;std::vector<Vector3> contacts;
        for(const auto& p:look.parts)if(p.name=="catch fitting"||p.name=="pin"){
            Vector3 point(float(-p.hinge.y),length*.5f-float(p.hinge.x),float(p.hinge.z));pins.add(point);++pinCount;
            contacts.push_back(point.applyQuaternion(attitude).add(position));}
        Vector3 rails;unsigned railCount=0;
        for(std::size_t i=0;i<scenery.bars.size();++i){const auto& b=scenery.bars[i];
            if(!b.hinged||b.radius<=0||std::abs(b.from.z-b.to.z)>1||vec(b.to-b.from).length()<5)continue;
            Quaternion h;
            h.setFromAxisAngle(vec(b.axis).normalize(),angles&&i<angles->size()?(*angles)[i]:0.f);
            for(const auto endpoint:{b.from,b.to}){
                Vector3 point=vec(endpoint).sub(vec(b.pivot)).applyQuaternion(h).add(vec(b.pivot));
                rails.add(point);++railCount;contacts.push_back(point);}
        }
        if(pinCount&&railCount){
            pins.multiplyScalar(1.f/float(pinCount)).applyQuaternion(attitude).add(position);rails.divideScalar(float(railCount));
            const bool atCatch=pins.distanceTo(rails)<8&&velocity.z<=.5f&&length<100;
            if(returning_||atCatch){
                aim=pins; // The catch fitting, rather than the vehicle centre several storeys below the arms.
                if(pins.distanceTo(rails)<90){
                    const Vector3 toPins=Vector3(pins).sub(eye),toRails=Vector3(rails).sub(eye);
                    Vector3 bisector=Vector3(toPins).normalize().add(Vector3(toRails).normalize());
                    if(bisector.lengthSq()>.001f){
                        const Vector3 direction=bisector.normalize();float angle=0;
                        for(const auto& point:contacts)angle=std::max(angle,direction.angleTo(Vector3(point).sub(eye)));
                        const float fit=2*std::atan(std::tan(std::min(angle,pi*.45f))/.8f)*180/pi;
                        // Above the crown, pins and rails can lie on opposite sides of the eye. Keep following the pins
                        // until both fit, rather than clamping a very wide view and losing the incoming booster.
                        if(fit<=96){aim=Vector3(eye).add(direction.clone().multiplyScalar((toPins.length()+toRails.length())*.5f));fov=std::max(36.f,fit);}
                    }
                }
            }
        }
        if(!cut){aim=Vector3(aim_).lerp(aim,1-std::exp(-dt*5));fov=std::max(fov,std::lerp(fov_,fov,1-std::exp(-dt*4)));}
        // Looking almost vertically down: north at the top instead of an undefined roll.
        up=saneUp(up,eye,aim,{0,1,0});
    }else{
        Vector3 localEye,localAim;
        if(shot==M::Hotstage){
            float ring=length*.5f-1;
            for(const auto& p:look.parts)if(p.name=="vent"){ring=length*.5f-float(p.hinge.x);break;}
            localEye.set(radius+.45f,ring+.5f,0);localAim.set(radius-.2f,ring-13,0);fov=100;
        }
        else if(shot==M::Flap){
            // Outside the hull above the aft flap, looking aft: the hull edge and moving flap stay in frame.
            float flap=-length*.36f;
            for(const auto& p:look.parts)if(p.name=="flap aft left"){flap=length*.5f-float(p.hinge.x);break;}
            localEye.set(radius+.7f,flap+9.f,-.6f);localAim.set(radius+2.7f,flap-4.f,1.5f);fov=96;
        }else{
            // Aft skirt bracket outside the exit perimeter, looking forward across the bell mouths.
            // Engine positions describe exits; the throats are one bell length forward of those points.
            float y=-length*.5f;for(const auto& e:look.engines)y=std::max(y,float(look.length/2-e.position.x));
            const float mountRadius=radius+1.5f;float best=-1e9f,angle=0;
            for(int i=0;i<72;++i){const float a=2*pi*float(i)/72;float clearance=1e9f;
                for(const auto& e:look.engines){const float dx=mountRadius*std::cos(a)+float(e.position.y),dz=mountRadius*std::sin(a)-float(e.position.z);
                    clearance=std::min(clearance,std::hypot(dx,dz)-float(e.exitRadius));}
                if(clearance>best){best=clearance;angle=a;}}
            localEye.set(mountRadius*std::cos(angle),y-1.6f,mountRadius*std::sin(angle));localAim.set(0,y+1.f,0);fov=112;
        }
        eye=localEye.applyQuaternion(attitude).add(position);aim=localAim.applyQuaternion(attitude).add(position);
        up=Vector3(0,1,0).applyQuaternion(attitude); // Rigid roll follows the hull, including entry and flip.
    }
    const bool attached=shot==M::Engine||shot==M::Hotstage||shot==M::Flap;
    if(!attached){
        clearStructures(eye,scenery,angles.get(),shot==M::Drone?8.f:.6f);
        if(shot==M::Drone){
            if(cloudTop_>cloudBase_&&eye.z>cloudBase_-60&&eye.z<cloudTop_+60)
                eye.z=position.z<(cloudBase_+cloudTop_)*.5f?std::max(60.f,cloudBase_-80):cloudTop_+80;
            // Use the actual advecting launch-bank ellipsoids, including wind and their growth after liftoff.
            site.updateWorldMatrix(true,true);
            site.traverse([&](Object3D& cloud){if(cloud.name!="launch cloud"||!cloud.visible)return;
                Vector3 worldEye=Vector3(eye).applyQuaternion(site.quaternion).divideScalar(metres).add(site.position);
                cloud.worldToLocal(worldEye);if(worldEye.length()<1.2f){if(worldEye.lengthSq()<1e-6f)worldEye.set(0,0,1);
                    worldEye.normalize().multiplyScalar(1.2f);cloud.localToWorld(worldEye);eye.copy(worldEye.sub(site.position).multiplyScalar(metres).applyQuaternion(inverse));}});
            clearStructures(eye,scenery,angles.get(),8.f);
            if(cloudTop_>cloudBase_&&eye.z>cloudBase_-60&&eye.z<cloudTop_+60)
                eye.z=position.z<(cloudBase_+cloudTop_)*.5f?std::max(60.f,cloudBase_-80):cloudTop_+80;
        }
    }else{
        // If scenery occupies an attached mount (for example an unexpected QD pose), cut to the safe shore rig.
        // Moving the eye away from that mount would violate the rigid vehicle attachment.
        Vector3 clear=eye;clearStructures(clear,scenery,angles.get(),.15f);
        if(clear.distanceTo(eye)>.2f){
            shot=M::Tracking;fisheye=false;eye.set(500,-2500,1.85f);aim=position;up.set(0,0,1);
            fov=std::clamp(2*std::atan2(projectedSize(eye)*1.65f,eye.distanceTo(position))*180/pi,.035f,42.f);
            clearStructures(eye,scenery,angles.get(),.6f);
        }
    }
    up=saneUp(up,eye,aim,attached?Vector3(0,0,1).applyQuaternion(attitude):Vector3(0,1,0));
    eye_=eye;aim_=aim;fov_=fov;previous_=position;subject_=id;site_=siteId;instance_=object.instance;utc_=utc;ready_=true;shot_=shot;
    auto world=[&](Vector3 p){return p.applyQuaternion(site.quaternion).divideScalar(metres).add(site.position);};
    return BroadcastView{world(eye),world(aim),up.applyQuaternion(site.quaternion),fisheye?145.f:fov,fisheye,shot};
}
}
