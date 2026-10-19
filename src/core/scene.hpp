#pragma once
#include "simulation.hpp"
#include "time.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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
enum class Shape {Builtin,Sphere,Box,Cylinder,Model};
struct Appearance {Shape shape=Shape::Sphere;Vector sizeMeters{1000,1000,1000};std::uint32_t color=0x77cddd;std::string modelId;double metersPerUnit=1;};
struct Object {
    ObjectId id;std::string name,source;
    Appearance appearance;
    double radiusMeters=500,defaultDistanceMeters=2000,minDistanceMeters=550;
    bool builtin=false,visible=true,providerManaged=false;std::uint64_t instance=0;
    std::vector<Sample> samples;
};
struct Evaluated {Sample state;bool stale=false;};
struct CameraState {ObjectId target="earth";Vector positionMeters;double distanceMeters=0;bool transitioning=false;};
struct Model;
struct ModelInfo {std::string id,source;std::size_t bytes=0,vertices=0,triangles=0;Vector size;double radius=0;};
enum class LayerKind {Points,Polyline};
enum class LayerFrame {GeocentricICRF,ObjectLocal,EarthSurface};
struct Geographic {double longitude=0,latitude=0;};
struct LayerData {std::vector<Vector> positions;};
struct Layer {
    std::string id,name,source,anchor;LayerKind kind=LayerKind::Points;LayerFrame frame=LayerFrame::GeocentricICRF;
    std::uint32_t color=0x66d9ee;float widthPixels=6;double clearanceMeters=100;
    bool visible=true;std::uint64_t instance=0;std::shared_ptr<const LayerData> data;
};
struct LayerInfo {std::string id,name,source,anchor;LayerKind kind;LayerFrame frame;std::size_t points=0;std::uint32_t color;float widthPixels;double clearanceMeters;bool visible;};
// utcSeconds is the simulated instant of the object states; clock is the disciplined
// real clock, which equals it only while the simulation is live.
struct Snapshot {std::uint64_t revision=0;double utcSeconds=0;std::vector<Object> objects;CameraState camera;time::Status clock;std::vector<ModelInfo> models;std::vector<LayerInfo> layers;time::Simulated simulation;bool approximatePositions=false;};
struct NaturalBody;struct LocalOrbit;
class Scene {
public:
    static constexpr size_t maxObjects=2048,maxSamples=256;
    Scene();
    static constexpr std::size_t maxLayers=64,maxLayerInputPoints=2048,maxLayerPoints=16384,maxSceneLayerPoints=32768;
    void create(Object object,bool replace=false); // replace: an existing object's definition, keeping its id and anchored layers.
    void createLayer(Layer);
    void removeLayer(const std::string& id);
    void layerVisible(const std::string& id,bool visible);
    const std::map<std::string,Layer>& layers()const{return layers_;}
    void installModel(std::shared_ptr<const Model>);
    void removeModel(const std::string& id);
    const std::map<std::string,std::shared_ptr<const Model>>& models()const{return models_;}
    void include(const NaturalBody& body);
    void include(const LocalOrbit& body);
    void remove(const ObjectId& id);
    void visible(const ObjectId& id,bool enabled);
    void samples(const ObjectId& id,std::vector<Sample> values);
    void replace(const ObjectId& id,std::vector<Sample> values);
    void builtinState(const ObjectId& id,Sample value);
    const Object& get(const ObjectId& id) const;
    const std::map<ObjectId,Object>& objects() const{return objects_;}
    Evaluated evaluate(const ObjectId& id,double utcSeconds) const;
    Snapshot snapshot(double utcSeconds,const CameraState& camera) const;
    void cameraChanged(){++revision_;}
    std::uint64_t revision() const{return revision_;}
private:
    std::map<ObjectId,Object> objects_;
    std::map<std::string,std::shared_ptr<const Model>> models_;
    std::map<std::string,Layer> layers_;
    std::uint64_t revision_=0;
};
bool validObjectId(std::string_view id);
void validate(Sample& sample);
}
