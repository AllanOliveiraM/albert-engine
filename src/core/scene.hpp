#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace albert::core {
using ObjectId=std::string;
inline constexpr double earthRadiusMeters=6378137.;
struct Vector {double x=0,y=0,z=0;Vector operator+(Vector b) const{return {x+b.x,y+b.y,z+b.z};}Vector operator-(Vector b) const{return {x-b.x,y-b.y,z-b.z};}Vector operator*(double k) const{return {x*k,y*k,z*k};}};
struct Quaternion {double x=0,y=0,z=0,w=1;};
struct Sample {
    Sample(double utc=0,Vector position={},Vector velocity={},std::optional<Quaternion> rotation={},bool measuredVelocity=false):
        utcSeconds(utc),positionMeters(position),velocityMetersPerSecond(velocity),attitude(rotation),hasVelocity(measuredVelocity){}
    double utcSeconds=0; // Unix UTC seconds; not receipt time or TDB.
    Vector positionMeters,velocityMetersPerSecond;
    std::optional<Quaternion> attitude; // Local axes -> geocentric ICRF, XYZW.
    bool hasVelocity=false;
};
enum class Shape {Builtin,Sphere,Box,Cylinder};
struct Appearance {Shape shape=Shape::Sphere;Vector sizeMeters{1000,1000,1000};std::uint32_t color=0x77cddd;};
struct Object {
    ObjectId id;std::string name,source;
    Appearance appearance;
    double radiusMeters=500,defaultDistanceMeters=2000,minDistanceMeters=550;
    bool builtin=false,visible=true;
    std::vector<Sample> samples;
};
struct Evaluated {Sample state;bool stale=false;};
struct CameraState {ObjectId target="earth";Vector positionMeters;double distanceMeters=0;bool transitioning=false;};
struct Snapshot {std::uint64_t revision=0;double utcSeconds=0;std::vector<Object> objects;CameraState camera;};
class Scene {
public:
    static constexpr size_t maxObjects=2048,maxSamples=256;
    Scene();
    void create(Object object);
    void remove(const ObjectId& id);
    void visible(const ObjectId& id,bool enabled);
    void samples(const ObjectId& id,std::vector<Sample> values);
    void builtinState(const ObjectId& id,Sample value);
    const Object& get(const ObjectId& id) const;
    const std::map<ObjectId,Object>& objects() const{return objects_;}
    Evaluated evaluate(const ObjectId& id,double utcSeconds) const;
    Snapshot snapshot(double utcSeconds,const CameraState& camera) const;
    void cameraChanged(){++revision_;}
    std::uint64_t revision() const{return revision_;}
private:
    std::map<ObjectId,Object> objects_;
    std::uint64_t revision_=0;
};
void validate(Sample& sample);
}
